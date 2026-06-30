#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Editor/StageTools/ContourLines/Nodes/IsohypseNode.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"

struct RidgeSnapshotData
{
	QString name;
	qint64 parentGuid;
	int sourcePointIdx;
	float segmentWidth;
	std::optional<ETableLand> tablelandType;
	std::pair<float, float> slopeVariation;
	std::vector<float> ridgelineHeight;
	std::vector<QVector3D> points;
	QSet<GPoint> squares;
	// tree properties of ridge
	std::vector<qint64> treeData;
	std::vector<int> treeParents;

	FRIEND_OMNIBIN(RidgeSnapshotData);
};

void omniSave(const RidgeSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(RidgeSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class RidgeNode : public StageObjectNode, public QEnableSharedFromThis<RidgeNode>
{
public:
	RidgeNode(const QSharedPointer<DRidgeMarker>& inRidge)
		: ridge(inRidge)
		, guid(inRidge->getGuid())
	{}

	const auto& getRidge() const { return ridge; };
	const auto& getSnapshot() const { return snapshotRidge; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }
	const auto& getIsohypseNodes() const { return isohypseNodes; }

	void setRidge(const QSharedPointer<DRidgeMarker>& inRidge) { ridge = inRidge; }
	void nullifyLandmass() { ridge = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotRidge.reset(); }

	void addIsohypseNode(const QSharedPointer<Isohypse>& isohypse) { isohypseNodes += isohypse->getGuid(); }

	void clearIsohypseNodes() { isohypseNodes.clear(); }

private:
	RidgeNode() = default;

	qint64 guid;
	QSharedPointer<DRidgeMarker> ridge;
	std::optional<RidgeSnapshotData> snapshotRidge;

	bool createdOnCurrentStage = true;

	std::unordered_set<qint64> isohypseNodes;

	FRIEND_OMNIBIN(RidgeNode);
};

void omniSave(const RidgeNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(RidgeNode& object, OmniBin<std::ios::in>& omniBin);