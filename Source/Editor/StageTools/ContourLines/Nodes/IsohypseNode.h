#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

struct IHProtoSnapshotData
{
    bool boundsReached;
    bool slopeFactorForPeakApplied;
    int originIdx;
    int lowestRidgeTier;
    float height;
    float mergeDistanceMult;
    float heightAtHalfOfDistanceToBase;
    float distanceToBase;
    float groupingFactor;
    qint64 usedDomainId;
	std::vector<std::tuple<qint64, int>> sources;
    std::vector<std::pair<float, bool>> modifiedBy;
    std::unordered_map<qint64, EIHGenerationStage> bounds;
    std::vector<int> mergeIhlevels;
	std::unordered_map<EIHAffectType, std::unordered_set<qint64>> affectedBy;
    std::set<qint64> mergedDomains;
    std::unordered_set<qint64> ridgeIds;
    std::unordered_map<int, QVector3D> ridgelineSources;
    std::optional<float> mergeThreshold;
    std::optional<int> currentDropLvl;
    std::optional<int> desiredDropLvl;
    std::optional<ETableLand> tablelandType;
};

struct IsohypseSnapshotData
{
	int level;
	std::set<qint64> parentGuids;
	std::vector<std::tuple<qint64, int>> descendants;
	std::vector<std::vector<std::tuple<qint64, std::vector<int>>>> preflow;
	std::vector<QVector3D> points;
	IsohypseBatchParams batchParams;
	IHProtoSnapshotData data;

	FRIEND_OMNIBIN(IsohypseSnapshotData);
};

void omniSave(const IsohypseSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(IsohypseSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class IsohypseNode : public StageObjectNode, public QEnableSharedFromThis<IsohypseNode>
{
public:
	IsohypseNode(const QSharedPointer<Isohypse>& inIsohypse)
		: isohypse(inIsohypse)
		, guid(inIsohypse->getGuid())
	{}

	const auto& getIsohypse() const { return isohypse; };
	const auto& getSnapshot() const { return snapshotIsohypse; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setIsohypse(const QSharedPointer<Isohypse>& inIsohypse) { isohypse = inIsohypse; }
	void nullifyIsohypse() { isohypse = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotIsohypse.reset(); }

private:
	IsohypseNode() = default;

	qint64 guid;
	QSharedPointer<Isohypse> isohypse;
	std::optional<IsohypseSnapshotData> snapshotIsohypse;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(IsohypseNode);
};

void omniSave(const IsohypseNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(IsohypseNode& object, OmniBin<std::ios::in>& omniBin);