#include "stdafx.h"
#include "RoadTerrainSearch.h"

#include "RoadGenerator.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"

float TerrainSearchNode::GoalDistanceEstimate(const TerrainSearchNode& nodeGoal) const
{
    return distance(position, nodeGoal.getPosition());
}

bool TerrainSearchNode::IsGoal(const TerrainSearchNode& nodeGoal) const
{
    return blockId == nodeGoal.getId();
}

bool TerrainSearchNode::GetSuccessors(AStarSearch<TerrainSearchNode>* astarsearch, TerrainSearchNode* parentNode) const
{
    auto&& terrainVoronoi = Generation::Data::get()->getTerrainCells();
    auto&& terrainBlocks = Generation::Data::get()->getTerrainCells()->getCells();
    auto&& terrainClusters = Generation::Data::get()->getTerrainClustersMap();
    auto&& neighbors = terrainVoronoi->getCellAt(blockId).getNeighbors();

    for (auto it = neighbors.keyBegin(); it != neighbors.keyEnd(); ++it)
    {
        auto&& terrainBlock = terrainBlocks[*it];
        //TODO: Calculate actual average height?

        //Structuring this this way is more verbose but avoids some unneeded computations
        if (isCrossingEnvBounds(terrainBlock->getCenter(), *it))
        {
            auto newNode = TerrainSearchNode(terrainBlock->getCenter(), terrainClusters[*it]->type,
                *it, 0.f, 0.f, true);
            astarsearch->AddSuccessor(newNode);

            continue;
        }

        auto newNode = TerrainSearchNode(terrainBlock->getCenter(), terrainClusters[*it]->type,
            *it, UrbanUtils::heightQuery(terrainBlock->getCenter()).y(),
            heightDiffToSuccessor(terrainBlock->getCenter()), false);

        astarsearch->AddSuccessor(newNode);
    }

    return true;
}

float TerrainSearchNode::GetCost(const TerrainSearchNode& successor) const
{
    if (successor.envBoundsInPath)
        return std::numeric_limits<float>::max();

    const float terrainBlockCost = terrainBlockCosts[successor.getType()];
    if (terrainBlockCost > 100'000.f)
        return std::numeric_limits<float>::max();

    const float avgHeightDiff = averageHeightDifference + successor.averageHeightDifference;

    return (distance(this->getPosition(), successor.getPosition()) * terrainBlockCost) * avgHeightDiff;
}

bool TerrainSearchNode::IsSameState(const TerrainSearchNode& rhs) const
{
    return blockId == rhs.getId();
}

void TerrainSearchNode::debugPrint()
{
    OmniLog(ELoggingLevel::Info) << "Node Id " << blockId << "\n\t"
        << "Node position: " << position.x << ", " << position.z << "\n\t"
        << "Node type: " <<= static_cast<int>(blockType);
}

float TerrainSearchNode::heightDiffToSuccessor(const GVector2D& target) const
{
    std::vector<float> diffs;

    QVector3D lastPoint = UrbanUtils::heightQuery(getPosition());
    const QVector3D targetPoint = UrbanUtils::heightQuery(target);

    const auto dir = (target - position).normalized();
    while (true)
    {
        if (distance(GVector2D(lastPoint), GVector2D(targetPoint)) < heightTraceOffset * 1.1f)
            break;

        const auto offsetV = dir * heightTraceOffset;
        const auto newPoint = UrbanUtils::heightQuery(lastPoint + offsetV);

        diffs << abs(lastPoint.y() - newPoint.y());

        lastPoint = newPoint;
    }

    return 1.0f * std::accumulate(diffs.begin(), diffs.end(), 0.f) / diffs.size();
}

bool TerrainSearchNode::isCrossingEnvBounds(const GVector2D& target, const int targetBlockId) const
{
    auto&& bounds = Generation::Data::get()->getEnviroBounds();
    auto&& bounds1 = bounds[this->getId()];
    auto&& bounds2 = bounds[targetBlockId];

    const auto seg = Segment2D(getPosition(), target);

    auto findCrossing = [&seg](const std::vector<QSharedPointer<Generation::EnvBound>>& inBound) -> bool
    {
        for (auto&& bound : inBound)
        {
            if (bound->line.empty())
                continue;

            for (auto i = 0; i < bound->line.size() - 1; i++)
            {
                if (const auto testSeg = Segment2D(bound->line[i], bound->line[i + 1]);
                    seg.intersects(testSeg, true))
                    return true;
            }
        }

        return false;
    };

    if (findCrossing(bounds1))
        return true;
    if (findCrossing(bounds2))
        return true;

    return false;
}

void TerrainSearchExecutor::setStartAndGoal(const QVector3D& startPos, const QVector3D& endPos)
{
    auto&& blocksQTree = Generation::Data::get()->getBlockQuadTree();
    auto&& terrainBlocks = Generation::Data::get()->getTerrainCells()->getCells();
    auto&& terrainClusters = Generation::Data::get()->getTerrainClustersMap();

    const auto start = blocksQTree->find_nearest(startPos.x(), startPos.z(), 5'000.f);
    if (!start)
        Q_ASSERT(false);

    const auto goal = blocksQTree->find_nearest(endPos.x(), endPos.z(), 5'000.f);
    if (!goal)
        Q_ASSERT(false);

    auto&& startBlock = terrainBlocks[start->data];
    startNode = { startBlock->getCenter(), terrainClusters[start->data]->type,
            static_cast<int>(start->data), UrbanUtils::heightQuery(startBlock->getCenter()).y(), 0.f, false };

    auto&& goalBlock = terrainBlocks[goal->data];
    goalNode = { goalBlock->getCenter(),  terrainClusters[goal->data]->type,
            static_cast<int>(goal->data), UrbanUtils::heightQuery(goalBlock->getCenter()).y(), 0.f, false };
}

void TerrainSearchExecutor::execute()
{
    OmniProfile("Rural Road Terrain Search");

    for (auto i = 0; i < numSearches; i++)
        runTrace();
}

void TerrainSearchExecutor::runTrace()
{
    AStarSearch<TerrainSearchNode> astarsearch = AStarSearch<TerrainSearchNode>(
        Generation::Data::get()->getTerrainCells()->getCells().size());

    astarsearch.SetStartAndGoalStates(startNode, goalNode);

    unsigned int searchState;
    unsigned int searchSteps = 0;

    while (true)
    {
        searchState = astarsearch.SearchStep();
        searchSteps++;

        if (searchState != AStarSearch<TerrainSearchNode>::SEARCH_STATE_SEARCHING)
            break;
    }

    if (searchState == AStarSearch<TerrainSearchNode>::SEARCH_STATE_SUCCEEDED)
    {
        TerrainSearchNode* node = astarsearch.GetSolutionStart();
        int steps = 0;

        path.push_back(UrbanUtils::heightQuery(node->getPosition()));
        while (true)
        {
            node = astarsearch.GetSolutionNext();
            if (!node)
                break;

            path.push_back(UrbanUtils::heightQuery(node->getPosition()));
            ++steps;
        }

        astarsearch.FreeSolutionNodes();
    }
    else if (searchState == AStarSearch<TerrainSearchNode>::SEARCH_STATE_FAILED)
    {
        OmniLog(ELoggingLevel::Critical) <<= "Search terminated. Did not find goal state\n";
    }


    astarsearch.EnsureMemoryFreed();
}
