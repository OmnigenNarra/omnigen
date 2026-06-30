#include "stdafx.h"
#include "ShorelineNode.h"
#include "Omnigen.h"

void ShorelineNode::makeSnapshot()
{
	ShorelineSnapshotData snapshotData;
	snapshotData.name = shoreline->getName();
	snapshotData.isCoast = shoreline->getLandmass().lock()->isCoast();
	snapshotData.segmentWidth = shoreline->getSegmentWidth();
	snapshotData.baysRoot = shoreline->getBays();
	snapshotData.peninsulasRoot = shoreline->getPeninsulas();
	snapshotData.squares = shoreline->getSquares();
	snapshotData.points = shoreline->getControlPoints();
	snapshotData.shorelineHeights = shoreline->getHeights();

	snapshotShoreline = std::move(snapshotData);
}

void omniSave(const ShorelineSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.name;
	omniBin << object.isCoast;
	omniBin << object.segmentWidth;
	omniBin << object.baysRoot;
	omniBin << object.peninsulasRoot;
	omniBin << object.squares;
	omniBin << object.points;
	omniBin << object.shorelineHeights;
}

void omniLoad(ShorelineSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.name;
	omniBin >> object.isCoast;
	omniBin >> object.segmentWidth;
	omniBin >> object.baysRoot;
	omniBin >> object.peninsulasRoot;
	omniBin >> object.squares;
	omniBin >> object.points;
	omniBin >> object.shorelineHeights;
}

void omniSave(const ShorelineNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotShoreline;
}

void omniLoad(ShorelineNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.guid;
	object.shoreline = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(object.guid);
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotShoreline;
}