#include "stdafx.h"

#include "TerrainBlockDesert.h"

#include "Omnigen.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include <functional>
#include "Utils/Interpolation.h"
#include <atomic>
#include <noise/noise.h>
#include <random>
#include "DesertSubtypes.h"
#include "DuneGraph.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

namespace Generation
{
    float TerrainBlockCluster<ETerrainBlock::Desert>::chance(const BlockChanceData& data)
    {
        if (!data.biomeDomain)
            return 0.0f;

        auto terrainParams = data.terrainDomain->getData<EDomainType::Terrain>();
        auto biomeParams   = data.biomeDomain  ->getData<EDomainType::Biome>();

        if (biomeParams->humidity == EHumidity::Desert &&
            (biomeParams->temperature == ETemperature::Subtropical || biomeParams->temperature == ETemperature::Tropical))
        {
            return (1.0f - data.steepness) * 1.1f; //std::pow(std::clamp(1.0f - data.steepness, 0.0f, 1.0f), 3);
        }

        return 0.0f;
    }

    TerrainBlockCluster<ETerrainBlock::Desert>::TerrainBlockCluster(const ClusterData<ETerrainBlock::Desert>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Desert, data.cells, data.centerCell)
        {
            constexpr float dunesThreshold = 0.8f;
            if (getDesertNeighborsPart() < dunesThreshold)
            {
                auto&& diagram = Data::get()->getTerrainCells();
                const auto& coreCell = diagram->getCellAt(keyCell);
                const GVector2D& center = coreCell.getVoronoiCenter();
                spawn<DLineMarker>(center);

                subCluster = QSharedPointer<DesertSubCluster<EDesertBlockSubtype::DuneSand>>::create(this);
            }
            else
                subCluster = data.subData->createSubCluster(this);

            subType = subCluster->type;
        }

    void TerrainBlockCluster<ETerrainBlock::Desert>::generate()
    {
        OmniProfile("Desert cluster geometry");

        computeBorderPoints();
        subCluster->generate();
    }

    float TerrainBlockCluster<ETerrainBlock::Desert>::getDesertNeighborsPart() const
    {
        if (cells.empty())
            return 0.f;

        auto&& diagram = Data::get()->getTerrainCells();
        auto&& blockTypeMap = Data::get()->getBlockTypeMap();

        std::unordered_set<int> clusterNeighbors;

        for (int id: cells) // clusterData->cells
        {
            const auto& currCell = diagram->getCellAt(id);
            const auto& neighbors = currCell.getNeighbors();
            for (auto it = neighbors.keyBegin(); it != neighbors.keyEnd(); ++it)
            {
                const int neighbor = *it;
                if (cells.contains(neighbor))
                    continue;
                clusterNeighbors += neighbor;
            }
        }

        float total = 0.f;
        float desert = 0.f;
        for (int id: clusterNeighbors)
        {
            total += 1.f;
            if (blockTypeMap[id] == ETerrainBlock::Desert)
                desert += 1.f;
        }
        const float part = desert / total;
        return part;
    }

    void TerrainBlockCluster<ETerrainBlock::Desert>::fillResultMesh(MeshConnector& meshConnector)
    {
        meshConnector.indices.shrink_to_fit();
        GeometryData<TerrainMeshVertex> geometry;
        geometry.indices = std::move(meshConnector.indices);
        geometry.vertices.reserve(meshConnector.vertices.size());
        for (const auto& pt : meshConnector.vertices)
        {
            auto tmv = TerrainMeshVertex{ pt, {}, *this };
            tmv.displacementFactor = 0.f;
            geometry.vertices <<= std::move(tmv);

            if (biomeTexPackSlot >= 0)
                geometry.vertices.back().biomeTexWeights = 255u << (8 * biomeTexPackSlot);
        }

        section = spawnBatched(std::move(geometry), makeBatchParams());
    }

    ClusterData<ETerrainBlock::Desert>::ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Desert>* metaCluster, int id)
        : ClusterDataBase(id)
        , centerCell(id)
    {
        // TEMP STABLE
        // subData = QSharedPointer<DesertClusterSubData<EDesertBlockSubtype::DuneNabkha>>::create(this);
        // return;

        if (metaCluster->forcedType)
        {
            subData = EDesertBlockSubtypeConstexpr::UseIn<EAC::CreateDesertSubData>(*metaCluster->forcedType, this);
            return;
        }

        auto&& diagram = Data::get()->getTerrainCells();
        const auto& coreCell = diagram->getCellAt(centerCell);
        const GVector2D windDirection = Data::get()->getWindVector(coreCell.getVoronoiCenter());
        const float windSqStrength = windDirection.lengthSquared();

        constexpr float windStrengthSqThresold = 0.3f * 0.3f;
        constexpr float starChance = 0.5f;
        constexpr float nabkhaChance = 0.1f;

        auto chosenType = EDesertBlockSubtype::DuneBarchan;
        if (getRandomFloat() > 1.f - nabkhaChance)
            chosenType = EDesertBlockSubtype::DuneNabkha;
        else if (windSqStrength < windStrengthSqThresold && getRandomFloat() > 1.f - starChance)
            chosenType = EDesertBlockSubtype::DuneStar;

        subData = EDesertBlockSubtypeConstexpr::UseIn<EAC::CreateDesertSubData>(chosenType, this);
    }

    std::unordered_set<int> ClusterData<ETerrainBlock::Desert>::customGrow(const std::unordered_set<int>& candidates)
    {
        return subData->customGrow(candidates);
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Desert>::initialize()
    {
        computeBaseMaterial();

        if (cells.size() < 20)
            return;

        constexpr float longitudinalChance = 0.15f;
        if (getRandomFloat() > 1.f - longitudinalChance)
            forcedType = EDesertBlockSubtype::DuneLongitudinal;
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Desert>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        TerrainBlockMetaClusterBase::computePackParams(lithoCluster, biomeDomain, averageIHLevel);

        // No vegetation?
        setPackParam(&packParams, 1, 0.0f);
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Desert>::spawnClusters()
    {
        spawnBigClusters(); 
    }


    DesertClusterSubDataBase::DesertClusterSubDataBase(ClusterData<ETerrainBlock::Desert>* inBaseData)
        : baseData(inBaseData)
    {
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Desert>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
    omniBin << object.subType;
    Generation::EDesertBlockSubtypeConstexpr::UseIn<Generation::EAC::SaveDesertSubCluster>(object.subType, object.subCluster, omniBin);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Desert>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
    omniBin >> object.subType;
    Generation::EDesertBlockSubtypeConstexpr::UseIn<Generation::EAC::LoadDesertSubCluster>(object.subType, object.subCluster, omniBin);
    object.subCluster->cluster = &object;
    object.subCluster->type = object.subType;
}

BezierCurve2D computeRandomBezierCurve(const Polygon2D& restrictionPolygon, int numParts, float widthFactor /*= 1.f*/)
{
    Q_ASSERT(restrictionPolygon.getPts().size() == 4);
    return computeRandomBezierCurve(restrictionPolygon[0], restrictionPolygon[1], restrictionPolygon[2], restrictionPolygon[3], numParts, widthFactor);
}

BezierCurve2D computeRandomBezierCurve(const GVector2D& p0, const GVector2D& p1, const GVector2D& p2, const GVector2D& p3, int numParts, float widthFactor /*= 1.f*/)
{
    GVector2D p1New = std::lerp(p1, p3, 0.5f * (1.f - widthFactor));
    GVector2D p3New = std::lerp(p1, p3, 0.5f * (1.f + widthFactor));

    Polygon2D polygon(std::vector{p0, p1New, p2, p3New});

    constexpr int iterationsLimit = 10;
    int itCounter = 0;

    // prevent concave restriction polygon - it can lead to result curve will be situated out of bounds!
    while (true)
    {
        const bool isConcave0 = polygon.isConcaveVertex(0);
        const bool isConcave2 = polygon.isConcaveVertex(2);

        if (!isConcave0 && !isConcave2)
            break;

        if (++itCounter > iterationsLimit)
        {
            Q_ASSERT_X(false, "computeRandomBezierCurve", "incorrect (concave) restriction polygon");
#if !NDEBUG
            Polygon2D originalBuggyPolygon(std::vector{p0, p1, p2, p3});
            originalBuggyPolygon.debugPlot(Colors::robinEggBlue, 1000.f);
            spawn<DLineMarker>((QVector3D)p0, 1000.f, Colors::robinEggBlue);
            spawn<DLineMarker>((QVector3D)p1, 1000.f, Colors::robinEggBlue);
            spawn<DLineMarker>((QVector3D)p2, 1000.f, Colors::robinEggBlue);
            spawn<DLineMarker>((QVector3D)p3, 1000.f, Colors::robinEggBlue);
#endif
            break;
        }

        if (isConcave0)
        {
            p1New = (p1New + p2) * 0.5f;
            p3New = (p3New + p2) * 0.5f;
        }
        else if (isConcave2)
        {
            p1New = (p1New + p0) * 0.5f;
            p3New = (p3New + p0) * 0.5f;
        }

        polygon = Polygon2D(std::vector{p0, p1New, p2, p3New});
    }

    std::vector<GVector2D> controlPoints;
    controlPoints.reserve(numParts + 1);

    controlPoints << p0;
    for (int i = 1; i < numParts; ++i)
    {
        const float t = i / (float)numParts;
        const GVector2D choices[2] =
        {
            (t < 0.5f) ? std::lerp(p0, p1New, t * 2.f) : std::lerp(p1New, p2, (t - 0.5f) * 2.f),
            (t < 0.5f) ? std::lerp(p0, p3New, t * 2.f) : std::lerp(p3New, p2, (t - 0.5f) * 2.f)
        };

        controlPoints << choices[Generation::gRandomEngine() % 2];
    }
    controlPoints << p2;

    // debug draw for control points
    // spawn<DLineMarker>(controlPoints, Colors::yellow, false, 200.f);

    return BezierCurve2D(controlPoints);
}


float getSandNoise(float x, float z)
{
    static std::mt19937 randomEngine;
    static noise::module::RidgedMulti noiseSource;
    static noise::model::Plane noiseModel;
    static std::atomic_bool isInited = false;

    if (!isInited)
    {
        static std::mutex guard;
        std::scoped_lock lock(guard);
        noiseSource.SetSeed(randomEngine());
        noiseSource.SetFrequency(2e-3f);
        noiseSource.SetOctaveCount(2);
        noiseModel.SetModule(noiseSource);
        isInited = true;
    }

    return noiseModel.GetValue(x, z);
}

float getSandHeight(const GVector2D& pt)
{
    auto&& dem = Generation::Data::get()->getDEM();
    constexpr float sandNoiseAmplitude = 25.f;
    const float sandHeight = dem->heightData.sample(pt) + getSandNoise(pt.x, pt.z) * sandNoiseAmplitude;
    return sandHeight;
}

QVector3D get3dSandPoint(const QVector3D& pt)
{
    return QVector3D(pt.x(), getSandHeight((GVector2D)pt), pt.z());
}

DuneVertex createDuneVertexAtPoint(const GVector2D& pt)
{
    return DuneVertex(pt, getSandHeight(pt));
}

float getDunePeakHeight(const GVector2D& pt, float min, float max)
{
    auto&& dem = Generation::Data::get()->getDEM();
    return std::uniform_real_distribution<float>(min, max)(Generation::gRandomEngine) + dem->heightData.sample(pt);
}

static CellsLayer createLayer(const CellsLayer& prevLayer, std::unordered_set<int>& allCells)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();
    CellsLayer newLayer;
    for (const int cellId: prevLayer.cells)
    {
        const auto& currCell = diagram->getCellAt(cellId);
        const auto& neighbors = currCell.getNeighbors();
        for (auto&& it = neighbors.begin(); it != neighbors.end(); ++it)
        {
            const int neighbor = it.key();
            if (!allCells.contains(neighbor))
            {
                allCells.insert(neighbor);
                newLayer.cells.insert(neighbor);
            }
        }
    }
    return newLayer;
}

std::unordered_set<int> customGrowWithCellsLayers(const std::unordered_set<int>& candidates, std::unordered_set<int>& clusterCells, std::vector<CellsLayer>& layers, std::unordered_set<int>& allLayersCells)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();
    std::unordered_set<int> newCandidates;

    if (clusterCells.size() == 1)
    {
        layers.reserve(3);
        layers.resize(1);
        const int firstCellId = *clusterCells.begin();
        layers[0].cells.insert(firstCellId);
        layers[0].usedCells.insert(firstCellId);
        allLayersCells.insert(firstCellId);

        layers <<= createLayer(layers.back(), allLayersCells);
    }

    for (const int nid: candidates)
    {
        for (CellsLayer& layer: layers)
        {
            if (layer.isCellInLayer(nid) && !layer.isCellUsed(nid))
            {
                layer.usedCells.insert(nid);
                allLayersCells.insert(nid);
                newCandidates += nid;

                if (layer.getFillingRate() > 0.9f)
                {
                    // add new layer
                    layers <<= createLayer(layers.back(), allLayersCells);
                }
                break;
            }
        }
    }

    clusterCells += newCandidates;
    return newCandidates;
}
