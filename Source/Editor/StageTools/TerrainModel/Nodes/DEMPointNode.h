#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Omnigen.h"

struct DEMPointSnapshotData
{
	Generation::Heightfield::Vertex heightData;
	Generation::Heightfield::Vertex levelData;
	Generation::Heightfield::Vertex verticalDisplacementXCoords;

	FRIEND_OMNIBIN(DEMPointSnapshotData);
};

void omniSave(const DEMPointSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DEMPointSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class DEMPointNode : public StageObjectNode
{
public:
	DEMPointNode(const GVector2D& inPoint)
		: point(inPoint)
	{}

	const auto& getPoint() const { return point; };
	const auto& getSnapshot() const { return snapshotDEMPoint; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }
	const auto& getCellNode() const { return cellNode; }

	void setPoint(const GVector2D& inPoint) { point = inPoint; }
	void nullifyPoint() { point = std::nullopt; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotDEMPoint.reset(); }

	void setCellNode(const GVector2D& inCellNode) { cellNode = inCellNode; }

	void clearCellNode() { cellNode = std::nullopt; }

private:
	DEMPointNode() = default;

	std::optional<GVector2D> point;
	std::optional<DEMPointSnapshotData> snapshotDEMPoint;
	bool createdOnCurrentStage = true;

	std::optional<GVector2D> cellNode;

	FRIEND_OMNIBIN(DEMPointNode);
};

void omniSave(const DEMPointNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DEMPointNode& object, OmniBin<std::ios::in>& omniBin);