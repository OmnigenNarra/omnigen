#include "stdafx.h"
#include "BiomeDomainNode.h"
#include "Scene/Generation/OmnigenGenerationData.h"

void BiomeDomainNode::makeSnapshot()
{
	BiomeDomainSnapshotData snapshotData;
	snapshotData.type = biomeDomain->getType();
	snapshotData.squares = biomeDomain->getSquares();
	snapshotData.database = QSharedPointer<DomainData<EDomainType::Biome>>::create(*biomeDomain->getData<EDomainType::Biome>().data());

	snapshotBiomeDomain = std::move(snapshotData);
}

void omniSave(const BiomeDomainSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.squares;
	omniBin << object.type;
	EDomainTypeConstexpr::UseIn<EAC::SaveDomainData>(object.type, &object.database, omniBin);
}

void omniLoad(BiomeDomainSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.squares;
	omniBin >> object.type;
	EDomainTypeConstexpr::UseIn<EAC::LoadDomainData>(object.type, &object.database, omniBin);
}

void omniSave(const BiomeDomainNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotBiomeDomain;
}

void omniLoad(BiomeDomainNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.guid;
	if (auto&& domain = Generation::Data::get()->findDomainByGuid(object.guid))
		object.biomeDomain = *domain;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotBiomeDomain;
}