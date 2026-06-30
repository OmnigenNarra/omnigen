#pragma once
#include "Utils/AStar.h"
#include "Utils/Polygon.h"

class AdjacencyGraph;
class UrbanTopologyGenerator;

class RoadVertexSearch
{
public:
	RoadVertexSearch() = default;
	RoadVertexSearch(const GVector2D& inPos, const int inClusterId,
		const int inId, const float inHeight, const bool envBoundsCross, const GVector2D& inStartPos, const GVector2D& inEndPos)
		: position(inPos), clusterId(inClusterId), id(inId),
		height(inHeight), envBoundsInPath(envBoundsCross), startPos(inStartPos), targetPos(inEndPos)
	{
	}

	[[nodiscard]] const auto& getPosition() const { return position; }
	[[nodiscard]] int getClusterId() const { return clusterId; }
	[[nodiscard]] int getId() const { return id; }
	[[nodiscard]] float getHeight() const { return height; }

	[[nodiscard]] static std::tuple<int, int, QVector3D> getVertexData(const QVector3D& vertex);

	// ASearchState required methods

	[[nodiscard]] float GoalDistanceEstimate(const RoadVertexSearch& nodeGoal) const;
	// Returns true if this node is the goal node
	[[nodiscard]] bool IsGoal(const RoadVertexSearch& nodeGoal) const;
	// Retrieves all successors to this node and adds them via astarsearch.addSuccessor()
	[[nodiscard]] bool GetSuccessors(AStarSearch<RoadVertexSearch>* astarsearch, RoadVertexSearch* parentNode) const;
	// Computes the cost of travelling from this node to the successor node
	[[nodiscard]] float GetCost(const RoadVertexSearch& successor) const;
	// Returns true if this node is the same as the rhs node
	[[nodiscard]] bool IsSameState(const RoadVertexSearch& rhs) const;
private:
	[[nodiscard]] bool isCrossingEnvBounds(const GVector2D& target, const int targetClusterId) const;

	GVector2D position = {};

	GVector2D startPos = {};
	GVector2D targetPos = {};

	int clusterId = -1;
	int id = -1;
	bool envBoundsInPath = false;

	float height = 0.f;
	float angle = 0.f;
};

class RoadVertexSearchExecutor
{
public:
	RoadVertexSearchExecutor(const size_t inSize) : allocationSize(inSize) {}
	void setSearchIterationNum(const int num) { numSearches = num; }
	void setStartAndGoal(const QVector3D& startPos, const QVector3D& endPos, const UrbanTopologyGenerator* inTopology);
	bool execute();

	[[nodiscard]] const auto& getPath() const { return path; }
protected:
	void runTrace();
private:
	std::vector<QVector3D> path;

	int numSearches = 1;
	size_t allocationSize = 0;

	RoadVertexSearch startNode;
	RoadVertexSearch goalNode;
};