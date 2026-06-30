#include "stdafx.h"
#include "UrbanUtils.h"
#include <array>

#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Mathematics/BezierCurve.h"
#include "Mathematics/MinimalCycleBasis.h"
#include "Mathematics/IsPlanarGraph.h"

#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"

MCBComputer::MCBComputer(const RoadGraphData& inGraph)
    : data(inGraph)
{
    OmniProfile("MCB Computer", true);

    gte::IsPlanarGraph<float> isPlanarGraph;
    if (isPlanarGraph(data.nodes, data.edges) != gte::IsPlanarGraph<float>::IPG_IS_PLANAR_GRAPH)
    {
        if (!isPlanarGraph.GetDuplicatedPositions().empty() || !isPlanarGraph.GetEdgesWithInvalidVertices().empty() 
            || !isPlanarGraph.GetDuplicatedEdges().empty())
            Q_ASSERT(false);
    }

    std::vector<std::shared_ptr<gte::MinimalCycleBasis<float>::Tree>> forest;
    gte::MinimalCycleBasis<float> mcb(data.nodes, data.edges, forest);

    consumeForest(forest);
}

void MCBComputer::fixEdgeOrdering(const std::vector<int>& edgesToSwap)
{
    for (auto& edge : edgesToSwap)
    {
        data.edges[edge] = { data.edges[edge][1], data.edges[edge][0] };
    }
}

void MCBComputer::consumeForest(const std::vector<std::shared_ptr<gte::MinimalCycleBasis<float>::Tree>>& inForest)
{
    using TreePtr = std::shared_ptr<gte::MinimalCycleBasis<float>::Tree>;

    const auto preorderTraversal = [this](const TreePtr& root) -> std::vector<Polygon2D>
    {
        std::stack<TreePtr> stack;

        std::vector<std::vector<int>> visitedCycles;

        stack.push(root);

        while (!stack.empty())
        {
            const TreePtr temp = stack.top();
            stack.pop();

            visitedCycles.push_back(temp->cycle);
            for (int i = temp->children.size() - 1; i >= 0; i--)
            {
                stack.push(temp->children[i]);
            }
        }

        std::vector<Polygon2D> lotsToReturn;

        for (auto&& cycle : visitedCycles)
        {
            std::vector<GVector2D> cyclePoints;
            for (auto&& idx : cycle)
            {
                cyclePoints << GVector2D(data.nodes[idx][0], data.nodes[idx][1]);
            }

            if (!cyclePoints.empty())
                lotsToReturn << Polygon2D(cyclePoints);
        }

        return lotsToReturn;
    };

    for (auto&& tree : inForest)
    {
        lots << preorderTraversal(tree);
    }
}

std::pair<float, QSet<int>> UrbanUtils::calculateSiteArea(const int firstIdx, const float areaSize)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();
    auto estimateCellArea = [&](int idx) { return std::pow(diagram->getCells()[idx]->getRadius() * 0.01, 2) * 2.5f; };

    // Find first cell
    QSet<int> siteArea;

    siteArea << firstIdx;
    float siteAreaInSquareMeters = estimateCellArea(firstIdx);

    auto tryExpand = [&](int idx)
    {
        if (siteArea.contains(idx))
            return false;

        siteAreaInSquareMeters += estimateCellArea(idx);
        return true;
    };

    static constexpr std::array allowedBlockTypes = {
        Generation::ETerrainBlock::Beach,
        Generation::ETerrainBlock::Fault,
        Generation::ETerrainBlock::Flatland
    };

    // Grow area
    QSet<int> deadEnds;
    while (true) // equivalent to 1 grid square = a village
    {
        for (int cellIdx : siteArea)
        {
            if (deadEnds.contains(cellIdx))
                continue;

            if (!contains(allowedBlockTypes, Generation::Data::get()->getTerrainClustersMap()[cellIdx]->type))
            {
                deadEnds << cellIdx;
                continue;
            }

            if (!diagram->expandCellularCluster(&siteArea, cellIdx, tryExpand))
            {
                deadEnds << cellIdx;
                continue;
            }

            if (siteAreaInSquareMeters >= areaSize)
                break;
        }

        if (siteAreaInSquareMeters >= areaSize)
            break;

        if (deadEnds.size() == siteArea.size())
            break;
    }


    // TODO: Can still break in one-time in universe occurrence of cells clusters (hole)
    // inside urban site being so dense that they create their own holes 
    auto isInsideUrbanSite = [&](const QSet<int>& cellsCluster)
    {
        for (auto& cell_id : cellsCluster)
        {
            auto&& neighbours = Generation::Data::get()->getTerrainCells()->getCellAt(cell_id).getNeighbors().keys();
            const int count = std::ranges::count_if(neighbours, [&](int n) {return siteArea.contains(n) || cellsCluster.contains(n); });

            if (count < neighbours.size())
                return false;
        }

        return true;
    };

    for (auto& cellsCluster : getUrbanSiteNeighbourCellsClusters(siteArea))
    {
        if (isInsideUrbanSite(cellsCluster))
            for (auto& cell_id : cellsCluster)
                siteArea += cell_id;
    }

    return std::make_pair(siteAreaInSquareMeters, siteArea);
}

std::pair<float, QSet<int>> UrbanUtils::calculateFlatlandArea(const int firstIdx)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();
    auto estimateCellArea = [&](int idx) { return std::pow(diagram->getCells()[idx]->getRadius() * 0.01, 2) * 2.5f; };
    auto&& clustersMap = Generation::Data::get()->getTerrainClustersMap();

    // Find first cell
    QSet<int> siteArea;

    siteArea << firstIdx;
    float siteAreaInSquareMeters = estimateCellArea(firstIdx);

    auto tryExpand = [&](int idx)
    {
        if (siteArea.contains(idx))
            return false;

        if (clustersMap[idx]->type != Generation::ETerrainBlock::Flatland)
            return false;

        siteAreaInSquareMeters += estimateCellArea(idx);
        return true;
    };

    // Grow area
    QSet<int> deadEnds;
    while (true)
    {
        for (int cellIdx : siteArea)
        {
            if (deadEnds.contains(cellIdx))
                continue;

            if (!diagram->expandCellularCluster(&siteArea, cellIdx, tryExpand))
            {
                deadEnds << cellIdx;
                continue;
            }

            if (siteAreaInSquareMeters >= getUrbanSizeAsFloat(EUrbanSize::Last))
                break;
        }

        if (siteAreaInSquareMeters >= getUrbanSizeAsFloat(EUrbanSize::Last))
            break;

        if (deadEnds.size() == siteArea.size())
            break;
    }

    return std::pair(siteAreaInSquareMeters, siteArea);
}

float UrbanUtils::getRoadWidthFromEnum(const ERoadWidth inWidth)
{
    switch (inWidth)
    {
        using enum ERoadWidth;
    case Alley:
        return 150.f;
    case Street:
        return 150.f;
    case MainRoad:
        return 200.f;
    case Highway:
        return 500.f;
    }

    return 0.f;
}

float UrbanUtils::getPointHeightAverage(const std::vector<QVector3D>& pts)
{
    float heights = 0.f;
    for (auto&& pt : pts)
        heights += pt.y();

    return heights / pts.size();
}

QVector3D UrbanUtils::heightQuery(const GVector2D& p, const float heightMedian)
{
    auto pred = [&](auto&& a, auto&& b) { return std::abs(heightMedian - a.y()) < std::abs(heightMedian - b.y()); };

    const auto candidates = heightMedian < 0.f ? Generation::Utils::castPointTo3D(p) : Generation::Utils::castPointTo3D(p, pred);
    return candidates.empty() ? QVector3D(p) : candidates[0] + QVector3D(0.f, 2.f, 0.f);
}

void UrbanUtils::removeGraphIntersections(AdjacencyGraph* inGraph)
{
    std::vector<RoadMergeInfo> mergeData;

    for (auto&& node : inGraph->getNodes())
    {
        const auto nearbyNodes = inGraph->getNodesQTree()->find_all_nearest(
            node->data.x, node->data.z, inGraph->getMaxEdgeSize() * 2.f);
        if (nearbyNodes.empty())
            continue;

        for (auto&& n : node->neighbors)
        {
            for (auto&& r : nearbyNodes)
            {
                auto&& nearbyNode = inGraph->getNode(r->data);
                if (n->data == nearbyNode->data || node->data == nearbyNode->data)
                    continue;

                Segment2D firstSeg = Segment2D(node->data, n->data);
                for (auto&& rn : nearbyNode->neighbors)
                {
                    Segment2D secondSeg = Segment2D(nearbyNode->data, rn->data);

                    if (firstSeg.intersects(secondSeg, false))
                    {
                        const auto query = firstSeg.getIntersectionPoint(secondSeg);
                        if (!query)
                            continue;

                        RoadMergeInfo newInfo;
                        newInfo.nodeToAdd = *query;
                        newInfo.edgesToRemove << std::make_pair(inGraph->getNodeIdx(node->data),
                            inGraph->getNodeIdx(n->data));
                        newInfo.edgesToRemove << std::make_pair(inGraph->getNodeIdx(nearbyNode->data),
                            inGraph->getNodeIdx(rn->data));

                        newInfo.nodesToConnectFrom << inGraph->getNodeIdx(node->data);
                        newInfo.nodesToConnectFrom << inGraph->getNodeIdx(nearbyNode->data);

                        newInfo.nodesToConnectTo << inGraph->getNodeIdx(n->data);
                        newInfo.nodesToConnectTo << inGraph->getNodeIdx(rn->data);

                        mergeData << newInfo;
                    }
                }
            }
        }
    }

    for (auto&& info : mergeData)
    {
        const size_t idx = inGraph->addNode(info.nodeToAdd);

        for (auto&& [pt1, pt2] : info.edgesToRemove)
            inGraph->removeEdge(pt1, pt2);

        for (auto&& id : info.nodesToConnectFrom)
            inGraph->addEdge(id, idx);
        for (auto&& id : info.nodesToConnectTo)
            inGraph->addEdge(idx, id);
    }
}

RoadGraphData UrbanUtils::getDataFromAdjacencyGraph(const AdjacencyGraph& inGraph)
{
    RoadGraphData data;

    const auto checkMatch = [&data](const int edge1, const int edge2) -> bool
    {
        for (auto i = 0; i < data.edges.size(); i++)
        {
            if (contains(data.edges, std::array<int, 2>{ edge1, edge2 }) ||
                contains(data.edges, std::array<int, 2>{ edge2, edge1 }))
                return true;
        }

        return false;
    };

    std::vector<std::shared_ptr<AdjacencyGraph::Node>> sortedNodes;

    for (auto&& node : inGraph.getNodes())
    {
        sortedNodes.push_back(node);
    }

    std::ranges::sort(sortedNodes, [](const std::shared_ptr<AdjacencyGraph::Node>& A, const std::shared_ptr<AdjacencyGraph::Node>& B)
        {
            return A->data.x < B->data.x;
        });

    for (auto&& node : inGraph.getNodes())
    {
        auto&& pt1 = node->data;
        data.nodes.push_back({ pt1.x, pt1.z });
    }

    for (auto i = 0; i < sortedNodes.size(); i++)
    {
        for (auto&& node = sortedNodes[i]; auto && n : node->neighbors)
        {
            if (!checkMatch(inGraph.getNodeIdx(node->data), inGraph.getNodeIdx(n->data)))
                data.edges.push_back({ (int)inGraph.getNodeIdx(node->data), (int)inGraph.getNodeIdx(n->data) });
        }
    }

    return data;
}

float UrbanUtils::fastAngle3(const GVector2D& a, const GVector2D& b, const GVector2D& c)
{
    GVector2D ab = GVector2D{ b.x - a.x, b.z - a.z };
    GVector2D cb = GVector2D{ b.x - c.x, b.z - c.z };

    // dot product
    const float dot = (ab.x * cb.x + ab.z * cb.z);

    // length square of both vectors
    const float abSqr = ab.x * ab.x + ab.z * ab.z;
    const float cbSqr = cb.x * cb.x + cb.z * cb.z;

    // square of cosine of the needed angle    
    const float cosSqr = dot * dot / abSqr / cbSqr;

    const float cos2 = 2 * cosSqr - 1;

    const float alpha2 =
        (cos2 <= -1) ? std::numbers::pi :
        (cos2 >= 1) ? 0 :
        acosf(cos2);

    const float rslt = alpha2 / 2;

    float rs = rslt * 180. / std::numbers::pi;

    // Now revolve the ambiguities.
    // 1. If dot product of two vectors is negative - the angle is definitely
    // above 90 degrees. Still we have no information regarding the sign of the angle.

    // NOTE: This ambiguity is the consequence of our method: calculating the cosine
    // of the double angle. This allows us to get rid of calling sqrt.

    if (dot < 0)
        rs = 180 - rs;

    // 2. Determine the sign. For this we'll use the Determinant of two vectors.

    const float det = (ab.x * cb.z - ab.z * cb.z);
    if (det < 0)
        rs = -rs;

    return (int)floor(rs + 0.5);
}


std::vector<QSet<int>> UrbanUtils::getUrbanSiteNeighbourCellsClusters(const QSet<int>& siteArea)
{
    QSet<int> urbanSiteNeighbours;

    for (auto& id : siteArea)
    {
        auto&& cell = Generation::Data::get()->getTerrainCells()->getCellAt(id);

        for (auto it = cell.getNeighbors().keyBegin(); it != cell.getNeighbors().keyEnd(); ++it)
        {
            if (siteArea.contains(*it))
                continue;

            urbanSiteNeighbours += *it;
        }
    }

    std::vector<QSet<int>> urbanSiteCellsClusters;

    for (auto& id : urbanSiteNeighbours)
    {
        if (std::ranges::any_of(urbanSiteCellsClusters, [&](const QSet<int>& cluster) { return cluster.contains(id); }))
            continue;

        QList<int> newCluster{ id };

        for (size_t i = 0; i < newCluster.size(); i++)
        {
            auto&& cell = Generation::Data::get()->getTerrainCells()->getCellAt(newCluster[i]);

            for (auto& neighbour_id : cell.getNeighbors().keys())
                if (urbanSiteNeighbours.contains(neighbour_id) && !newCluster.contains(neighbour_id))
                    newCluster.push_back(neighbour_id);
        }

        urbanSiteCellsClusters << QSet(newCluster.begin(), newCluster.end());
    }

    return urbanSiteCellsClusters;
}

std::tuple<std::vector<GVector2D>, std::vector<GVector2D>> UrbanUtils::getPointsAtOffset(const std::vector<GVector2D>& points, ERoadWidth roadWidth)
{
    return getPointsAtOffset(points, getRoadWidthFromEnum(roadWidth));
}

std::tuple<std::vector<GVector2D>, std::vector<GVector2D>> UrbanUtils::getPointsAtOffset(
    const std::vector<GVector2D>& points, float offset)
{
    auto offsetSegment = [](const Segment2D& seg, float angle, float offset)
    {
        const GVector2D dir = (seg.second - seg.first).normalized();
        const GVector2D rotatedDir = GVector2D::rotateDegrees(dir, angle);

        const GVector2D p1 = seg.first + (rotatedDir * offset);
        const GVector2D p2 = seg.second + (rotatedDir * offset);

        return Segment2D(p1, p2);
    };

    if (points.size() < 2)
        return { points, points };

    std::vector<GVector2D> leftBound, rightBound;

    for (auto i = 0; i < points.size() - 1; i++)
    {
        const auto seg1 = offsetSegment(Segment2D(points[i], points[i + 1]), -90, offset / 2);
        const auto seg2 = offsetSegment(Segment2D(points[i], points[i + 1]), 90, offset / 2);

        leftBound << seg1.first;
        rightBound << seg2.first;
        if (i == points.size() - 2)
        {
            leftBound << seg1.second;
            rightBound << seg2.second;
        }
    }
    return { leftBound, rightBound };
}

std::vector<GVector2D> UrbanUtils::smoothLine(const std::vector<GVector2D>& line, int lenghtInterval)
{
    std::vector<GVector2D> smoothLine;

    std::vector<gte::Vector<2, float>> bezierPts(line.size());
    std::ranges::transform(line, bezierPts.begin(), [](const GVector2D& v) { return gte::Vector<2, float>{ v.x, v.z }; });

    const gte::BezierCurve<2, float> curve(bezierPts.size() - 1, &bezierPts[0]);

    const int curveLength = curve.GetTotalLength();
    const float endsLength = lenghtInterval + (curveLength % lenghtInterval) / 2.0f;

    smoothLine << VtoG2(curve.GetPosition(0.0f));
    for (float l = endsLength; l <= curveLength - endsLength; l += lenghtInterval)
    {
        smoothLine << VtoG2(curve.GetPosition(curve.GetTime(l)));
    }
    smoothLine << VtoG2(curve.GetPosition(1.0f));

    return smoothLine;
}

float UrbanUtils::getUrbanSizeAsFloat(const EUrbanSize& inSize)
{
    switch (inSize)
    {
        using enum EUrbanSize;
    case Outpost:
        return 2000.f;
    case Village:
        return 5000.f;
    case Town:
        return 10000.f;
    case LargeTown:
        return 20000.f;
    case HugeTown: case Last:
        return 30000.f;
    default:;
    }

    return 0.f;
}

EUrbanSize UrbanUtils::getFloatAsUrbanSize(const float inSize)
{
    Q_ASSERT(inSize > getUrbanSizeAsFloat(EUrbanSize::Outpost));

    if (const auto size = getUrbanSizeAsFloat(EUrbanSize::Village); inSize < size)
        return EUrbanSize::Outpost;

    if (const auto size = getUrbanSizeAsFloat(EUrbanSize::Town); inSize < size)
        return EUrbanSize::Village;

    if (const auto size = getUrbanSizeAsFloat(EUrbanSize::LargeTown); inSize < size)
        return EUrbanSize::Town;

    if (const auto size = getUrbanSizeAsFloat(EUrbanSize::HugeTown); inSize < size)
        return EUrbanSize::LargeTown;

    return EUrbanSize::HugeTown;
}
