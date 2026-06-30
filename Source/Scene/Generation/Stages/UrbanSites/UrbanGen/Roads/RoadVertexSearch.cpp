#include "stdafx.h"
#include "RoadVertexSearch.h"

#include "UrbanTopologyGenerator.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanUtils.h"

float RoadVertexSearch::GoalDistanceEstimate(const RoadVertexSearch& nodeGoal) const
{
    return distance(position, nodeGoal.getPosition());
}

bool RoadVertexSearch::IsGoal(const RoadVertexSearch& nodeGoal) const
{
    if (clusterId == nodeGoal.getClusterId())
        return id == nodeGoal.getId();

    return false;
}

bool RoadVertexSearch::GetSuccessors(AStarSearch<RoadVertexSearch>* astarsearch, RoadVertexSearch* parentNode) const
{
    auto&& clusters = Generation::Data::get()->getTerrainClustersMap();

    const auto clusterResult = Generation::Data::get()->getBlockQuadTree()->find_all_nearest(position.x, position.z, 
        Generation::Data::get()->getLargestVoronoiCellRadius());

    for (auto&& clId : clusterResult)
    {
        const auto vertexResult = clusters[clId->data]->getFaceQuadTree().find_all_nearest(position.x, position.z, 600.f);
        for (auto&& n : vertexResult)
        {
            if (n->x == position.x && n->y == position.z)
                continue;

            auto&& [cl, id, pos] = RoadVertexSearch::getVertexData(GVector2D(n->x, n->y));

            RoadVertexSearch newNode;
            if (parentNode)
                newNode = RoadVertexSearch(pos, cl, id, pos.y(),
                    isCrossingEnvBounds(pos, cl), parentNode->startPos, parentNode->targetPos);
            else
                newNode = RoadVertexSearch(pos, cl, id, pos.y(),
                    isCrossingEnvBounds(pos, cl), {}, {});

            if (GetCost(newNode) < std::numeric_limits<float>::max())
                astarsearch->AddSuccessor(newNode);
        }
    }

    return true;
}

float RoadVertexSearch::GetCost(const RoadVertexSearch& successor) const
{
    if (successor.envBoundsInPath)
        return std::numeric_limits<float>::max();

    constexpr float maxAllowedDeviationAngle = 80.f;

    if (UrbanUtils::fastAngle3(startPos, successor.getPosition(), targetPos) > maxAllowedDeviationAngle)
        return std::numeric_limits<float>::max();

    const float heightDiff = std::abs(height - successor.getHeight());
    const float heightDiffMod = heightDiff <= 20.f ? 0.f : 0.3f;

    return (distance(this->getPosition(), successor.getPosition())) + (heightDiff * heightDiffMod) /*+ (successor.angle * 0.05f)*/;
}

bool RoadVertexSearch::IsSameState(const RoadVertexSearch& rhs) const
{
    if (clusterId == rhs.getClusterId())
        return id == rhs.getId();

    return false;
}

bool RoadVertexSearch::isCrossingEnvBounds(const GVector2D& target, const int targetClusterId) const
{
    //TODO: This map uses blocks atm and not cluster ids
    auto&& bounds = Generation::Data::get()->getEnviroBounds();
    auto&& bounds1 = bounds[this->getClusterId()];
    auto&& bounds2 = bounds[targetClusterId];

    const auto seg = Segment2D(getPosition(), target);

    auto findCrossing = [&seg](const std::vector<QSharedPointer<Generation::EnvBound>>& inBound) -> bool {
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

std::tuple<int, int, QVector3D> RoadVertexSearch::getVertexData(const QVector3D& vertex)
{
    const auto clusterResult = Generation::Data::get()->getBlockQuadTree()->find_nearest(vertex.x(), vertex.z(), Generation::Data::get()->getLargestVoronoiCellRadius());
    Q_ASSERT(clusterResult);

    auto&& clusters = Generation::Data::get()->getTerrainClustersMap();

    const auto vertexResult = clusters[clusterResult->data]->getFaceQuadTree().find_nearest(vertex.x(), vertex.z(), 600.f);
    Q_ASSERT(vertexResult);

    return std::make_tuple(clusters[clusterResult->data]->keyCell, vertexResult->data / 3, UrbanUtils::heightQuery(GVector2D(vertexResult->x, vertexResult->y)));
}

void RoadVertexSearchExecutor::setStartAndGoal(const QVector3D& startPos, const QVector3D& endPos, const UrbanTopologyGenerator* inTopology)
{
    auto&& [cl, id, pos] = RoadVertexSearch::getVertexData(startPos);
    auto&& [cl2, id2, pos2] = RoadVertexSearch::getVertexData(endPos);

    startNode = { pos, cl, id, pos.y(), false, pos, pos2 };
    goalNode = { pos2, cl2, id2, pos2.y(), false, pos, pos2 };

}

bool RoadVertexSearchExecutor::execute()
{
    AStarSearch<RoadVertexSearch> astarsearch = AStarSearch<RoadVertexSearch>(allocationSize);

    astarsearch.SetStartAndGoalStates(startNode, goalNode);

    unsigned int searchState;
    unsigned int searchSteps = 0;

    while (true)
    {
        searchState = astarsearch.SearchStep();
        searchSteps++;

        if (searchState != AStarSearch<RoadVertexSearch>::SEARCH_STATE_SEARCHING)
            break;
    }

    bool bSuccess = false;
    if (searchState == AStarSearch<RoadVertexSearch>::SEARCH_STATE_SUCCEEDED 
        || searchState == AStarSearch<RoadVertexSearch>::SEARCH_STATE_FAILED)
    {
        RoadVertexSearch* node = astarsearch.GetSolutionStart();
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

        if (searchState == AStarSearch<RoadVertexSearch>::SEARCH_STATE_FAILED)
            astarsearch.FreeAllNodes();
        else
            astarsearch.FreeSolutionNodes();

        bSuccess = (searchState == AStarSearch<RoadVertexSearch>::SEARCH_STATE_SUCCEEDED);
    }

    astarsearch.EnsureMemoryFreed();

    return bSuccess;
}

void RoadVertexSearchExecutor::runTrace()
{
    OmniProfile("Urban Road Terrain Search", true);

    for (auto i = 0; i < numSearches; i++)
        runTrace();
}
