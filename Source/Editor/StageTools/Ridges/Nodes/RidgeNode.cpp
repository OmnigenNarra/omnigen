#include "stdafx.h"
#include "RidgeNode.h"
#include "Omnigen.h"

void store(std::vector<qint64>* data, std::vector<int>* parents, const QSharedPointer<DRidgeMarker>& currentNode)
{
	data->push_back(currentNode->getGuid());
	parents->push_back(currentNode->getParent() ? indexOf(*data, currentNode->getParent().lock()->getGuid()) : -1);

	for (auto&& child : currentNode->getChildren())
		store(data, parents, child);
}

void RidgeNode::makeSnapshot()
{
	RidgeSnapshotData snapshotData;
	snapshotData.name = ridge->getName();
	snapshotData.parentGuid = ridge->getParent() ? ridge->getParent().lock()->getGuid() : -1;
	snapshotData.sourcePointIdx = ridge->getSourcePointIdx();
	snapshotData.segmentWidth = ridge->getSegmentWidth();
	snapshotData.slopeVariation = std::pair<float, float>{ ridge->getLeftSlopeFactor(), ridge->getRightSlopeFactor() };
	snapshotData.tablelandType = ridge->getTablelandType();
	snapshotData.ridgelineHeight = ridge->getHeights();
	snapshotData.points = ridge->getControlPoints();
	snapshotData.squares = ridge->getSquares();
	if (!ridge->getParent())
		store(&snapshotData.treeData, &snapshotData.treeParents, ridge);

	snapshotRidge = std::move(snapshotData);
}

void omniSave(const RidgeSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.name;
	omniBin << object.parentGuid;
	omniBin << object.sourcePointIdx;
	omniBin << object.segmentWidth;
	omniBin << object.slopeVariation;
	omniBin << object.tablelandType;
	omniBin << object.ridgelineHeight;
	omniBin << object.points;
	omniBin << object.squares;
	omniBin << object.treeData;
	omniBin << object.treeParents;
}

void omniLoad(RidgeSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.name;
	omniBin >> object.parentGuid;
	omniBin >> object.sourcePointIdx;
	omniBin >> object.segmentWidth;
	omniBin >> object.slopeVariation;
	omniBin >> object.tablelandType;
	omniBin >> object.ridgelineHeight;
	omniBin >> object.points;
	omniBin >> object.squares;
	omniBin >> object.treeData;
	omniBin >> object.treeParents;
}

void omniSave(const RidgeNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotRidge;
	omniBin << object.isohypseNodes;
}

void omniLoad(RidgeNode& object, OmniBin<std::ios::in>& omniBin)
{
	std::vector<QSharedPointer<DRidgeMarker>> ridges;
	Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

	omniBin >> object.guid;
	if (auto result = std::find_if(ridges.begin(), ridges.end(), [&](auto& r) { return r->getGuid() == object.guid; }); result != ridges.end())
		object.ridge = *result;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotRidge;
	omniBin >> object.isohypseNodes;
}