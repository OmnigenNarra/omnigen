#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Editor/StageTools/Landmasses/Nodes/ShorelineNode.h"
#include "Editor/StageTools/Landmasses/Nodes/LandmassNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

struct WaterDomainSnapshotData
{
	QSet<GPoint> squares;
	EDomainType type;
	QSharedPointer<DomainDataBase> database;

	FRIEND_OMNIBIN(WaterDomainSnapshotData);
};

void omniSave(const WaterDomainSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(WaterDomainSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class WaterDomainNode : public StageObjectNode
{
public:
	WaterDomainNode(const QSharedPointer<DDomain>& domain)
		: waterDomain(domain)
		, guid(domain->getGuid())
	{}

	const auto& getDomain() const { return waterDomain; };
	const auto& getSnapshot() const { return snapshotWaterDomain; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setDomain(const QSharedPointer<DDomain>& inWaterDomain) { waterDomain = inWaterDomain; }
	void nullifyDomain() { waterDomain = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotWaterDomain.reset(); }

private:
	WaterDomainNode() = default;

	qint64 guid;
	QSharedPointer<DDomain> waterDomain;
	std::optional<WaterDomainSnapshotData> snapshotWaterDomain;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(WaterDomainNode);
};

void omniSave(const WaterDomainNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(WaterDomainNode& object, OmniBin<std::ios::in>& omniBin);