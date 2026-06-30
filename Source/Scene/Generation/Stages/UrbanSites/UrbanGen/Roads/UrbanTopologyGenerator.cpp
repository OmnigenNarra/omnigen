#include "stdafx.h"
#include "UrbanTopologyGenerator.h"

#include "RoadNetworkData.h"
#include "RoadVertexSearch.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/Stages/UrbanSites/RuralGen/RoadGenerator.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/UrbanSite.h"

UrbanTopologyGenerator::UrbanTopologyGenerator(const Generation::UrbanSite* inParent,
    std::vector<Segment2D> inPrimaryEdges)
{
    parent = inParent;

    roadData[Primary].push_back(QSharedPointer<RoadNetworkData>::create(true));

    for (auto&& e : inPrimaryEdges)
        Generation::Data::get()->createMarker<DLineMarker>(std::vector<QVector3D>{ e.first, e.second }, Colors::yellow, false, 1'000.f);

    std::vector<BaseEdgeInfo> edges;
    for (auto&& e : inPrimaryEdges)
        edges.push_back(BaseEdgeInfo(e));

    roadData[Primary][0]->baseEdges = std::move(edges);

    RoadFixedTracerConfig tracerConfig = {};
    tracerConfig.bounds = parent->getAreaPolygon();

    roadTracer = RoadFixedTracer(tracerConfig);
}

void UrbanTopologyGenerator::generate()
{
    cachedFacesNum = getFacesInArea() + 1;

    {
        OmniProfile("Primary Roads", true);

        roadData[Primary][0]->computeRoadGraphHigh();

        tracePrimaryRoads();

        roadData[Primary][0]->computeRoadGraphLow();
    }

    //for (auto&& line : parent->getEnvironmentalLines())
        //Generation::Data::get()->createMarker<DLineMarker>(line, Colors::yellow, false, 1'000.f);

    {
        OmniProfile("Secondary Roads", true);

        parent->generateDistrictVoronoi(computeDistrictBounds());

        traceSecondaryRoads();
    }

    {
        OmniProfile("Building Block/Lot Extraction", true);

        extractBuildingBlocks();
        extractBuildingLots();
    }

    {
        OmniProfile("Terrain Adjustment", true);

        adjustTerrain();
    }

    {
        OmniProfile("Road Painting", true);

        paintRoads();
    }
}

void UrbanTopologyGenerator::clear()
{
    //revertTerrainAdjustment();
    //roadPainter.revertRoadPainting();
}

size_t UrbanTopologyGenerator::getFacesInArea() const
{
    auto&& clustersMap = Generation::Data::get()->getTerrainClustersMap();

    std::unordered_set<int> processedClusters;

    size_t countToReturn = 0;

    for (auto&& id : parent->getAreaIds())
    {
        auto&& cluster = clustersMap[id];
        if (processedClusters.contains(cluster->keyCell))
            continue;

        processedClusters.insert(cluster->keyCell);

        countToReturn += (cluster->section->getIndexBufferSize() / 3);
    }

    return countToReturn;
}

void UrbanTopologyGenerator::tracePrimaryRoads()
{
    auto&& data = roadData[Primary][0]->roadTargets;

    for (auto&& seg : data)
    {
        const auto newRoad = (seg.length() <= roadTraceStep * 1.3f) ?
            traceSimpleRoad(seg, Primary) : traceComplexRoad(seg, parent->getAreaPolygon());

        roadData[Primary][0]->roads << newRoad;
    }

    /*static std::mutex insertGuard;

    tbb::parallel_for(0, int(data.size()), [&](int i) 
    {
        auto&& seg = data[i];
        const auto newRoad = (seg.length() <= roadTraceStep * 1.3f) ? 
            traceSimpleRoad(seg, Primary) : traceComplexRoad(seg, parent->getAreaPolygon());

        {
            std::scoped_lock lock(insertGuard);
            roadData[Primary][0]->roads << newRoad;
        }
    });*/
}

void UrbanTopologyGenerator::extractBuildingBlocks()
{
    for (auto&& networks = roadData[Secondary]; 
        auto&& data : networks)
    {
        const auto mcbData = UrbanUtils::getDataFromAdjacencyGraph(data->roadGraphLowLevel);

        const MCBComputer mcb(mcbData);

        std::vector<Polygon2D> shrunkLots;
        for (auto&& lot : mcb.getLots())
            shrunkLots << Polygon2D::inflatePolygonByScale(lot, 0.95f);

        buildingBlocks << shrunkLots;
    }
}

void UrbanTopologyGenerator::traceSecondaryRoads()
{
    for (auto&& district : parent->getDistrictNetwork().getDistricts())
    {
        bool shouldTraceComplex = district.getDominantRoadPattern() != ERoadPattern::Grid &&
            district.getDominantRoadPattern() != ERoadPattern::CircularGrid;

        auto newData = QSharedPointer<RoadNetworkData>::create(shouldTraceComplex);

        for (auto&& seg : district.getRoadEdges())
            newData->baseEdges.push_back(BaseEdgeInfo(seg));

        newData->bounds = district.getBounds();

        roadData[Secondary].push_back(newData);
    }

    auto&& networks = roadData[Secondary];
    for (auto i = 0; i < networks.size(); i++)
    {
        auto&& data = networks[i];
        data->computeRoadGraphHigh();

        for (auto&& seg : data->roadTargets)
        {
            if (data->shouldComplexTrace() && (seg.length() > roadTraceStep * 1.3f) && false)
            {
                data->roads << traceComplexRoad(seg, data->bounds);
            }
            else
            {
                data->roads << traceComplexRoadNoHeight(seg, Secondary);
            }
        }

        data->computeRoadGraphLow();

        UrbanUtils::removeGraphIntersections(&data->roadGraphLowLevel);
    }
}

std::vector<DistrictCreationInfo> UrbanTopologyGenerator::computeDistrictBounds()
{
    const auto districtLots = roadData[Primary][0]->getEnclosingRegions();

    std::vector<DistrictCreationInfo> districtInfo;

    for (auto i = 0; i < districtLots.size(); i++)
    {
        auto&& lot = districtLots[i];
        if (lot.getPts().empty())
            continue;

        float foundDist = std::numeric_limits<float>::max();
        auto foundIdx = -1;

        const auto& voronoiCenters = parent->getDistrictNetwork().getDiagram().getCenters();
        for (auto j = 0; j < voronoiCenters.size(); j++)
        {
            if (const auto dist = distanceSquared2D(lot.getCenter(), voronoiCenters[j]); dist < foundDist)
            {
                foundDist = dist;
                foundIdx = j;
            }
        }

        if (foundIdx == -1)
            continue;

        districtInfo.push_back(DistrictCreationInfo{ (IndexType)foundIdx,
            parent->getDistrictNetwork().getDiagram().getCellAt(foundIdx).getPolygon(), lot });
    }

    return districtInfo;
}

void UrbanTopologyGenerator::adjustTerrain()
{
    //TODO: Review if this needs outer neighbors
    for (auto&& road : roadData[Primary][0]->roads)
    {
        if (road->getTerrainVertices().empty())
            continue;

        for (const auto& [cl, v] : road->getTerrainVertices())
        {
            smoothTerrainVertex(cl, v);
        }
    }

    for (auto&& roads : roadData[Secondary])
        for (auto&& road : roads->roads)
        {
            if (road->getTerrainVertices().empty())
                continue;

            for (const auto& [cl, v] : road->getTerrainVertices())
            {
                smoothTerrainVertex(cl, v);
            }
        }
}

void UrbanTopologyGenerator::revertTerrainAdjustment()
{
    auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

    auto keys = terrainModifications | std::views::keys;
    const auto blocksAffected = std::vector<IndexType>{ keys.begin(), keys.end() };

    tbb::parallel_for(0, (int)blocksAffected.size(), [&](int blockKeyIdx)
        {
            const int clusterId = blocksAffected[blockKeyIdx];
            auto&& cluster = clusterMap[clusterId];
            auto vertices = cluster->section->getVertices();

            for (auto&& changedVertices = terrainModifications[clusterId]; auto&& data : changedVertices)
            {
                vertices[data.vertexId].position = data.originalPos;
                vertices[data.vertexId].displacementFactor = data.originalDisplacement;
            }
        });


    for (auto&& id : terrainModMarkers)
    {
        Generation::Data::get()->clearSingleExactMarker<DLineMarker>(id);
    }
}

void UrbanTopologyGenerator::extractBuildingLots()
{
    const LotExtractor newLotExtractor(buildingBlocks);

    //TODO: Handle enclosed lots to add green space etc
    // TODO: Get voronoi information to add mutliple lot sized buildings

    buildingLots = newLotExtractor.getLots();

    for (auto& [lot, districtBounds, isEnclosed, isDivided] : buildingLots)
    {
        lot.debugPlot(Colors::random(), 6'000.f);
    }
}

void UrbanTopologyGenerator::paintRoads()
{
    for (auto&& road : roadData.at(Primary)[0]->roads)
        roadPainter.paintRoad(road);

    for (auto&& road : roadData[Secondary])
        for (auto&& r : road->roads)
            roadPainter.paintRoad(r);
}

void UrbanTopologyGenerator::smoothTerrainVertex(const int cluster, const int idx)
{
    std::unordered_map<int, std::unordered_set<IndexType>> adjMap;

    auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

    auto getClustersContainingVertex = [&clusterMap](const GVector2D& v2D) -> std::vector<std::pair<IndexType, IndexType>>
    {
        std::vector<std::pair<IndexType, IndexType>> vecToReturn;

        std::vector<IndexType> clusterIds;

        auto&& clusterTree = Generation::Data::get()->getBlockQuadTree();
        for (auto&& nearbyBlocks = clusterTree->find_all_nearest(v2D.x, v2D.z, 200.f);
            auto && entry : nearbyBlocks)
        {
            clusterIds << entry->data;
        }

        for (auto&& id : clusterIds)
        {
            if (const auto* val = clusterMap[id]->getVertexQuadTree().find_nearest(v2D.x, v2D.z, 10.f); val)
            {
                vecToReturn << std::make_pair(id, val->data);
            }
        }

        return vecToReturn;
    };

    std::vector<float> heights;

    auto&& clusterPtr = clusterMap[cluster];
    auto vertices = clusterPtr->section->getVertices();
    const auto& v = vertices[idx].position;
    for (auto&& nearbyVertices = clusterPtr->getVertexQuadTree().find_all_nearest(v.x(), v.z(), 200.f);
        auto && entry : nearbyVertices)
    {
        heights << vertices[entry->data].position.y();
    }

    float sum = 0.f;
    for (auto&& h : heights)
        sum += h;

    const float height = sum / heights.size();
    for (auto&& [cl, vIdx] : getClustersContainingVertex(GVector2D(v.x(), v.z())))
    {
        if (adjMap[cl].contains(vIdx))
            continue;

        auto&& terrainV = vertices[vIdx];

        terrainV.position.setY(height);
        terrainV.displacementFactor = 0.0f;

        TerrainAdjustmentData adjData;
        adjData.vertexId = vIdx;
        adjData.originalDisplacement = terrainV.displacementFactor;
        adjData.originalPos = terrainV.position;
        adjData.adjustedPos = QVector3D(terrainV.position.x(), height, terrainV.position.z());
        adjData.adjustedDisplacement = 0.f;

        terrainModifications[cl] << adjData;

        adjMap[cl].insert(vIdx);
    }
}

QSharedPointer<DRoadMarker> UrbanTopologyGenerator::traceSimpleRoad(const Segment2D& initialSegment, const ERoadType roadType)
{
    //TODO: Snap to main roads? Snap as post process?
    return Generation::Data::get()->createMarker<DRoadMarker>(
        std::vector<QVector3D>{ UrbanUtils::heightQuery(initialSegment.first),
        UrbanUtils::heightQuery(initialSegment.second) }, initialSegment);
}

QSharedPointer<DRoadMarker> UrbanTopologyGenerator::traceComplexRoadNoHeight(const Segment2D& initialSegment, const ERoadType roadType) const
{
    const float increment = roadTracer.config.stepSize;
    const GVector2D dir = GVector2D(initialSegment.second - initialSegment.first).normalized();

    if (initialSegment.length() <= increment)
        return Generation::Data::get()->createMarker<DRoadMarker>(
            std::vector{ UrbanUtils::heightQuery(initialSegment.first), UrbanUtils::heightQuery(initialSegment.second) }, initialSegment);

    std::vector<QVector3D> controlPoints{ UrbanUtils::heightQuery(initialSegment.first) };
    Segment2D seg = initialSegment;

    while (seg.length() > increment * 1.2f)
    {
        const auto newPt = seg.first + (dir * increment);
        controlPoints << UrbanUtils::heightQuery(newPt);

        seg = Segment2D(newPt, seg.second);
    }

    controlPoints << UrbanUtils::heightQuery(initialSegment.second);

    return Generation::Data::get()->createMarker<DRoadMarker>(controlPoints, initialSegment);
}

QSharedPointer<DRoadMarker> UrbanTopologyGenerator::traceComplexRoad(const Segment2D& initialSegment, const Polygon2D& bounds) const
{
    const auto result = roadTracer.traceRoad(initialSegment.first, initialSegment.second, bounds);
    return Generation::Data::get()->createMarker<DRoadMarker>(result.path, initialSegment);

    /*std::vector<QVector3D> verticesToRemove;

    for (auto&& pt : result.maxSlopeVertices)
    {
        spawn<DLineMarker>(result.path[pt.first], 5'000.f, Colors::red);
        spawn<DLineMarker>(result.path[pt.second], 5'000.f, Colors::red);

        for (auto i = pt.first; i < pt.second + 1; i++)
        {
            verticesToRemove.push_back(result.path[i]);
        }

        auto terrainPath = traceTerrainBasedRoad(Segment2D(result.path[pt.first], result.path[pt.second]));
        if (terrainPath.empty())
            continue;

        result.path.insert(result.path.begin() + pt.second, terrainPath.cbegin(), terrainPath.cend());
    }

    std::forward_list<QVector3D> editedPath{ result.path.cbegin(), result.path.cend() };
    for (auto&& v : verticesToRemove)
        editedPath.remove(v);

    result.path = std::vector<QVector3D>{ editedPath.begin(), editedPath.end() };

    for (auto&& pt : result.envBoundVertices)
    {
        spawn<DLineMarker>(result.path[pt.first], 5'000.f, Colors::azure);
        spawn<DLineMarker>(result.path[pt.second], 5'000.f, Colors::azure);
    }

    return Generation::Data::get()->createMarker<DRoadMarker>(result.path, initialSegment);*/
}

std::vector<QVector3D> UrbanTopologyGenerator::traceTerrainBasedRoad(const Segment2D& initialSegment) const
{
    auto exec = RoadVertexSearchExecutor(cachedFacesNum);

    exec.setStartAndGoal(initialSegment.first, initialSegment.second,
        this);
    const bool execSuccess = exec.execute();

    std::vector<QVector3D> controlPoints = exec.getPath();
    if (controlPoints.empty())
        return {};

    controlPoints.insert(controlPoints.begin(), UrbanUtils::heightQuery(initialSegment.first));
    controlPoints.push_back(UrbanUtils::heightQuery(initialSegment.second));

    return controlPoints;
}

std::optional<GVector2D> UrbanTopologyGenerator::getNextBestSeed(const GVector2D& startingPoint,
                                                                 const GVector2D& endPoint, const GVector2D& offsetV, const float targetElevation) const
{
    std::vector<GVector2D> potentialPoints;

    constexpr float heightSearchDeviation = 20.f;
    const float decrement = (heightSearchDeviation * 2.f) / roadTraceDensity;

    const GVector2D midPoint = startingPoint + offsetV;
    potentialPoints.push_back(midPoint);

    const GVector2D lineVec = { midPoint - startingPoint };

    float currentDegrees = heightSearchDeviation;
    for (int i = 0; i < roadTraceDensity - 1; i++)
    {
        const auto rotatedVector = GVector2D::rotateDegrees(lineVec.normalized(), currentDegrees);
        const GVector2D newOffset = rotatedVector * offsetV.length();

        const GVector2D newPoint = startingPoint + newOffset;

        if (parent->getAreaPolygon().contains(newPoint))
            potentialPoints.push_back(newPoint);

        currentDegrees -= decrement;

        if (currentDegrees < -20)
            break;
    }

    if (potentialPoints.empty())
        return {};

    // Get point elevation data
    QHash<int, float> elevationMap;
    for (auto i = 0; i < potentialPoints.size(); i++)
    {
        // Calculate desired elevation
        const float elevation = qAbs(UrbanUtils::heightQuery((potentialPoints[i])).y() / offsetV.length() - targetElevation / (endPoint - midPoint).length());
        elevationMap.insert(i, elevation);
    }

    float minElevation = std::numeric_limits<float>::max();
    for (auto& data : elevationMap)
    {
        minElevation = std::min(minElevation, data);
    }

    const int idxToReturn = elevationMap.key(minElevation);
    return potentialPoints[idxToReturn];
}
