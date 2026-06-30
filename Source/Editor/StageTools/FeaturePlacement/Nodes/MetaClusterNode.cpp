#include "stdafx.h"
#include "MetaClusterNode.h"
#include "Omnigen.h"

void MetaClusterNode::makeSnapshot()
{
	MetaClusterSnapshotData snapshotData;
	for (auto&& cluster : metaCluster->getClusters())
		snapshotData.clusterGuids << cluster->getGuid();
	snapshotData.cells = metaCluster->getCells();
	snapshotData.terrainTexPack = metaCluster->getTerrainTexPack();
	snapshotData.biomeTexPack = metaCluster->getBiomeTexPack();
	snapshotData.packParams = metaCluster->getPackParams();

	snapshotMetaCluster = std::move(snapshotData);
}

void omniSave(const MetaClusterSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.clusterGuids;
	omniBin << object.cells;
	omniBin << object.terrainTexPack;
	omniBin << object.biomeTexPack;
	omniBin << object.packParams;
}

void omniLoad(MetaClusterSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.clusterGuids;
	omniBin >> object.cells;
	omniBin >> object.terrainTexPack;
	omniBin >> object.biomeTexPack;
	omniBin >> object.packParams;
}

void omniSave(const MetaClusterNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.type;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotMetaCluster;
}

void omniLoad(MetaClusterNode& object, OmniBin<std::ios::in>& omniBin)
{
	auto&& metaClustersPerType = Generation::Data::get()->getTerrainMetaClusters();

	omniBin >> object.guid;
	omniBin >> object.type;
	auto&& metaClusters = metaClustersPerType[object.type];
	if (auto result = std::find_if(metaClusters.begin(), metaClusters.end(), [&](auto&& c) { return c->getGuid() == object.guid; }); result != metaClusters.end())
		object.metaCluster = *result;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotMetaCluster;
}