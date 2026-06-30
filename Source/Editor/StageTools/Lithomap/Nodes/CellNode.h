#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Scene/Generation/Stages/Lithomap/LithoCluster.h"

struct CellSnapshotData
{
	qint64 lithoType;
	Generation::ETerrainBlock blockType;
};

class CellNode : public StageObjectNode
{
public:
	CellNode(const GVector2D& inCellCenter)
		: cellCenter(inCellCenter)
	{}

	const auto& getCell() const { return cellCenter; }
	const auto& getSnapshot() const { return snapshotCell; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setCell(const GVector2D& inCellCenter) { cellCenter = inCellCenter; }
	void nullifyCell() { cellCenter = std::nullopt; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void updateLithoSnapshot();
	void clearSnapshot() { snapshotCell.reset(); }

	void setClusterNode(const qint64& inClusterNode) { clusterNode = inClusterNode; }
	void setMetaClusterNode(const qint64& inMetaClusterNode) { metaClusterNode = inMetaClusterNode; }

	void clearClusterNode() { clusterNode = std::nullopt; }
	void clearMetaClusterNode() { metaClusterNode = std::nullopt; }

private:
	CellNode() = default;

	std::optional<GVector2D> cellCenter;
	std::optional<CellSnapshotData> snapshotCell;
	bool createdOnCurrentStage = true;

	std::optional<qint64> clusterNode;
	std::optional<qint64> metaClusterNode;

	FRIEND_OMNIBIN(CellNode);
};

void omniSave(const CellNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(CellNode& object, OmniBin<std::ios::in>& omniBin);