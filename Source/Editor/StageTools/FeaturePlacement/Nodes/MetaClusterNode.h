#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"

struct MetaClusterSnapshotData
{
	std::vector<qint64> clusterGuids;
	std::unordered_set<int> cells;
	quint32 terrainTexPack;
	quint32 biomeTexPack;
	quint32 packParams;

	FRIEND_OMNIBIN(MetaClusterSnapshotData);
};

void omniSave(const MetaClusterSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(MetaClusterSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class MetaClusterNode : public StageObjectNode, public QEnableSharedFromThis<MetaClusterNode>
{
public:
	MetaClusterNode(const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& inMetaCluster)
		: metaCluster(inMetaCluster)
		, type (inMetaCluster->getType())
		, guid(inMetaCluster->getGuid())
	{}

	const auto& getMetaCluster() const { return metaCluster; };
	const auto& getType() const { return type; };
	const auto& getSnapshot() const { return snapshotMetaCluster; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setMetaCluster(const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& inMetaCluster) { metaCluster = inMetaCluster; }
	void nullifyMetaCluster() { metaCluster = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotMetaCluster.reset(); }

private:
	MetaClusterNode() = default;

	qint64 guid;
	Generation::ETerrainBlock type;
	QSharedPointer<Generation::TerrainBlockMetaClusterBase> metaCluster;
	std::optional<MetaClusterSnapshotData> snapshotMetaCluster;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(MetaClusterNode);
};

void omniSave(const MetaClusterNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(MetaClusterNode& object, OmniBin<std::ios::in>& omniBin);