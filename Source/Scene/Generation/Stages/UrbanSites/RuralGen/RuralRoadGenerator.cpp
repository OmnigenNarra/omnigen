#include "stdafx.h"
#include "RuralRoadGenerator.h"

#include "RoadTerrainSearch.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/Roads/RoadMarker.h"

void RuralRoadGenerator::generateRuralRoadPaths()
{
    auto&& sites = Generation::Data::get()->getUrbanSites();
    if (sites.size() < 2)
    {
        OmniLog(ELoggingLevel::Info) <<= "Not enough urban sites for rural roads.";
        return;
    }

    std::unordered_set<qint64> processedSites;
    for (auto&& site : sites)
    {
        for (auto&& site2 : sites)
        {
            if (site->getGuid() == site2->getGuid())
                continue;

            if (processedSites.contains(site2->getGuid()))
                continue;

            connections << getSiteConnectionPoints(site, site2);
            processedSites.insert(site->getGuid());
        }
    }

    for (auto&& connInfo : connections)
    {
        auto path = RuralPathFinder(connInfo);
        path.compute();

        internalRoads << Generation::Data::get()->createMarker<DRoadMarker>(path.getFinalPath(), 
            Segment2D(connInfo.start, connInfo.end));
    }
}

TargetConnectionInfo::TargetConnectionInfo(const GVector2D& inStart, const GVector2D& inEnd)
{
    start = UrbanUtils::heightQuery(inStart);
    end = UrbanUtils::heightQuery(inEnd);
}

RuralPathFinder::RuralPathFinder(TargetConnectionInfo inInfo)
    : connInfo(std::move(inInfo))
{
    roadTracer = RoadFixedTracer({});
}

bool RuralPathFinder::compute()
{
    OmniProfile("Rural Road Computation");
    if (!computeRoadPath())
        return false;

    return true;
}

std::vector<QVector3D> RuralPathFinder::computeTargetPath() const
{
    auto path = TerrainSearchExecutor();
    path.setStartAndGoal(connInfo.start, connInfo.end);

    path.execute();

    //Generation::Data::get()->createMarker<DLineMarker>(path.getPath(), Colors::green);

    return path.getPath();
}

bool RuralPathFinder::computeRoadPath()
{
    const auto seeds = computeTargetPath();
    if (seeds.size() < 2)
        return false;

    for (auto i = 0; i < seeds.size() - 1; i++)
    {
        auto result = roadTracer.traceRoad(seeds[i], seeds[i + 1], {});
        finalPath << adjustSlopesOverMax(result);
    }

    return true;
}

std::vector<QVector3D> RuralPathFinder::adjustSlopesOverMax(const RoadFixedTracerResult& inRoadData)
{
    std::vector<QVector3D> path = inRoadData.path;

    std::unordered_map<int, std::pair<int, std::vector<QVector3D>>> changesMap;

    auto avgHeightDelta = [](const std::vector<QVector3D>& pts) -> float
    {
        const int size = pts.size();
        float sum = 0.f;

        for (auto i = 0; i < pts.size() - 1; i++)
        {
            sum += abs(pts[i].y() - pts[i + 1].y());
        }

        return sum / size;
    };

    std::unordered_set<int> vsToSkip;
    for (auto i = 0; i < inRoadData.maxSlopeVertices.size(); i++)
    {
        auto&& [id1, id2] = inRoadData.maxSlopeVertices[i];

        const auto leftDir = GVector2D((path[id2] - path[id1]).normalized()).rotatedLeft90();
        const auto rightDir = GVector2D((path[id2] - path[id1]).normalized()).rotatedRight90();

        const auto leftSeg = Segment2D(path[id1], leftDir * roadTracer.config.stepSize);
        const auto rightSeg = Segment2D(path[id1], rightDir * roadTracer.config.stepSize);

        auto leftSeeds = getSeedsAroundSlope(leftSeg, path[id2], inRoadData);
        auto rightSeeds = getSeedsAroundSlope(rightSeg, path[id2], inRoadData);

        if (leftSeeds.empty() && rightSeeds.empty())
            continue;

        auto&& vecToUse = avgHeightDelta(leftSeeds) < avgHeightDelta(rightSeeds) ? leftSeeds : rightSeeds;
        for (auto j = id1; j < id2 + 1; j++)
        {
            vsToSkip.insert(j);
        }
        changesMap[id1] = std::make_pair(i, vecToUse);
    }

    //Debug drawing
    /*for (auto& vec : changesMap | std::views::values)
    {
        Generation::Data::get()->createMarker<DLineMarker>(vec.second, Colors::orange, false, 4'000.f);
    }*/

    std::vector<QVector3D> processedPath;
    for (auto i = 0; i < path.size(); i++)
    {
        if (changesMap.contains(i))
        {
            for (auto&& vec : changesMap[i].second)
                processedPath << vec;
        }

        if (vsToSkip.contains(i))
            continue;

        processedPath << path[i];
    }

    return processedPath;
}

std::vector<QVector3D> RuralPathFinder::getSeedsAroundSlope(const Segment2D& initialSeg,
    const QVector3D& targetVertex, const RoadFixedTracerResult& inRoadData)
{
    std::vector verticesToReturn = { UrbanUtils::heightQuery(initialSeg.first, UrbanUtils::getPointHeightAverage(inRoadData.path))  };

    auto findBestVertex = [&](const bool useTargetForSlope) -> std::optional<QVector3D>
    {
        const auto dir = verticesToReturn.size() == 1 ? ((QVector3D)initialSeg.second - verticesToReturn[0]).normalized() :
            (verticesToReturn.back() - *(verticesToReturn.end() - 2)).normalized();
        const auto newSeeds = roadTracer.queryNearbyPoints(verticesToReturn.back(), inRoadData.path, dir);

        std::vector<int> acceptableSeeds;
        for (auto i = 0; i < newSeeds.size(); i++)
        {
            if (!useTargetForSlope)
            {
                if (auto&& seed = newSeeds[i]; roadTracer.isWithinMaxSlope(verticesToReturn.back(), seed))
                    acceptableSeeds << i;
            }
            else
            {
                if (auto&& seed = newSeeds[i]; roadTracer.isWithinMaxSlope(verticesToReturn.back(), targetVertex))
                {
                    if (distance(seed, targetVertex) < roadTracer.config.stepSize * 1.1f)
                        acceptableSeeds << i;
                }
            }
        }

        if (acceptableSeeds.empty())
            return {};

        float minDistance = std::numeric_limits<float>::max();
        int closestSeedId = -1;
        for (auto&& id : acceptableSeeds)
        {
            if (const auto d = distance(newSeeds[id], targetVertex); d < minDistance)
            {
                minDistance = d;
                closestSeedId = id;
            }
        }

        return newSeeds[closestSeedId];
    };

    while (true)
    {
        const auto seed = findBestVertex(false);
        if (!seed)
            return {};

        verticesToReturn << *seed;

        if (distance((GVector2D)*seed, (GVector2D)targetVertex) < roadTracer.config.stepSize * 1.1f)
            break;
    }

    const auto seed = findBestVertex(true);
    if (!seed)
        return {};

    verticesToReturn << *seed;
    verticesToReturn << targetVertex;

    return verticesToReturn;
}

void RuralRoadGenerator::generate()
{
    generateRuralRoadPaths();

    return;
    {
        OmniProfile("Rural Roads -- Plotter Computation", true);
        computeRoadPlotters();
    }

    {
        OmniProfile("Rural Roads -- Road Painting", true);
        for (auto&& road : internalRoads)
            roadPainter.paintRoad(road);
    }
}

void RuralRoadGenerator::revertGen()
{
    internalRoads.clear();
    roadPainter.revertRoadPainting();
}

void RuralRoadGenerator::computeRoadPlotters()
{
    for (auto i = 0; i < internalRoads.size(); i++)
    {
        internalRoads[i]->setTerrainVertices(getRoadTerrainVertices(i));
    }
}

TargetConnectionInfo RuralRoadGenerator::getSiteConnectionPoints(const QSharedPointer<Generation::UrbanSite>& site1,
                                                                 const QSharedPointer<Generation::UrbanSite>& site2) const
{
    auto&& poly1 = site1->getAreaPolygon();
    auto&& poly2 = site2->getAreaPolygon();

    float dist = std::numeric_limits<float>::max();
    GVector2D pt1;
    GVector2D pt2;

    for (auto&& pt : poly1.getPts())
    {
        for (auto&& ptInner : poly2.getPts())
        {
            if (auto d = distanceSquared2D(pt, pt2); d < dist) 
            {
                dist = d;
                pt1 = pt;
                pt2 = ptInner;
            }
        }
    }

    return { UrbanUtils::heightQuery(pt1), UrbanUtils::heightQuery(pt2) };
}

std::vector<TerrainVertexData> RuralRoadGenerator::getRoadTerrainVertices(const int id) const
{
    std::vector<TerrainVertexData> dataToReturn;

    auto&& road = internalRoads[id];
    auto&& bounds = road->getBounds();

    auto&& blockTree = Generation::Data::get()->getBlockQuadTree();
    auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();
    auto&& clusters = Generation::Data::get()->getTerrainClustersMap();
    const float maxRadius = Generation::Data::get()->getLargestVoronoiCellRadius();

    std::unordered_map<int, std::unordered_set<int>> checkedVertices;

    auto getRelevantBlocks = [&blockTree, &cells](const QVector3D& v, const float radius) -> std::unordered_set<int>
    {
        auto&& findResultIds = blockTree->find_all_nearest(v.x(), v.z(), radius);

        float maxDistance = std::numeric_limits<float>::max();
        int closestBlockId = -1;
        for (auto&& entry : findResultIds)
        {
            if (auto d = distance(cells[entry->data]->getCenter(), (GVector2D)v);
                d < maxDistance)
            {
                closestBlockId = entry->data;
                maxDistance = d;
            }
        }

        if (closestBlockId == -1)
            return {};

        std::unordered_set<int> relevantBlockIds;
        relevantBlockIds.insert(closestBlockId);

        auto&& closestCell = cells[closestBlockId];
        for (auto&& key : closestCell.getNeighbors().keys())
        {
            relevantBlockIds.insert(key);
        }

        return relevantBlockIds;
    };

    auto shouldVertexBePainted = [&bounds, &road](const QVector3D& pos) -> bool
    {
        constexpr float maxProximityHeightDiff = 150.f;
        const auto closestSlPt = road->getClosestPoint(pos);

        if (bounds.contains(pos) && !(abs(closestSlPt.y() - pos.y()) <= maxProximityHeightDiff))
            Generation::Data::get()->createMarker<DLineMarker>((GVector2D)pos, 20'000.f, Colors::azure);

        return bounds.contains(pos) && (abs(closestSlPt.y() - pos.y()) <= maxProximityHeightDiff);
    };

    
    auto&& nodes = internalRoads[id]->getControlPoints();

    for (auto&& v : nodes)
    {
        float startRadius = 100.f;
        auto findResultIds = getRelevantBlocks(v, startRadius);
        while (findResultIds.empty())
        {
            startRadius *= 2.f;
            if (startRadius >= maxRadius)
                break;

            findResultIds = getRelevantBlocks(v, startRadius);
        }

        for (auto&& entry : findResultIds)
        {
            auto&& cluster = clusters[entry];
            auto vertices = cluster->section->getVertices();
            for (auto i = 0; i < vertices.size(); i++)
            {
                auto&& vertex = vertices[i];

                if (checkedVertices[entry].contains(i))
                    continue;

                checkedVertices[entry].insert(i);

                if (shouldVertexBePainted(vertex.position))
                {
                    TerrainVertexData data = std::make_pair(entry, i);
                    dataToReturn << data;
                }
            }
        }
    }

    //TODO
    //static std::mutex insertGuard;
   //tbb::parallel_for(0, int(nodes.size()), [&](int i)
   //     {
   //         auto&& v = nodes[i];
   //         float startRadius = 100.f;
   //         auto findResultIds = getRelevantBlocks(v, startRadius);
   //         while (findResultIds.empty())
   //         {
   //             startRadius *= 2.f;
   //             if (startRadius >= maxRadius)
   //                 break;

   //             findResultIds = getRelevantBlocks(v, startRadius);
   //         }

   //         for (auto&& entry : findResultIds)
   //         {
   //             auto&& cluster = clusters[entry];
   //             for (auto i = 0; i < cluster->geometry->vertices.size(); i++)
   //             {
   //                 auto&& vertex = cluster->geometry->vertices[i];

   //                 if (checkedVertices[entry].contains(i))
   //                     continue;

   //                 {
   //                     std::scoped_lock lock(insertGuard);
   //                     checkedVertices[entry].insert(i);
   //                 }

   //                 if (shouldVertexBePainted(vertex.pos))
   //                 {
   //                     TerrainVertexData data = std::make_pair(entry, i);

   //                     {
   //                         std::scoped_lock lock(insertGuard);
   //                         dataToReturn << data;
   //                     }
   //                 }
   //             }
   //         }
   //     });

    return dataToReturn;
}

