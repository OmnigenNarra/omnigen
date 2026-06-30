#pragma once
#include "Editor/StageTools/StageObjectNode.h"
#include "Editor/StageTools/Landmasses/Nodes/ShorelineNode.h"
#include "Editor/StageTools/Landmasses/Nodes/LandmassNode.h"
#include "Editor/StageTools/ContourLines/Nodes/IsohypseNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

class DomainSquareNode : public StageObjectNode
{
public:
	DomainSquareNode(const GPoint& inSquare)
		: square(inSquare)
	{}

	const auto& getSquare() const { return square; }
	const auto& isCreatedOnCurrentStage() const { return createdOnCurrentStage; }
	const auto& isModifiedOnCurrentStage() const { return modifiedOnCurrentStage; }
	const auto& getShorelineNodes() const { return shorelineNodes; }
	const auto& getLandmassNodes() const { return landmassNodes; }
	const auto& getRidgeNodes() const { return ridgeNodes; }
	const auto& getIsohypseNodes() const { return isohypseNodes; }

	void setSquare(const GPoint& inSquare) { square = inSquare; }
	void nullifySquare() { square = std::nullopt; }
	void setCreatedOnCurrentStage(bool inCreatedOnCurrentStage) { createdOnCurrentStage = inCreatedOnCurrentStage; }
	void setModifiedOnCurrentStage(bool inModifiedOnCurrentStage) { modifiedOnCurrentStage = inModifiedOnCurrentStage; }

	void addShorelineNode(const QSharedPointer<DShorelineMarker>& shoreline) { shorelineNodes += shoreline->getGuid(); }
	void addLandmassNode(const QSharedPointer<DLandmassMarker>& landmass) { landmassNodes += landmass->getGuid(); }
	void addRidgeNode(const QSharedPointer<DRidgeMarker>& ridge) { ridgeNodes += ridge->getGuid(); }
	void addIsohypseNode(const QSharedPointer<Isohypse>& isohypse) { isohypseNodes += isohypse->getGuid(); }

	void clearShorelineNodes() { shorelineNodes.clear(); }
	void clearLandmassNodes() { landmassNodes.clear(); }
	void clearRidgeNodes() { ridgeNodes.clear(); }
	void clearIsohypseNodes() { isohypseNodes.clear(); }

private:
	DomainSquareNode() = default;

	std::optional<GPoint> square;
	bool createdOnCurrentStage = true;
	bool modifiedOnCurrentStage = false;

	std::unordered_set<qint64> shorelineNodes;
	std::unordered_set<qint64> landmassNodes;
	std::unordered_set<qint64> ridgeNodes;
	std::unordered_set<qint64> isohypseNodes;

	FRIEND_OMNIBIN(DomainSquareNode);
};

void omniSave(const DomainSquareNode& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DomainSquareNode& object, OmniBin<std::ios::in>& omniBin);