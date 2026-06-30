#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Utils/AStar.h"

class TerrainSearchNode
{
public:
    TerrainSearchNode() = default;
    TerrainSearchNode(const GVector2D& inPos, const Generation::ETerrainBlock type, 
		const int id, const float avgHeight, const float avgHeightDiff, const bool envBoundsCross)
        : position(inPos), blockType(type), blockId(id),
        averageHeight(avgHeight), envBoundsInPath(envBoundsCross), averageHeightDifference(avgHeightDiff) {}

    [[nodiscard]] const auto& getPosition() const { return position; }
	[[nodiscard]] const auto& getType() const { return blockType; }
	[[nodiscard]] int getId() const { return blockId; }
	[[nodiscard]] float getAverageHeight() const { return averageHeight; }

	// ASearchState required methods

	[[nodiscard]] float GoalDistanceEstimate(const TerrainSearchNode& nodeGoal) const;
	// Returns true if this node is the goal node
	[[nodiscard]] bool IsGoal(const TerrainSearchNode& nodeGoal) const;
	// Retrieves all successors to this node and adds them via astarsearch.addSuccessor()
	[[nodiscard]] bool GetSuccessors(AStarSearch<TerrainSearchNode>* astarsearch, TerrainSearchNode* parentNode) const;
	// Computes the cost of travelling from this node to the successor node
	[[nodiscard]] float GetCost(const TerrainSearchNode& successor) const;
	// Returns true if this node is the same as the rhs node
	[[nodiscard]] bool IsSameState(const TerrainSearchNode& rhs) const;

	void debugPrint();

private:
	[[nodiscard]] float heightDiffToSuccessor(const GVector2D& target) const;
	[[nodiscard]] bool isCrossingEnvBounds(const GVector2D& target, const int targetBlockId) const;

	GVector2D position = {};
	Generation::ETerrainBlock blockType = Generation::ETerrainBlock::Last;
	int blockId = -1;
	float averageHeight = 0.f;

	bool envBoundsInPath = false;

	float averageHeightDifference = 0.f;

	//Less means more detailed traces
    float heightTraceOffset = 100.f;

	static inline std::unordered_map<Generation::ETerrainBlock, float> terrainBlockCosts = {
		{ Generation::ETerrainBlock::Beach, 1.5f },
		{ Generation::ETerrainBlock::Cliff, 3.f },
		{ Generation::ETerrainBlock::Fault, 2.f },
		{ Generation::ETerrainBlock::Flatland, 1.f },
		{ Generation::ETerrainBlock::Ridge, 3.f },
		{ Generation::ETerrainBlock::Slope, std::numeric_limits<float>::max() },
		{ Generation::ETerrainBlock::Seabed, std::numeric_limits<float>::max() },
		{ Generation::ETerrainBlock::Precipice, std::numeric_limits<float>::max() },
		{ Generation::ETerrainBlock::Desert, 1.5f }
	};
};

class TerrainSearchExecutor
{
public:
	TerrainSearchExecutor() = default;
	void setSearchIterationNum(const int num) { numSearches = num; }
	void setStartAndGoal(const QVector3D& startPos, const QVector3D& endPos);
	void execute();

	[[nodiscard]] const auto& getPath() const { return path; }
protected:
	void runTrace();
private:
	std::vector<QVector3D> path;

	int numSearches = 1;
	TerrainSearchNode startNode;
	TerrainSearchNode goalNode;
};
