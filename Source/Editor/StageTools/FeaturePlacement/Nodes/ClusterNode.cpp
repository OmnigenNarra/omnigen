#include "stdafx.h"
#include "ClusterNode.h"
#include "Omnigen.h"

void ClusterNode::makeSnapshot()
{
	ClusterSnapshotData snapshotData;
	snapshotData.metaClusterGuid = cluster->metaCluster->getGuid();
	snapshotData.cells = cluster->cells;
	snapshotData.keyCell = cluster->keyCell;
	snapshotData.borderPoints = cluster->borderPoints;
	snapshotData.temperatureRange = cluster->temperatureRange;
	snapshotData.humidityRange = cluster->humidityRange;

	snapshotCluster = std::move(snapshotData);
}

void omniSave(const ClusterSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.metaClusterGuid;
	omniBin << object.cells;
	omniBin << object.keyCell;
	omniBin << object.borderPoints;
	omniBin << object.temperatureRange;
	omniBin << object.humidityRange;
}

void omniLoad(ClusterSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.metaClusterGuid;
	omniBin >> object.cells;
	omniBin >> object.keyCell;
	omniBin >> object.borderPoints;
	omniBin >> object.temperatureRange;
	omniBin >> object.humidityRange;
}

void omniSave(const ClusterNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.type;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotCluster;
}

void omniLoad(ClusterNode& object, OmniBin<std::ios::in>& omniBin)
{
	auto&& clusters = Generation::Data::get()->getTerrainClustersMap();

	omniBin >> object.guid;
	omniBin >> object.type;
	if (auto&& result = std::find_if(clusters.begin(), clusters.end(), [&](auto&& c) { return c && c->getGuid() == object.guid; }); result != clusters.end())
		object.cluster = *result;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotCluster;
}