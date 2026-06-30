#include "stdafx.h"
#include "IHSelection.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"
#include "../StageTools.h"

namespace Design
{
	bool IHSelection::findOnScene(QMap<EIHSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
	{
		auto&& ridgeTools = getStageTools<EGenerationStage::ContourLines>();

		// Skip selection while editing
// 		if (ridgeTools->bIsRidgeEditing || ridgeTools->bHeightEditing)
// 			return false;

//		std::vector<QSharedPointer<Isohypse>> ihs = ...

		QMouseEvent* dummyEvent = new QMouseEvent(QEvent::None, QPointF(x, y), QPointF(0, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
// 		auto&& results = ridgeTools->findClosestRidgeline(dummyEvent);
// 
// 		if (results)
// 		{
// 			auto guid = results->first;
// 			auto&& ridgeIter = std::find_if(ihs.begin(), ihs.end(), [guid](auto&& ele) {return ele->getGuid() == guid; });
// 			(*output)[EIHSelection::IHs] = *ridgeIter;
// 			return true;
// 		}

		return false;
	}

	void IHSelection::hoverUpdate(const std::any& data, bool isLive)
	{
// 		auto&& ridgeTools = getStageTools<EGenerationStage::Ridges>();
// 
// 		if (!isLive || ridgeTools->bIsRidgeEditing || ridgeTools->bBlockHover || ridgeTools->bHeightEditing)
// 		{
// 			if (currentHover)
// 				currentHover.lock()->setHovered(isLive);
// 
// 			currentHover.clear();
// 			return;
// 		}
// 
// 		currentHover = std::any_cast<QSharedPointer<Isohypse>>(data);
// 		currentHover.lock()->setHovered(isLive);
	}

	QMenu* IHSelection::requestContextMenu(const std::any& data)
	{
		auto&& ridgeTools = getStageTools<EGenerationStage::Ridges>();

		QMenu* menu = new QMenu(Omnigen::get());

		menu->addAction(ridgeTools->actions[ERidgeAction::DeleteSelectedRidge]);

		return menu;
	}

	void IHSelection::getData(const SelectionBase* obj, QSet<DataType>* data)
	{
		(*data) += static_cast<const IHSelection*>(obj)->ih;
	}

	std::vector<QSharedPointer<SelectionBase>> IHSelection::createFromData(const QSet<DataType>& inRidges)
	{
		std::vector<QSharedPointer<SelectionBase>> results;

		for (auto&& ridge : inRidges)
		{
			auto sel = QSharedPointer<IHSelection>::create();
			sel->ih = ridge;
			sel->select();
			results << sel;
		}

		return results;
	}

	IHSelection::Selection(const std::any& inRidge)
		: ih(std::any_cast<QSharedPointer<Isohypse>>(inRidge))
	{
		if (!bSubtract)
			select();
	}

	QSharedPointer<OmnigenPropertyListBase> IHSelection::makePropertyList()
	{
		Q_ASSERT(false);
		return {};
	}

	void IHSelection::update(const std::any& newRidge, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
	{
		if (!bSubtract)
			return;

		// Deselect all matches
		for (int i = 0; i < currentSelections->size(); ++i)
		{
			auto* ridgeSel = static_cast<IHSelection*>(currentSelections->at(i).get());
			if (ridgeSel->ih->getGuid() == ih->getGuid())
			{
				ridgeSel->deselect();
				currentSelections->erase(currentSelections->begin() + i--);
			}
		}
	}

	void IHSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
	{
		if (bSubtract)
			return;

		currentSelections->emplace_back(sharedFromThis());
	}

	QVector3D IHSelection::getPosition() const
	{
		auto pts = ih->getCircularPoints();
		return pts[pts.getSize() / 2];
	}

	void IHSelection::select() const
	{
		//ih->setSelected(true);
	}

	void IHSelection::deselect() const
	{
		//ih->setSelected(false);
	}
}