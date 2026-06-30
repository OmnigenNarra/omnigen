#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Editor/StageTools/Landmasses/Nodes/ShorelineNode.h"
#include "Editor/StageTools/Landmasses/Nodes/LandmassNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

struct TerrainDomainSnapshotData
{
	QSet<GPoint> squares;
	EDomainType type;
	QSharedPointer<DomainDataBase> database;

	FRIEND_OMNIBIN(TerrainDomainSnapshotData);
};

void omniSave(const TerrainDomainSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(TerrainDomainSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class TerrainDomainNode : public StageObjectNode
{
public:
	TerrainDomainNode(const QSharedPointer<DDomain>& domain)
		: terrainDomain(domain)
		, guid(domain->getGuid())
	{}

	const auto& getDomain() const { return terrainDomain; }
	const auto& getSnapshot() const { return snapshotTerrainDomain; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setDomain(const QSharedPointer<DDomain>& inTerrainDomain) { terrainDomain = inTerrainDomain; }
	void nullifyDomain() { terrainDomain = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotTerrainDomain.reset(); }

private:
	TerrainDomainNode() = default;

	qint64 guid;
	QSharedPointer<DDomain> terrainDomain;
	std::optional<TerrainDomainSnapshotData> snapshotTerrainDomain;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(TerrainDomainNode);
};

void omniSave(const TerrainDomainNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(TerrainDomainNode& object, OmniBin<std::ios::in>& omniBin);