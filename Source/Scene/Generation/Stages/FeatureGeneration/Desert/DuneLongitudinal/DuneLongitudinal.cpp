#include "stdafx.h"
#include "DuneLongitudinal.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "../DuneGraph.h"


static std::vector<GVector2D> getPointsBetweenCells(int cell1, int cell2)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();

    const auto& cellA = diagram->getCellAt(cell1);
    const auto& cellB = diagram->getCellAt(cell2);
    const auto& ptsA = cellA.getPolygon().getPts();
    const auto& ptsB = cellB.getPolygon().getPts();

    std::unordered_set<GVector2D> bpoints(ptsA.begin(), ptsA.end());
    bpoints.reserve(ptsA.size());

    std::vector<GVector2D> resultPoints;
    resultPoints.reserve(3);

    for (const auto& pt : ptsB)
    {
        if (bpoints.contains(pt))
            resultPoints << pt;
    }

    return resultPoints;
}

namespace Generation
{
    DesertClusterSubData<EDesertBlockSubtype::DuneLongitudinal>::DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData)
        : DesertClusterSubDataBase(inBaseData)
        , cellGraph({inBaseData->centerCell})
    {
        const auto& cell = Data::get()->getTerrainCells()->getCellAt(inBaseData->centerCell);

        baseWindDirection = Data::get()->getWindVector(cell->getCenter()).normalized().rotatedRight90();
        if (baseWindDirection.isNull())
        {
            CellElevationData ced = Data::get()->getDEM()->getCellElevationData(*cell);
            baseWindDirection = ced.gradient.normalized().rotatedRight90();
        }
    }

    std::unordered_set<int> DesertClusterSubData<EDesertBlockSubtype::DuneLongitudinal>::customGrow(const std::unordered_set<int>& candidates)
    {
        if (candidates.empty())
            return {};

        auto&& diagram = Data::get()->getTerrainCells();
        std::unordered_set<int> newCells;

        if (baseData->cells.empty())
        {
            const int newCellId = *candidates.begin();
            newCells += newCellId;
            cellGraph << newCellId;
            baseData->cells += newCells;

            std::unordered_set<int> newCandidates = candidates;
            newCandidates.erase(newCellId);
            return newCells + customGrow(newCandidates);
        }

        const int chainEndId = cellGraph.back();
        const auto& chainEndCell = diagram->getCellAt(chainEndId);
        const auto& endCellNeighbors = chainEndCell.getNeighbors();
        const GVector2D& endCellCenter = chainEndCell->getCenter();
        const GVector2D windDir = Data::get()->getWindVector(endCellCenter).normalized();

        constexpr float maxTurnAngle = 150.f;
        float minAngle = 360.f;
        int bestCandidateId = -1;

        for (int nid : candidates)
        {
            if (!endCellNeighbors.contains(nid))
                continue;

            const auto& cell = diagram->getCellAt(nid);
            const GVector2D dir = (cell->getCenter() - endCellCenter).normalized();
            const float angleDelta = fabsf(angle180(windDir, dir));

            if (angleDelta < minAngle)
            {
                minAngle = angleDelta;
                bestCandidateId = nid;
            }
        }

        if (bestCandidateId == -1 || minAngle > maxTurnAngle)
            return {};

        newCells += bestCandidateId;

        newCells = customGrowFilterIslands(newCells, &baseData->cells);
        if (newCells.empty())
            return {};

        cellGraph << bestCandidateId;
        baseData->cells += newCells;

        std::unordered_set<int> newCandidates = candidates;
        newCandidates.erase(bestCandidateId);
        return newCells + customGrow(newCandidates);
    }

    QSharedPointer<DesertSubClusterBase> DesertClusterSubData<EDesertBlockSubtype::DuneLongitudinal>::createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster)
    {
        Q_ASSERT(!cellGraph.empty());
        return QSharedPointer<DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>>::create(cluster, cellGraph);
    }

    DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>::DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster, std::vector<int> inCellGraph)
        : DesertSubClusterBase(inCluster, EDesertBlockSubtype::DuneLongitudinal)
        , cellGraph(std::move(inCellGraph))
    {
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>::straightenTheCellGraphImpl()
    {
        std::unordered_map<int, QSet<int>> searchGraph;
        searchGraph.reserve(cluster->cells.size());
        QSet<int> availableCells = convertStlToQSet(cluster->cells);

        auto&& diagram = Data::get()->getTerrainCells();

        searchGraph[0] = { cellGraph.front() };
        availableCells -= cellGraph.front();
        int counter = 1;
        while (true)
        {
            searchGraph[counter] = QSet<int>();
            for (int cellId: searchGraph[counter - 1])
            {
                const auto& currCell = diagram->getCellAt(cellId);
                searchGraph[counter] += convertStlToQSet(currCell.getNeighborsSet());
            }

            searchGraph[counter].intersect(availableCells);
            availableCells -= searchGraph[counter];

            if (availableCells.empty())
                break;

            ++counter;
        }

        // new cell graph
        cellGraph.clear();
        cellGraph << *searchGraph[counter].rbegin();

        for (int i = counter; i >= 0; --i)
        {
            const int cellId = cellGraph.back();
            const auto& currCell = diagram->getCellAt(cellId);
            for (int nid: currCell.getNeighborsSet())
            {
                if (searchGraph[i].contains(nid))
                {
                    cellGraph << nid;
                    break;
                }
            }
        }
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>::removeSharpAngles()
    {
        if (cellGraph.size() < 3)
            return;

        constexpr float turnLimit = 90.f; //deg
        auto&& diagram = Data::get()->getTerrainCells();
        for (int i = 1; i < cellGraph.size() - 1; ++i)
        {
            const auto& prevCell = diagram->getCellAt(cellGraph[i - 1]);
            const auto& currCell = diagram->getCellAt(cellGraph[i]);
            const auto& nextCell = diagram->getCellAt(cellGraph[i + 1]);
            const GVector2D& prevPt = prevCell.getVoronoiCenter();
            const GVector2D& currPt = currCell.getVoronoiCenter();
            const GVector2D& nextPt = nextCell.getVoronoiCenter();
            const GVector2D dir1 = (currPt - prevPt).normalized();
            const GVector2D dir2 = (nextPt - currPt).normalized();
            const float angle = angle180(dir1, dir2);
            if (fabsf(angle) > turnLimit)
            {
                const auto iter = cellGraph.begin() + (i == cellGraph.size() - 2 ? cellGraph.size() - 1 : i);
                cellGraph.erase(iter);
                return;
            }
        }

    }

    void DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>::straightenTheCellGraph()
    {
        if (cellGraph.size() < 2)
            return;

        // first iteration - from current start to the farthest cell
        straightenTheCellGraphImpl();
        // second iteration - find longest straight way in cluster (from farthest cell to opposite side of cluster)
        straightenTheCellGraphImpl();

        removeSharpAngles();
    }

    DuneGraph DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>::generateDuneShape() const
    {
        auto&& diagram = Data::get()->getTerrainCells();
        constexpr float gapFactor = 0.9f;
        constexpr float gapFactorIntermediate = 0.7f;

        std::vector<GVector2D> centersVec;
        std::vector<float> radiusesVec;
        centersVec.reserve(cluster->cells.size() * 2 - 1);
        radiusesVec.reserve(cluster->cells.size() * 2 - 1);

        DuneGraph duneGraph;

        if (cellGraph.size() == 1)
        {
            const auto& cell = diagram->getCellAt(cellGraph.front());
            const Polygon2D& cellPolygon = cell.getPolygon();
            const GVector2D center = cellPolygon.getCenter();
            constexpr float searchRadiusFactor = 0.75f;
            const float radius = cellPolygon.getRadiusOfInscribedCircleAtPoint(center) * searchRadiusFactor;

            const GVector2D windDir = Data::get()->getWindVector(center).normalized();
            const GVector2D windDirL = windDir.rotatedLeft90();
            const GVector2D windDirR = windDir.rotatedRight90();

            const GVector2D center1 = center + windDir * radius;
            const GVector2D center2 = center - windDir * radius;
            const float enlargeFactor = std::uniform_real_distribution<float>(2.f, 3.5f)(Generation::gRandomEngine);
            const float radius1 = cellPolygon.getRadiusOfInscribedCircleAtPoint(center1) * enlargeFactor;
            const float radius2 = cellPolygon.getRadiusOfInscribedCircleAtPoint(center2) * enlargeFactor;

            const GVector2D edgePt1 = center1 + windDir * radius1;
            const GVector2D edgePt2 = center2 - windDir * radius2;

            centersVec << center1 << center2;
            radiusesVec << radius1 << radius2;

            // Fill dune graph
            duneGraph.addNode(DuneNode(center1, getDunePeakHeight(center1, radius1 * 0.25f, radius1 * 0.42f)));
            duneGraph.addNode(DuneNode(center2, getDunePeakHeight(center2, radius2 * 0.25f, radius2 * 0.42f)));

            duneGraph.addVertex(0, createDuneVertexAtPoint(edgePt1));
            duneGraph.addVertex(0, createDuneVertexAtPoint(center1 + windDirL * radius1));
            duneGraph.addVertex(0, createDuneVertexAtPoint(center1 + windDirR * radius1));

            duneGraph.addVertex(1, createDuneVertexAtPoint(center2 + windDirL * radius2));
            duneGraph.addVertex(1, createDuneVertexAtPoint(edgePt2));
            duneGraph.addVertex(1, createDuneVertexAtPoint(center2 + windDirR * radius2));

            duneGraph.addConnection(0, 1,  // nodes
                { 1, 2 },                  // from vertices
                { true, false },           // direction
                { 3, 5 });                 // to vertices

        }
        else
        {
            const auto& firstCell = diagram->getCellAt(cellGraph.front());
            const GVector2D& firstCenter = firstCell.getPolygon().getCenter();
            centersVec << firstCenter;

            std::vector<GVector2D> dirVec;
            dirVec.reserve(cellGraph.size());

            for (int i = 1; i < cellGraph.size(); ++i)
            {
                const auto& currCell = diagram->getCellAt(cellGraph[i]);
                const GVector2D& currCenter = currCell.getPolygon().getCenter();
                const GVector2D currDir = currCenter - centersVec.back();
                centersVec << currCenter;

                const float dist = currDir.length();
                dirVec << currDir * (1.f / dist);

                if (i == 1)
                    radiusesVec << dist;

                radiusesVec << dist;
            }

            std::vector<GVector2D> leftVertices;
            std::vector<GVector2D> rightVertices;
            leftVertices.reserve(cellGraph.size() + 2);
            rightVertices.reserve(cellGraph.size());

            const GVector2D& firstDir = dirVec.front();
            leftVertices  << centersVec.front() + firstDir.rotatedLeft90()  * radiusesVec.front();
            rightVertices << centersVec.front() + firstDir.rotatedRight90() * radiusesVec.front();

            for (int i = 1; i < cellGraph.size() - 1; ++i)
            {
                const GVector2D& dir1 = dirVec[i - 1];
                const GVector2D& dir2 = dirVec[i];
                GVector2D sumVec = (-dir1 + dir2);
                GVector2D leftVec;
                GVector2D rightVec;
                const GVector2D rotatedLeftDir = dir1.rotatedLeft90();
                if (sumVec.isNull())
                {
                    leftVec = rotatedLeftDir;
                    rightVec = dir1.rotatedRight90();
                }
                else
                {
                    sumVec.normalize();
                    const bool isLeftVec = GVector2D::dotProduct(sumVec, rotatedLeftDir) > 0;
                    leftVec  = isLeftVec ?  sumVec : -sumVec;
                    rightVec = isLeftVec ? -sumVec :  sumVec;
                }

                leftVertices  << centersVec[i] + leftVec  * radiusesVec[i];
                rightVertices << centersVec[i] + rightVec * radiusesVec[i];
            }

            const GVector2D& lastDir = dirVec.back();
            leftVertices  << centersVec.back() + lastDir.rotatedLeft90()  * radiusesVec.back();
            rightVertices << centersVec.back() + lastDir.rotatedRight90() * radiusesVec.back();


            int prevLastIndexL = 0;
            int prevLastIndexR = 0;

            for (int i = 0; i < cellGraph.size(); ++i)
            {
                const float currRadius = radiusesVec[i];
                duneGraph.addNode(DuneNode(centersVec[i], getDunePeakHeight(centersVec[i], currRadius * 0.2f, currRadius * 0.42f)));
            }

            for (int i = 0; i < cellGraph.size(); ++i)
            {
                if (i == 0)
                {
                    const GVector2D edgePoint = centersVec.front() - firstDir * radiusesVec.front();
                    duneGraph.addVertex(0, createDuneVertexAtPoint(edgePoint));
                }

                const int currIndexL = duneGraph.addVertex(i, createDuneVertexAtPoint(leftVertices[i]));
                if (i == cellGraph.size() - 1)
                {
                    const GVector2D edgePoint = centersVec.back() + lastDir * radiusesVec.back();
                    duneGraph.addVertex(i, createDuneVertexAtPoint(edgePoint));
                }
                const int currIndexR = duneGraph.addVertex(i, createDuneVertexAtPoint(rightVertices[i]));

                if (i > 0)
                {
                    // connection between nodes
                    duneGraph.addConnection(i - 1, i,          // nodes
                        { prevLastIndexL, prevLastIndexR },    // from vertices
                        { true, false },                       // direction
                        { currIndexL, currIndexR });           // to vertices
                }

                prevLastIndexL = currIndexL;
                prevLastIndexR = currIndexR;
            }
        }

        const auto createInterpolationWrapper = [](EInterpolation01 iType) -> HeightFunction
        {
            const auto interpolation = Interpolation::getInterpolation01(iType, 0.5f);
            return [interpolation](float t){ return (float)interpolation->interpolate(t); };
        };

        const std::vector<HeightFunction> heightFunctions = 
        {
            createInterpolationWrapper(EInterpolation01::Linear),
            createInterpolationWrapper(EInterpolation01::Power),
            createInterpolationWrapper(EInterpolation01::InversePower)
        };

        duneGraph.generateRidgesAndFaces(heightFunctions, heightFunctions, &getSandHeight);

        // Debug draw ============================================================================================================================================
        // duneGraph.debugDraw((int)DuneGraph::DebugDrawFlags::Curves);
        // duneGraph.debugDraw((int)DuneGraph::DebugDrawFlags::ConnectionsScheme);
        // constexpr float plotHeight = 10.f;
        // for (int i = 0; i < centersVec.size(); ++i)
        //     spawn<DCircleMarker>(centersVec[i], radiusesVec[i],i == 0 ? Colors::orange : Colors::green, plotHeight);
        // =======================================================================================================================================================

        return duneGraph;
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal>::generate()
    {
        auto&& diagram = Data::get()->getTerrainCells();

        // debug draw cells =====================================
        // for (int cellIdx : cluster->cells)
        // {
        //     auto&& cell = diagram->getCells()[cellIdx];

        //     const Polygon2D& cellPolygon = cell.getPolygon();
        //     cellPolygon.debugPlot(Colors::gray, -1.f);
        // }
        // ======================================================

        straightenTheCellGraph();

        const Polygon2D clusterPolygon = Utils::makeBoundingPolygon(cluster->cells).front();
        DuneGraph duneGraph = generateDuneShape();

        // duneGraph.debugDraw((int)DuneGraph::DebugDrawFlags::ConnectionsScheme);

        // Meshing --------------------------------------------------------------------------------------------------------------------
        MeshConnector meshConnector = duneGraph.meshClusterPolygon(clusterPolygon, &getSandHeight);
        cluster->fillResultMesh(meshConnector);
    }
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneLongitudinal>& object, OmniBin<std::ios::out>& omniBin)
{

}

void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneLongitudinal>& object, OmniBin<std::ios::in>& omniBin)
{

}
