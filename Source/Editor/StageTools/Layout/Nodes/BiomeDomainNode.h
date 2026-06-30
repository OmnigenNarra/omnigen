#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

struct BiomeDomainSnapshotData
{
	QSet<GPoint> squares;
	EDomainType type;
	QSharedPointer<DomainDataBase> database;

	FRIEND_OMNIBIN(BiomeDomainSnapshotData);
};

void omniSave(const BiomeDomainSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(BiomeDomainSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class BiomeDomainNode : public StageObjectNode
{
public:
	BiomeDomainNode(const QSharedPointer<DDomain>& domain)
		: biomeDomain(domain)
		, guid(domain->getGuid())
	{}

	const auto& getDomain() const { return biomeDomain; };
	const auto& getSnapshot() const { return snapshotBiomeDomain; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setDomain(const QSharedPointer<DDomain>& inBiomeDomain) { biomeDomain = inBiomeDomain; }
	void nullifyDomain() { biomeDomain = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotBiomeDomain.reset(); }

private:
	BiomeDomainNode() = default;

	qint64 guid;
	QSharedPointer<DDomain> biomeDomain;
	std::optional<BiomeDomainSnapshotData> snapshotBiomeDomain;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(BiomeDomainNode);
};

void omniSave(const BiomeDomainNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(BiomeDomainNode& object, OmniBin<std::ios::in>& omniBin);