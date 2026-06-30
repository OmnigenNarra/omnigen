#include "stdafx.h"
#include "WaterDomainNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"

void WaterDomainNode::makeSnapshot()
{
	WaterDomainSnapshotData snapshotData;
	snapshotData.type = waterDomain->getType();
	snapshotData.squares = waterDomain->getSquares();
	snapshotData.database = QSharedPointer<DomainData<EDomainType::Water>>::create(*waterDomain->getData<EDomainType::Water>().data());

	snapshotWaterDomain = std::move(snapshotData);
}

void omniSave(const WaterDomainSnapshotData& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.squares;
	omniBin << object.type;
	EDomainTypeConstexpr::UseIn<EAC::SaveDomainData>(object.type, &object.database, omniBin);
}

void omniLoad(WaterDomainSnapshotData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.squares;
	omniBin >> object.type;
	EDomainTypeConstexpr::UseIn<EAC::LoadDomainData>(object.type, &object.database, omniBin);
}

void omniSave(const WaterDomainNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.snapshotWaterDomain;
}

void omniLoad(WaterDomainNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.guid;
	if (auto&& domain = Generation::Data::get()->findDomainByGuid(object.guid))
		object.waterDomain = *domain;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.snapshotWaterDomain;
}