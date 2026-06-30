#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/Landmasses/LandmassMarker.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"

struct LandmassSnapshotData
{
	QString name;
	bool coast;
	QSet<GPoint> squares;
	std::vector<qint64> shorelines;
	std::vector<qint64> innerSeaShorelines;
	std::vector<QVector3D> mainPolygon;
	std::vector<std::vector<QVector3D>> cutPolygons;

	FRIEND_OMNIBIN(LandmassSnapshotData);
};

void omniSave(const LandmassSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(LandmassSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class LandmassNode : public StageObjectNode, public QEnableSharedFromThis<LandmassNode>
{
public:
	LandmassNode(const QSharedPointer<DLandmassMarker>& inLandmass)
		: landmass(inLandmass)
		, guid(inLandmass->getGuid())
	{}

	const auto& getLandmass() const { return landmass; };
	const auto& getSnapshot() const { return snapshotLandmass; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setLandmass(const QSharedPointer<DLandmassMarker>& inLandmass) { landmass = inLandmass; }
	void nullifyLandmass() { landmass = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotLandmass.reset(); }

private:
	LandmassNode() = default;

	qint64 guid;
	QSharedPointer<DLandmassMarker> landmass;
	std::optional<LandmassSnapshotData> snapshotLandmass;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(LandmassNode);
};

void omniSave(const LandmassNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(LandmassNode& object, OmniBin<std::ios::in>& omniBin);