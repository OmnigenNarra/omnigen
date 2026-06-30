#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"

struct ShorelineSnapshotData
{
	QString name;
	bool isCoast;
	float segmentWidth;
	QSharedPointer<BayNode> baysRoot;
	QSharedPointer<BayNode> peninsulasRoot;
	QSet<GPoint> squares;
	std::vector<QVector3D> points;
	std::vector<float> shorelineHeights;

	FRIEND_OMNIBIN(ShorelineSnapshotData);
};

void omniSave(const ShorelineSnapshotData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(ShorelineSnapshotData& object, OmniBin<std::ios::in>& omniBin);

class ShorelineNode : public StageObjectNode , public QEnableSharedFromThis<ShorelineNode>
{
public:
	ShorelineNode(const QSharedPointer<DShorelineMarker>& inShoreline)
		: shoreline(inShoreline)
		, guid(inShoreline->getGuid())
	{}

	const auto& getShoreline() const { return shoreline; };
	const auto& getSnapshot() const { return snapshotShoreline; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }

	void setShoreline(const QSharedPointer<DShorelineMarker>& inShoreline) { shoreline = inShoreline; }
	void nullifyShoreline() { shoreline = nullptr; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }

	void makeSnapshot();
	void clearSnapshot() { snapshotShoreline.reset(); }

private:
	ShorelineNode() = default;

	qint64 guid;
	QSharedPointer<DShorelineMarker> shoreline;
	std::optional<ShorelineSnapshotData> snapshotShoreline;

	bool createdOnCurrentStage = true;

	FRIEND_OMNIBIN(ShorelineNode);
};

void omniSave(const ShorelineNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(ShorelineNode& object, OmniBin<std::ios::in>& omniBin);