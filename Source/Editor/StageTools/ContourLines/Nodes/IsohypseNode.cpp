#include "stdafx.h"
#include "IsohypseNode.h"
#include "Omnigen.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

void IsohypseNode::makeSnapshot()
{
	IsohypseSnapshotData snapshotData;

	snapshotData.level = isohypse->getLevel();
	for(auto&& parent : isohypse->getParentIHs())
		snapshotData.parentGuids.insert(parent->getGuid());
	for(auto&& descendant : isohypse->getDescendants())
		snapshotData.descendants.push_back({ descendant.ih ? descendant.ih->getGuid() : -1, descendant.idx });
	for (auto&& preflows : isohypse->getPreflow())
	{
		std::vector<std::tuple<qint64, std::vector<int>>> dataPreflow;

		for (auto&& preflow : preflows)
			dataPreflow.push_back({ preflow.ih ? preflow.ih->getGuid() : -1, preflow.indices });

		snapshotData.preflow.push_back(dataPreflow);
	}
		
	auto&& verticesSpan = isohypse->getVertices();
	snapshotData.points = std::vector<QVector3D>(verticesSpan.begin(), verticesSpan.end());
	snapshotData.batchParams = isohypse->getBatchParams();

	IHProtoData protoData = isohypse->data;
	IHProtoSnapshotData ihProtoSnapshotData;

	ihProtoSnapshotData.boundsReached = protoData.boundsReached;
	ihProtoSnapshotData.slopeFactorForPeakApplied = protoData.slopeFactorForPeakApplied;
	ihProtoSnapshotData.originIdx = protoData.originIdx;
	ihProtoSnapshotData.lowestRidgeTier = protoData.lowestRidgeTier;
	ihProtoSnapshotData.height = protoData.height;
	ihProtoSnapshotData.mergeDistanceMult = protoData.mergeDistanceMult;
	ihProtoSnapshotData.heightAtHalfOfDistanceToBase = protoData.heightAtHalfOfDistanceToBase;
	ihProtoSnapshotData.distanceToBase = protoData.distanceToBase;
	ihProtoSnapshotData.groupingFactor = protoData.groupingFactor;
	ihProtoSnapshotData.usedDomainId = protoData.usedDomainId;
	for (auto&& source : protoData.sources)
		ihProtoSnapshotData.sources.push_back({ source.ih ? source.ih->getGuid() : -1, source.idx });
	ihProtoSnapshotData.modifiedBy = protoData.modifiedBy;
	for (auto&& [guid, bound] : protoData.bounds)
		ihProtoSnapshotData.bounds[guid] = bound.second;
	ihProtoSnapshotData.mergeIhlevels = protoData.mergeIhlevels;
	ihProtoSnapshotData.affectedBy = protoData.affectedBy;
	ihProtoSnapshotData.mergedDomains = protoData.mergedDomains;
	ihProtoSnapshotData.ridgeIds = protoData.ridgeIds;
	ihProtoSnapshotData.ridgelineSources = protoData.ridgelineSources;
	ihProtoSnapshotData.mergeThreshold = protoData.mergeThreshold;
	ihProtoSnapshotData.currentDropLvl = protoData.currentDropLvl;
	ihProtoSnapshotData.desiredDropLvl = protoData.desiredDropLvl;
	ihProtoSnapshotData.tablelandType = protoData.tablelandType;
	snapshotData.data = ihProtoSnapshotData;

	snapshotIsohypse = std::move(snapshotData);
}

void omniSave(const IsohypseSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.level;
	omniBin << object.parentGuids;
	omniBin << object.descendants;
	omniBin << object.preflow;
	omniBin << object.points;
	omniBin << object.batchParams;
	omniBin << object.data;
}

void omniLoad(IsohypseSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.level;
	omniBin >> object.parentGuids;
	omniBin >> object.descendants;
	omniBin >> object.preflow;
	omniBin >> object.points;
	omniBin >> object.batchParams;
	omniBin >> object.data;
}

void omniSave(const IsohypseNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotIsohypse;
}

void omniLoad(IsohypseNode& object, OmniBin<std::ios::in>& omniBin)
{
	auto&& instance = gBatchingMarkerInstance<IsohypseBatchParams>;
	
	omniBin >> object.guid;
	if (auto result = instance->findSectionByGuid(object.guid))
		object.isohypse = result;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotIsohypse;
}