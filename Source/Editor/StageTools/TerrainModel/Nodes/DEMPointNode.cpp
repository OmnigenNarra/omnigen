#include "stdafx.h"
#include "DEMPointNode.h"

void DEMPointNode::makeSnapshot()
{
	auto&& dem = Generation::Data::get()->getDEM();
	IndexType index = dem->heightData.idx(*point);

	DEMPointSnapshotData snapshotData;

	snapshotData.heightData = dem->heightData.getGeometryRW()->vertices[index];
	snapshotData.levelData = dem->levelData.getGeometryRW()->vertices[index];
	snapshotData.verticalDisplacementXCoords = dem->verticalDisplacementXCoords.getGeometryRW()->vertices[index];

	snapshotDEMPoint = std::move(snapshotData);
}

void omniSave(const DEMPointSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.heightData;
	omniBin << object.levelData;
	omniBin << object.verticalDisplacementXCoords;
}

void omniLoad(DEMPointSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.heightData;
	omniBin >> object.levelData;
	omniBin >> object.verticalDisplacementXCoords;
}

void omniSave(const DEMPointNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.point;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotDEMPoint;
	omniBin << object.cellNode;
}

void omniLoad(DEMPointNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.point;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotDEMPoint;
	omniBin >> object.cellNode;
}