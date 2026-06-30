#include "stdafx.h"
#include "CellNode.h"
#include "Scene/Generation/OmnigenGenerationData.h"

void CellNode::makeSnapshot()
{
	auto&& diagram = Generation::Data::get()->getTerrainCells();
	auto&& [lithoMap, lithoCluster] = Generation::Data::get()->getLithomap();
	auto&& blockMap = Generation::Data::get()->getBlockTypeMap();
	auto&& cellIdx = diagram->getCellIndexFromCenter(*cellCenter);

	CellSnapshotData snapshotData;
	snapshotData.lithoType = lithoCluster.at(lithoMap.at(cellIdx))->getType();
	snapshotData.blockType = !blockMap.empty() ? blockMap.at(cellIdx) : Generation::ETerrainBlock::Last;

	snapshotCell = std::move(snapshotData);
}

void CellNode::updateLithoSnapshot()
{
	auto&& diagram = Generation::Data::get()->getTerrainCells();
	auto&& [lithoMap, lithoCluster] = Generation::Data::get()->getLithomap();
	auto&& cellIdx = diagram->getCellIndexFromCenter(*cellCenter);

	snapshotCell->lithoType = lithoCluster.at(lithoMap.at(cellIdx))->getType();
}

void omniSave(const CellNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.cellCenter;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotCell;
	omniBin << object.clusterNode;
	omniBin << object.metaClusterNode;
}

void omniLoad(CellNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.cellCenter;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotCell;
	omniBin >> object.clusterNode;
	omniBin >> object.metaClusterNode;
}