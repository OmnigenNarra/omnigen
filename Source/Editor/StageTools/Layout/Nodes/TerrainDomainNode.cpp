#include "stdafx.h"
#include "TerrainDomainNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"

void TerrainDomainNode::makeSnapshot()
{
	TerrainDomainSnapshotData snapshotData;
	snapshotData.type = terrainDomain->getType();
	snapshotData.squares = terrainDomain->getSquares();
	snapshotData.database = QSharedPointer<DomainData<EDomainType::Terrain>>::create(*terrainDomain->getData<EDomainType::Terrain>().data());

	snapshotTerrainDomain = std::move(snapshotData);
}

void omniSave(const TerrainDomainSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.squares;
	omniBin << object.type;
	EDomainTypeConstexpr::UseIn<EAC::SaveDomainData>(object.type, &object.database, omniBin);
}

void omniLoad(TerrainDomainSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.squares;
	omniBin >> object.type;
	EDomainTypeConstexpr::UseIn<EAC::LoadDomainData>(object.type, &object.database, omniBin);
}

void omniSave(const TerrainDomainNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotTerrainDomain;
}

void omniLoad(TerrainDomainNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.guid;
	if (auto&& domain = Generation::Data::get()->findDomainByGuid(object.guid))
		object.terrainDomain = *domain;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotTerrainDomain;
}