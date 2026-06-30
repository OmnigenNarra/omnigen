#include "stdafx.h"
#include "DuneStar.h"
#include "../DuneGraph.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

namespace Generation
{
    DesertClusterSubData<EDesertBlockSubtype::DuneStar>::DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData)
        : DesertClusterSubDataBase(inBaseData)
    {

    }

    std::unordered_set<int> DesertClusterSubData<EDesertBlockSubtype::DuneStar>::customGrow(const std::unordered_set<int>& candidates)
    {
        return customGrowFilterIslands(candidates, &baseData->cells);
        // baseData->cells += candidates;
        // return candidates;
        // return customGrowWithCellsLayers(candidates, baseData->cells, layers, allLayersCells);
    }

    QSharedPointer<DesertSubClusterBase> DesertClusterSubData<EDesertBlockSubtype::DuneStar>::createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster)
    {
        return QSharedPointer<DesertSubCluster<EDesertBlockSubtype::DuneStar>>::create(cluster);
    }

    static float getRandomValueInRange(float a, float b)
    {
        std::uniform_real_distribution<> distr(a, b);
        return distr(Generation::gRandomEngine);
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneStar>::generate()
    {
        auto&& dem = Data::get()->getDEM();
        auto&& diagram = Data::get()->getTerrainCells();

        const Polygon2D clusterPolygon = Utils::makeBoundingPolygon(cluster->cells).front();
        const float polygonRadius = clusterPolygon.getRadius();
        const GVector2D polygonCenter = clusterPolygon.getCenter();
        const auto& coreCell = diagram->getCellAt(cluster->keyCell);
        const GVector2D coreCenter = coreCell.getVoronoiCenter();
        GVector2D windDirection = Data::get()->getWindVector(coreCenter);
        const GVector2D dir = -windDirection.normalized();

        GVector2D center = polygonCenter;
        if (!clusterPolygon.contains(polygonCenter))
            center = coreCenter;

        const float inscribedRadius = clusterPolygon.getRadiusOfInscribedCircleAtPoint(center);
        const float radius = std::lerp(inscribedRadius, polygonRadius, 0.42f);

        // Create bottom points -------------------------------------------------------------------------------------------------------

        // get polar angle coordinates of bottom points
        std::uniform_int_distribution<> distrTopCount(1, 2);
        const int topPointsCount = 1; // distrTopCount(Generation::gRandomEngine);
        std::uniform_int_distribution<> distrBottomCount(topPointsCount == 1 ? 5 : 3, topPointsCount == 1 ? 8 : 4);
        const int bottomPointsCount[2] = { distrBottomCount(Generation::gRandomEngine), distrBottomCount(Generation::gRandomEngine) };
        const int totalBottomPointsCount = bottomPointsCount[0] + (topPointsCount > 1 ? bottomPointsCount[1] : 0);

        std::vector<float> angles(totalBottomPointsCount);
        const float minimumGap = 15.f;
        const float angleRange = 360.f / totalBottomPointsCount - minimumGap;
        float fromLimit = minimumGap;
        float toLimit = minimumGap + angleRange;
        for (int i = 0; i < totalBottomPointsCount; ++i)
        {
            angles[i] = getRandomValueInRange(fromLimit, toLimit);
            fromLimit = toLimit;
            toLimit = std::min(toLimit + minimumGap + angleRange, 360.f);
        }

        // Create Dune Graph and fill it ----------------------------------------------------------------------------------------------
        DuneGraph duneGraph;
        duneGraph.bottomCurveTopPointFactor = getRandomValueInRange(0.15f, 0.3f);

        int angleCounter = 0;
        for (int i = 0; i < topPointsCount; ++i)
        {
            DuneNode node;
            std::vector<GVector2D> bpts;
            bpts.resize(bottomPointsCount[i]);
            for (int j = 0; j < bottomPointsCount[i]; ++j)
            {
                bpts[j] = center + GVector2D::rotateDegrees(dir, angles[angleCounter]) * radius;
                ++angleCounter;
            }
            if (topPointsCount > 1)
            {
                node.center = center + dir * (i == 0 ? 1.f : -1.f) * std::uniform_real_distribution<float>(0.1f, 0.3f)(Generation::gRandomEngine) * radius;
            }
            else
            {
                node.center = center;
            }

            const float nodeRadius = (clusterPolygon.getRadiusOfInscribedCircleAtPoint(node.center) + clusterPolygon.getRadius()) * 0.5f;

            node.height = getDunePeakHeight(node.center, nodeRadius * 0.2f, nodeRadius * 0.35f);

            for (int j = 0; j < bottomPointsCount[i]; ++j)
            {
                const float currRadius = std::uniform_real_distribution<float>(0.9f, 2.f)(Generation::gRandomEngine) * nodeRadius;
                const GVector2D currDir = (bpts[j] - node.center).normalized();
                bpts[j] = node.center + currDir * currRadius;
            }

            duneGraph.addNode(node);
            for (const auto& pt: bpts)
            {
                DuneVertex dvertex;
                dvertex.point = pt;
                dvertex.height = dem->heightData.sample(pt) + get3dSandPoint((QVector3D)pt).y();
                duneGraph.addVertex(i, std::move(dvertex));
            }
        }

        if (topPointsCount > 1)
        {
            duneGraph.addConnection(0, 1,                                                                                         // nodes
                { 0, (int)duneGraph.getLastNodeVertex(0).address.vertexId },                                                      // from vertices
                { false, true },                                                                                                  // direction
                { (int)duneGraph.getLastNodeVertex(1).address.vertexId, (int)duneGraph.getFirstNodeVertex(1).address.vertexId }); // to vertices
        }

        const auto createInterpolationWrapper = [](EInterpolation01 iType) -> HeightFunction
        {
            const auto interpolation = Interpolation::getInterpolation01(iType, 0.5f);
            return [interpolation](float t){ return (float)interpolation->interpolate(t); };
        };

        const HeightFunction hfArray[3] = 
        {
            createInterpolationWrapper(EInterpolation01::Linear),
            createInterpolationWrapper(EInterpolation01::Power),
            createInterpolationWrapper(EInterpolation01::InversePower)
        };

        const std::vector<HeightFunction> heightFunctions = { hfArray[std::uniform_int_distribution(0, 2)(Generation::gRandomEngine)] };

        duneGraph.generateRidgesAndFaces(heightFunctions, heightFunctions, &getSandHeight);

        // duneGraph.debugDraw((int)DuneGraph::DebugDrawFlags::Curves | (int)DuneGraph::DebugDrawFlags::RidgeRPolygons);

        // Meshing --------------------------------------------------------------------------------------------------------------------
        MeshConnector meshConnector = duneGraph.meshClusterPolygon(clusterPolygon, &getSandHeight);
        cluster->fillResultMesh(meshConnector);
    }

    DesertSubCluster<EDesertBlockSubtype::DuneStar>::DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster)
        : DesertSubClusterBase(inCluster, EDesertBlockSubtype::DuneStar)
    {
    }
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneStar>& object, OmniBin<std::ios::out>& omniBin)
{

}

void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneStar>& object, OmniBin<std::ios::in>& omniBin)
{

}
