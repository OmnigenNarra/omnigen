//TerrainBlockRidge.cpp
#include "stdafx.h"

#include "TerrainBlockRidge.h"

#include "Omnigen.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/Stages/Landmasses/StageGeneration_Landmasses.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"

#define RIDGE_DEBUGGING 0

namespace Generation
{
    using RidgeCluster = TerrainBlockCluster<ETerrainBlock::Ridge>;

    float linear(float t) { return t; }
    float square(float t) { return t * t; }
    float cubic(float t)  { return t * t * t; }
    float root(float t)   { return std::pow(t, 0.5f); }

    float RidgeCluster::chance(const BlockChanceData& data)
    {
        if (!data.terrainDomain)
            return 0.0f;

        if (data.terrainDomain->getData<EDomainType::Terrain>()->landform == ELandform::Tablelands)
            return 0.0f;

        return (data.isUnderRidge * 1e4f);
    }

    struct NearestRidge
    {
        qint64 guid = 0;
        GVector2D projection;
        float distance = 0.f;
        int index = 0;
    };

    static NearestRidge getNearestRidge(const GVector2D& pt, const std::unordered_map<qint64, std::vector<QVector3D>>& smoothedCurvesMap)
    {
        const auto& ridgesQuadTree = Generation::Data::get()->getMarkerQuadTree<DRidgeMarker>();

        float searchRadius = 420.f;
        NearestRidge result;
        while (true)
        {
            const auto nodes = ridgesQuadTree.find_all_nearest(pt.x, pt.z, searchRadius);
            if (nodes.empty())
            {
                searchRadius *= 2.f;
                continue;
            }

            float minDistanceSq = std::numeric_limits<float>::max();
            for (auto* node : nodes)
            {
                const qint64 guid = node->data.marker->getGuid();
                const auto iter = smoothedCurvesMap.find(guid);
                if (iter == smoothedCurvesMap.end())
                    continue;
                const std::vector<QVector3D>& curve3d = iter->second;
                const auto [proj, distSq, index] = directionalBoundDistance(std::vector<GVector2D>(curve3d.begin(), curve3d.end()), pt, true);
                if (distSq < minDistanceSq)
                {
                    minDistanceSq = distSq;
                    result = NearestRidge {
                        guid,
                        proj,
                        distSq,
                        index
                    };
                }
            }

            if (result.guid == 0)
            {
                searchRadius *= 2.f;
                continue;
            }

            result.distance = sqrtf(result.distance);
            break;
        }

        return result;
    }

    static QSharedPointer<DRidgeMarker> findRidgeById(qint64 guid)
    {
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        const auto ridgeIter = std::find_if(ridges.begin(), ridges.end(), [guid](auto&& rm) {return rm->getGuid() == guid; });
        return *ridgeIter;
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> RidgeCluster::generateMesh()
    {
        auto&& dem = Data::get()->getDEM();
        auto&& diagram = Data::get()->getTerrainCells();
        const auto& ridgesQuadTree = Generation::Data::get()->getMarkerQuadTree<DRidgeMarker>();

        const Polygon2D clusterPolygon = calculatePolygon();
        const GVector2D center = clusterPolygon.getCenter();

        const TerrainBlockMetaCluster<ETerrainBlock::Ridge>& ridgeMetaCluster = *((TerrainBlockMetaCluster<ETerrainBlock::Ridge>*)metaCluster.get());
        const RidgeParameters& ridgeParams = ridgeMetaCluster.ridgeParameters;

        const float widthFactor = getRandomFloat(ridgeParams.minWidthFactor, ridgeParams.maxWidthFactor);
        const float widthDispersion = getRandomFloat(ridgeParams.minWidthDispersion, ridgeParams.maxWidthDispersion);
        const float verticalOffsetFactor = getRandomFloat(ridgeParams.minVerticalOffsetFactor, ridgeParams.maxVerticalOffsetFactor);
        noise::module::Perlin noiseWidthSource;
        noise::model::Plane noiseWidthModel;
        noiseWidthSource.SetSeed(gRandomEngine());
        noiseWidthSource.SetFrequency(getRandomFloat(9e-4f, 2e-3f));
        noiseWidthSource.SetOctaveCount(getRandomInt(2, 4));
        noiseWidthModel.SetModule(noiseWidthSource);

        // collect all ridges points ----------------------------------------------------------------------------------------------------
        std::unordered_set<qint64> ridgeIds;
        for (int cellId: cells)
        {
            const auto& cell = diagram->getCellAt(cellId);
            const GVector2D center = cell.getVoronoiCenter();
            const float radius = cell.getPolygon().getRadius() * 2.f;
            const auto nodes = ridgesQuadTree.find_all_nearest(center.x, center.z, radius);
            for (auto* node : nodes)
                ridgeIds << node->data.marker->getGuid();
        }

        std::vector<GVector2D> ridgePoints;
        ridgePoints.reserve(ridgeIds.size() * 42);

        std::unordered_map<qint64, std::vector<QVector3D>> smoothedRidgesCurvesMap;
        smoothedRidgesCurvesMap.reserve(ridgeIds.size());

        float minH = std::numeric_limits<float>::max();
        float maxH = std::numeric_limits<float>::min();

        for (auto guid: ridgeIds)
        {
            auto&& ridgeMarker = findRidgeById(guid);
            const auto& ridgeControlPts = ridgeMarker->getControlPoints();
            BSplineCurve ridgeCurve(ridgeControlPts);
            auto smoothedRidgePts = ridgeCurve.getPoints(ridgeControlPts.size() * 4);
            spawn<DLineMarker>(smoothedRidgePts, Colors::orange);

            for (const auto& pt: smoothedRidgePts)
            {
                const GVector2D p = (GVector2D)pt;
                constexpr float threshold = 1.f;
                if (clusterPolygon.contains(p, false) && clusterPolygon.getRadiusOfInscribedCircleAtPoint(p) > threshold)
                    ridgePoints << p;

                if (pt.y() < minH)
                    minH = pt.y();
                else if (pt.y() > maxH)
                    maxH = pt.y();
            }

            smoothedRidgesCurvesMap[guid] = std::move(smoothedRidgePts);
        }

        // create mesh ------------------------------------------------------------------------------------------------------------------
        static auto ridgeMeshingParams = []()
        {
            MeshingParams params = getDefaultMeshingParams();
            params.innerSplitFunc = [](const GVector2D& p1, const GVector2D& p2, FFirstLastPolicy policy)
            {
                return splitSegment(Segment2D{ p1, p2 }, policy, true, int(1.8f * getMeshSegmentsAdv(p1, p2)));
            };
            return params;
        }();

        const auto [geom2D, _] = meshPolygon2(clusterPolygon.getPts(), ridgeMeshingParams);
        auto& vert = geom2D.vertices;
        auto& indices = geom2D.indices;

        std::vector<NearestRidge> nearestRidgesToPoints(vert.size());
        std::unordered_map<qint64, std::array<float, 2>> minmaxDistancesToRidge; // ridge id -> [min, max]
        minmaxDistancesToRidge.reserve(10);

        // collect information for every vertex and calculate maximum distances to ridges -----------------------------------------------
        for (int i = 0; i < vert.size(); ++i)
        {
            NearestRidge nr = getNearestRidge(vert[i], smoothedRidgesCurvesMap);
            auto iter = minmaxDistancesToRidge.find(nr.guid);
            if (iter == minmaxDistancesToRidge.end())
            {
                minmaxDistancesToRidge[nr.guid][1] = nr.distance;
                minmaxDistancesToRidge[nr.guid][0] = std::numeric_limits<float>::max();
            }
            else if (nr.distance > iter->second[1])
            {
                iter->second[1] = nr.distance;
            }

            nearestRidgesToPoints[i] = std::move(nr);
        }

        for (int i = 0; i < clusterPolygon.getPts().size(); ++i)
        {
            const NearestRidge nr = getNearestRidge(clusterPolygon[i], smoothedRidgesCurvesMap);
            auto iter = minmaxDistancesToRidge.find(nr.guid);
            Q_ASSERT(iter != minmaxDistancesToRidge.end());
            if (nr.distance < iter->second[0])
                iter->second[0] = nr.distance;
        }

        // calculate interpolated height for vertices -----------------------------------------------------------------------------------
        std::vector<QVector3D> vertices;
        vertices.reserve(vert.size());
        for (int i = 0; i < vert.size(); ++i)
        {
            const GVector2D& pt = vert[i];
            const NearestRidge& nr = nearestRidgesToPoints[i];

            Q_ASSERT(nr.guid != 0);
            if (nr.guid == 0)
            {
                vertices <<= QVector3D(pt.x, dem->heightData.sample(pt), pt.z);
                spawn<DLineMarker>(vertices.back(), 10000.f, Colors::cyan);
                continue;
            }

            const std::vector<QVector3D>& ridgePoints = smoothedRidgesCurvesMap[nr.guid];
            const float maxDistanceToRidge = (minmaxDistancesToRidge[nr.guid][0] + minmaxDistancesToRidge[nr.guid][1]) * 0.5f;

            const GVector2D ridgePt1 = (GVector2D)ridgePoints[nr.index];
            const GVector2D ridgePt2 = (GVector2D)ridgePoints[nr.index + 1];

            const float demHeight1 = dem->heightData.sample(ridgePt1);
            const float demHeight2 = dem->heightData.sample(ridgePt2);
            const float ridgeOriginalHeight1 = std::clamp(ridgePoints[nr.index].y(),     demHeight1 + ridgeParams.minHeightLimit, demHeight1 + ridgeParams.maxHeightLimit);
            const float ridgeOriginalHeight2 = std::clamp(ridgePoints[nr.index + 1].y(), demHeight2 + ridgeParams.minHeightLimit, demHeight2 + ridgeParams.maxHeightLimit);
            const float peakFactor = std::max(ridgeOriginalHeight1 - minH, 0.f) / (maxH - minH);
            const float noiseAmplitude = std::max(ridgeOriginalHeight1 - demHeight1, 0.f) * ridgeMetaCluster.noiseAmplitudeFactor * peakFactor;

            const float ridgeHeight1 = std::max((float)(ridgeOriginalHeight1 + ridgeMetaCluster.getPeakNoiseValue(ridgePt1) * noiseAmplitude), demHeight1);
            const float ridgeHeight2 = std::max((float)(ridgeOriginalHeight2 + ridgeMetaCluster.getPeakNoiseValue(ridgePt2) * noiseAmplitude), demHeight2);

            const float d1 = (nr.projection - ridgePt1).length();
            const float d2 = (ridgePt2 - ridgePt1).length();
            const float ridgeHeight = std::lerp(ridgeHeight1, ridgeHeight2, d1 / d2);

            const float demHeight = dem->heightData.sample(pt);
            const float currentWidthOffset = widthFactor * (1.f - 0.5f * widthDispersion * (noiseWidthModel.GetValue(ridgePt1.x, ridgePt1.z) + 1.f));
            const float t = std::max(1.f - nr.distance / maxDistanceToRidge, 0.f);
            const float tt = (float)ridgeMetaCluster.interpolationFunc(currentWidthOffset, currentWidthOffset * verticalOffsetFactor, t);
            const float currHeight = std::lerp(demHeight, ridgeHeight, tt);

            vertices <<= QVector3D(pt.x, currHeight, pt.z);
        }

        GeometryData<TerrainMeshVertex> geometry;
        geometry.indices = std::move(indices);
        geometry.vertices.reserve(vertices.size());
        for (const auto& pt : vertices)
        {
            TerrainMeshVertex tmv{ pt, {}, *this };
            geometry.vertices <<= std::move(tmv);
        }

        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    ClusterData<ETerrainBlock::Ridge>::ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Ridge>* metaCluster, int id)
        : ClusterDataBase(id)
    {
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Ridge>::initialize()
    {
        TerrainBlockMetaClusterBase::initialize();
        initRidge();
        initUtils();
    }

    double TerrainBlockMetaCluster<ETerrainBlock::Ridge>::getPeakNoiseValue(const GVector2D& p) const
    {
        return noiseModel.GetValue(p.x, p.z);
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Ridge>::initRidge()
    {
        auto&& allCells = Data::get()->getTerrainCells()->getCells();
        const auto gpt = allCells[*cells.begin()]->getCenter().toGPoint();

        auto&& domain = Generation::Data::get()->getDomainAtSquare(gpt, EDomainType::Terrain);
        auto&& terrainData = domain->getData<EDomainType::Terrain>();

        if (terrainData->landform == ELandform::Hills)
        {
            ridgeParameters.interpolationFunctions = { &linear, &linear, &square, &square };
            ridgeParameters.minWidthFactor = 0.2f;
            ridgeParameters.maxWidthFactor = 0.3f;
            ridgeParameters.minWidthDispersion = 0.2f;
            ridgeParameters.maxWidthDispersion = 0.4f;
            ridgeParameters.minVerticalOffsetFactor = (terrainData->hillsSmoothness + 0.1f) * 0.01f * 0.05f;
            ridgeParameters.maxVerticalOffsetFactor = (terrainData->hillsSmoothness + 0.1f) * 0.01f * 0.1f;
            ridgeParameters.minHeightLimit = 50.f;
            ridgeParameters.maxHeightLimit = 250.f;
        }
        else if (terrainData->landform == ELandform::Plains || terrainData->landform == ELandform::RuggedPlains)
        {
            ridgeParameters.minWidthFactor = 0.3f;
            ridgeParameters.maxWidthFactor = 0.5f;
            ridgeParameters.minWidthDispersion = 0.6f;
            ridgeParameters.maxWidthDispersion = 0.8f;
            ridgeParameters.minVerticalOffsetFactor = 0.05f;
            ridgeParameters.maxVerticalOffsetFactor = 0.15f;
            ridgeParameters.minHeightLimit = 40.f;
            ridgeParameters.maxHeightLimit = 300.f;
        }

    }

    void TerrainBlockMetaCluster<ETerrainBlock::Ridge>::initUtils()
    {
        noiseSource.SetSeed(gRandomEngine());
        noiseSource.SetFrequency(getRandomFloat(1.5e-4f, 2.5e-4f));
        noiseSource.SetOctaveCount(getRandomInt(4, 7));
        noiseModel.SetModule(noiseSource);
        noiseAmplitudeFactor = getRandomFloat(ridgeParameters.minNoiseAmplitudeFactor, ridgeParameters.maxNoiseAmplitudeFactor);

        const auto& func = ridgeParameters.interpolationFunctions[getRandomInt(0, 3)];
        interpolationFunc = [func](float w, float verticalOffset, float t)
        {
            const float x1 = 1.f - w;
            const float y1 = func(x1 + w) - func(w);
            const float y2 = 1.f - verticalOffset;
            const float factor = y2 / y1;

            float result = 0.f;

            if (t <= 1.f - w)
            {
                result = (func(t + w) - func(w)) * factor;
            }
            else
            {
                // circle interpolation
                const float yc = (y2 * y2 + x1 * x1 - 2.f * x1) / (2.f * y2 - 2.f);
                const float r = 1.f - yc;
                result = yc + sqrtf(std::max(r * r - (t - 1.f) * (t - 1.f), 0.f));
            }

            return result;
        };
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Ridge>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        // 100% rock slab, 30% vegetation
        packParams = compilePackParams({ 0.0f, 0.3f });
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
}

void omniSave(const Generation::TerrainBlockMetaCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.clusters.size();
	for (auto&& cluster : object.clusters)
		omniBin << cluster.staticCast<Generation::TerrainBlockCluster<Generation::ETerrainBlock::Ridge>>();

    omniBin << object.guid;
	omniBin << object.terrainTexPack;
	omniBin << object.biomeTexPack;
	omniBin << object.packParams;
}

void omniLoad(Generation::TerrainBlockMetaCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::in>& omniBin)
{
    size_t clustersNum;
    omniBin >> clustersNum; 
    object.clusters.reserve(clustersNum);
    for (size_t i = 0; i < clustersNum; ++i)
    {
    	QSharedPointer<Generation::TerrainBlockCluster<Generation::ETerrainBlock::Ridge>> cluster;
    	omniBin >> cluster;
    	object.clusters << cluster;
    	cluster->metaCluster = object.sharedFromThis();
    }

    omniBin >> object.guid;
    omniBin >> object.terrainTexPack;
    omniBin >> object.biomeTexPack;
    omniBin >> object.packParams;

    object.initUtils();
}
