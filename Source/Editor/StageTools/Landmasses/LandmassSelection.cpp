#include "stdafx.h"
#include "LandmassSelection.h"
#include "Utils/PlatformMisc.h"
#include "../StageTools.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"

namespace Design
{
    bool LandmassSelection::findOnScene(QMap<ELandmassSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto landmass = SelectionMgrBase::findObjectUnderCursor<DLandmassMarker>(screenData);
        if (landmass)
        {
            (*output)[ELandmassSelection::Landmass] = landmass->object;
            return true;
        }

        return false;
    }

    void LandmassSelection::hoverUpdate(const std::any& data, bool isLive)
    {
        auto&& landmassTools = getStageTools<EGenerationStage::Landmasses>();

        if (!isLive || landmassTools->isLandmassEditing || landmassTools->isLandmassSpawning)
        {
            if (currentHover)
                currentHover.lock()->setHovered(isLive);

            currentHover.clear();
            return;
        }

        currentHover = std::any_cast<QSharedPointer<DLandmassMarker>>(data);
        currentHover.lock()->setHovered(isLive);
    }

    QMenu* LandmassSelection::requestContextMenu(const std::any& data)
    {
        auto&& landmassTools = getStageTools<EGenerationStage::Landmasses>();

        QMenu* menu = new QMenu(Omnigen::get());

        menu->addAction(landmassTools->actions[ELandmassAction::DeleteSelectedLandmasses]);

        return menu;
    }

    void LandmassSelection::getData(const SelectionBase* obj, QSet<QSharedPointer<DLandmassMarker>>* data)
    {
        (*data) += static_cast<const LandmassSelection*>(obj)->landmassPtr;
    }

    std::vector<QSharedPointer<SelectionBase>> LandmassSelection::createFromData(const QSet<QSharedPointer<DLandmassMarker>>& inLandmasses)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& landmass : inLandmasses)
        {
            auto sel = QSharedPointer<LandmassSelection>::create();
            sel->landmassPtr = landmass;
            sel->select();
            results << sel;
        }

        return results;
    }

    bool LandmassSelection::isLandmassHovered(const QSharedPointer<DLandmassMarker>& landmass)
    {
        return currentHover.lock() == landmass;
    }

    LandmassSelection::Selection(const std::any& inLandmass)
        : landmassPtr(std::any_cast<QSharedPointer<DLandmassMarker>>(inLandmass))
    {
        if (!bSubtract)
            select();
    }

    QSharedPointer<OmnigenPropertyListBase> LandmassSelection::makePropertyList()
    {
        QSharedPointer<DLandmassMarker> marker = landmassPtr;

        auto props = QSharedPointer<OmnigenPropertyListBase>::create(marker->getGuid(), sharedFromThis());

        return props;
    }

    void LandmassSelection::update(const std::any& newLandmass, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        if (!bSubtract)
            return;

        // Deselect all matches
        for (int i = 0; i < currentSelections->size(); ++i)
        {
            auto* landmassSel = static_cast<LandmassSelection*>(currentSelections->at(i).get());
            if (landmassSel->landmassPtr->getGuid() == landmassPtr->getGuid())
            {
                landmassSel->deselect();
                currentSelections->erase(currentSelections->begin() + i--);
            }
        }
    }

    void LandmassSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D LandmassSelection::getPosition() const
    {
        auto&& pts = landmassPtr->getMainPolygon();
        return std::accumulate(pts.begin(), pts.end(), QVector3D()) / float(pts.size());
    }

    void LandmassSelection::select() const
    {
        landmassPtr->setSelected(true);
    }

    void LandmassSelection::deselect() const
    {
        landmassPtr->setSelected(false);
    }

    bool ShorelineSelection::findOnScene(QMap<ELandmassSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto shoreline = SelectionMgrBase::findObjectUnderCursor<DShorelineMarker>(screenData);
        if (shoreline)
        {
            (*output)[ELandmassSelection::Shoreline] = shoreline->object;
            return true;
        }

        return false;
    }

    void ShorelineSelection::hoverUpdate(const std::any& data, bool isLive)
    {
        auto&& landmassTools = getStageTools<EGenerationStage::Landmasses>();

        if (!isLive || landmassTools->isLandmassEditing || landmassTools->isLandmassSpawning)
        {
            if(currentHover)
                currentHover.lock()->setHovered(isLive);

            currentHover.clear();
            return;
        }

        currentHover = std::any_cast<QSharedPointer<DShorelineMarker>>(data);
        currentHover.lock()->setHovered(isLive);
    }

    QMenu* ShorelineSelection::requestContextMenu(const std::any& data)
    {
        auto&& landmassTools = getStageTools<EGenerationStage::Landmasses>();
        
        QMenu* menu = new QMenu(Omnigen::get());

        menu->addAction(landmassTools->actions[ELandmassAction::ReshapeShoreline]);

        return menu;
    }

    void ShorelineSelection::getData(const SelectionBase* obj, QSet<QSharedPointer<DShorelineMarker>>* data)
    {
        (*data) += static_cast<const ShorelineSelection*>(obj)->shorelinePtr;
    }

    std::vector<QSharedPointer<SelectionBase>> ShorelineSelection::createFromData(const QSet<QSharedPointer<DShorelineMarker>>& inShorelines)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& shoreline : inShorelines)
        {
            auto sel = QSharedPointer<ShorelineSelection>::create();
            sel->shorelinePtr = shoreline;
            sel->select();
            results << sel;
        }

        return results;
    }

    bool ShorelineSelection::isShorelineHovered(const QSharedPointer<DShorelineMarker>& shoreline)
    {
        return currentHover.lock() == shoreline;
    }

    ShorelineSelection::Selection(const std::any& inShoreline)
        : shorelinePtr(std::any_cast<QSharedPointer<DShorelineMarker>>(inShoreline))
    {
        if (!bSubtract)
            select();
    }

    QSharedPointer<OmnigenPropertyListBase> ShorelineSelection::makePropertyList()
    {
        QSharedPointer<DShorelineMarker> marker = shorelinePtr;

        auto props = QSharedPointer<OmnigenPropertyListBase>::create(marker->getGuid(), sharedFromThis());


        return props;
    }

    void ShorelineSelection::update(const std::any& newShoreline, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        if (!bSubtract)
            return;

        // Deselect all matches
        for (int i = 0; i < currentSelections->size(); ++i)
        {
            auto* shorelineSel = static_cast<ShorelineSelection*>(currentSelections->at(i).get());
            if (shorelineSel->shorelinePtr->getGuid() == shorelinePtr->getGuid())
            {
                shorelineSel->deselect();
                currentSelections->erase(currentSelections->begin() + i--);
            }
        }
    }

    void ShorelineSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D ShorelineSelection::getPosition() const
    {
        return shorelinePtr->getControlPoints()[(shorelinePtr->getControlPoints().size() / 2)];
    }

    void ShorelineSelection::select() const
    {
        shorelinePtr->setSelected(true);
    }

    void ShorelineSelection::deselect() const
    {
        shorelinePtr->setSelected(false);
    }
}