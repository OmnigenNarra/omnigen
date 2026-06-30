#include "stdafx.h"
#include "LandmassNode.h"
#include "Omnigen.h"

void LandmassNode::makeSnapshot()
{
	LandmassSnapshotData snapshotData;
	snapshotData.name = landmass->getName();
	snapshotData.coast = landmass->isCoast();
	snapshotData.squares = landmass->getSquares();
	landmass->forEachShoreline([&](auto& s, bool isInner)
		{
			if (!isInner)
				snapshotData.shorelines << s->getGuid();
			else
				snapshotData.innerSeaShorelines << s->getGuid();
		});
	snapshotData.mainPolygon = landmass->getMainPolygon();
	snapshotData.cutPolygons = landmass->getCutPolygons();

	snapshotLandmass = std::move(snapshotData);
}

void omniSave(const LandmassSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.name;
	omniBin << object.coast;
	omniBin << object.squares;
	omniBin << object.shorelines;
	omniBin << object.innerSeaShorelines;
	omniBin << object.mainPolygon;
	omniBin << object.cutPolygons;
}

void omniLoad(LandmassSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.name;
	omniBin >> object.coast;
	omniBin >> object.squares;
	omniBin >> object.shorelines;
	omniBin >> object.innerSeaShorelines;
	omniBin >> object.mainPolygon;
	omniBin >> object.cutPolygons;
}

void omniSave(const LandmassNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotLandmass;
}

void omniLoad(LandmassNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.guid;
	object.landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(object.guid);
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotLandmass;
}