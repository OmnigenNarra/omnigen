#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"

struct ClusterSnapshotData
{
	qint64 metaClusterGuid;
	std::unordered_set<int> cells;
	int keyCell;
	QHash<int, std::vector<Generation::BorderPointInfo>> borderPoints;
	std::array<float, 2> temperatureRange;
	std::array<float, 2> humidityRange;

	FRIEND_OMNIBIN(ClusterSnapshotData);
};

void omniSave(const ClusterSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(ClusterSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class ClusterNode : public StageObjectNode, public QEnableSharedFromThis<ClusterNode>
{
public:
	ClusterNode(const QSharedPointer<Generation::TerrainBlockClusterBase>& inCluster)
		: cluster(inCluster)
		, type(inCluster->type)
		, guid(inCluster->getGuid())
	{}

	const auto& getCluster() const { return cluster; };
	const auto& getType() const { return type; };
	const auto& getSnapshot() const { return snapshotCluster; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setCluster(const QSharedPointer<Generation::TerrainBlockClusterBase>& inCluster) { cluster = inCluster; }
	void nullifyCluster() { cluster = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotCluster.reset(); }

private:
	ClusterNode() = default;

	qint64 guid;
	Generation::ETerrainBlock type;
	QSharedPointer<Generation::TerrainBlockClusterBase> cluster;
	std::optional<ClusterSnapshotData> snapshotCluster;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(ClusterNode);
};

void omniSave(const ClusterNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(ClusterNode& object, OmniBin<std::ios::in>& omniBin);