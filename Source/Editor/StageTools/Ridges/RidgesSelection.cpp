#include "stdafx.h"
#include "RidgesSelection.h"
#include "Utils/PlatformMisc.h"
#include "../StageTools.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Source/Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

namespace Design
{
    bool RidgeSelection::findOnScene(QMap<ERidgesSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto ridge = SelectionMgrBase::findObjectUnderCursor<DRidgeMarker>(screenData);
        if(ridge)
        {
            (*output)[ERidgesSelection::Ridge] = ridge->object;
            return true;
        }

        return false;
    }

    void RidgeSelection::hoverUpdate(const std::any& data, bool isLive)
    {
        auto&& ridgeTools = getStageTools<EGenerationStage::Ridges>();

        if (!isLive || ridgeTools->bIsRidgeEditing || ridgeTools->bBlockHover || ridgeTools->bHeightEditing)
        {
            if(currentHover)
                currentHover.lock()->setHovered(isLive);

            currentHover.clear();
            return;
        }

        currentHover = std::any_cast<QSharedPointer<DRidgeMarker>>(data);
        currentHover.lock()->setHovered(isLive);
    }

    QMenu* RidgeSelection::requestContextMenu(const std::any& data)
    {
        auto&& ridgeTools = getStageTools<EGenerationStage::Ridges>();
        
        QMenu* menu = new QMenu(Omnigen::get());

        menu->addAction(ridgeTools->actions[ERidgeAction::DeleteSelectedRidge]);

        return menu;
    }

    void RidgeSelection::getData(const SelectionBase* obj, QSet<QSharedPointer<DRidgeMarker>>* data)
    {
        (*data) += static_cast<const RidgeSelection*>(obj)->ridgePtr;
    }

    std::vector<QSharedPointer<SelectionBase>> RidgeSelection::createFromData(const QSet<QSharedPointer<DRidgeMarker>>& inRidges)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& ridge : inRidges)
        {
            auto sel = QSharedPointer<RidgeSelection>::create();
            sel->ridgePtr = ridge;
            sel->select();
            results << sel;
        }

        return results;
    }

    bool RidgeSelection::isRidgeHovered(const QSharedPointer<DRidgeMarker>& ridge)
    {
        return currentHover.lock() == ridge;
    }

    RidgeSelection::Selection(const std::any& inRidge)
        : ridgePtr(std::any_cast<QSharedPointer<DRidgeMarker>>(inRidge))
    {
        if (!bSubtract)
            select();
    }

    QSharedPointer<OmnigenPropertyListBase> RidgeSelection::makePropertyList()
    {
        std::vector<qint64> guidChain;
        QSharedPointer<DRidgeMarker> marker = ridgePtr;

        // Create guid chain for lookup
        while (true)
        {
            guidChain.emplace_back(marker->getGuid());
            if (marker->getParent())
                marker = marker->getParent().lock();
            else
                break;
        }

        auto props = QSharedPointer<OmnigenPropertyListBase>::create(guidChain.front(), sharedFromThis());
        auto getOwner = [guidChain]() { return Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(guidChain); };

        props->addField(QSharedPointer<TOmnigenField<QString>>::create(
            "Name",
            [getOwner]()
            {
                return getOwner()->getName();
            },
            [getOwner](auto&& newName)
            {
                auto ridge = getOwner();

                if (History::GetContext()->IsUndoingOrRedoing())
                    Design::RidgesSelectionMgr::get()->setSelection<Design::ERidgesSelection::Ridge>({ ridge });

                ridge->setName(newName);
                return true;
            }));

        props->addField(QSharedPointer<TOmnigenField<float>>::create("Right Slope Angle Factor",
            [getOwner]()
            {
                return getOwner()->getRightSlopeFactor();
            },
            [getOwner](auto&& value)
            {
                if (value <= 0.0f)
                {
                    QMessageBox(QMessageBox::Icon::Critical,
                        QString::fromStdString("Error"),
                        QString::fromStdString("Ridgeline Slope Factor cannot be 0 or lower."), QMessageBox::StandardButton::Ok).exec();
                    return false;
                }

                getOwner()->setRightSlopeFactor(value);
                return true;
            }));

        props->addField(QSharedPointer<TOmnigenField<float>>::create("Left Slope Angle Factor",
            [getOwner]()
            {
                return getOwner()->getLeftSlopeFactor();
            },
            [getOwner](auto&& value)
            {
                if (value <= 0.0f)
                {
                    QMessageBox(QMessageBox::Icon::Critical,
                        QString::fromStdString("Error"),
                        QString::fromStdString("Ridgeline Slope Factor cannot be 0 or lower."), QMessageBox::StandardButton::Ok).exec();
                    return false;
                }

                getOwner()->setLeftSlopeFactor(value);
                return true;
            }));

        return props;
    }

    void RidgeSelection::update(const std::any& newRidge, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        if (!bSubtract)
            return;

        // Deselect all matches
        for (int i = 0; i < currentSelections->size(); ++i)
        {
            auto* ridgeSel = static_cast<RidgeSelection*>(currentSelections->at(i).get());
            if (ridgeSel->ridgePtr->getGuid() == ridgePtr->getGuid())
            {
                ridgeSel->deselect();
                currentSelections->erase(currentSelections->begin() + i--);
            }
        }
    }

    void RidgeSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D RidgeSelection::getPosition() const
    {
        return ridgePtr->getControlPoints()[(ridgePtr->getControlPoints().size() / 2)];
    }

    void RidgeSelection::select() const
    {
        ridgePtr->setSelected(true);
    }

    void RidgeSelection::deselect() const
    {
        ridgePtr->setSelected(false);
    }

    bool RidgeDomainSelection::findOnScene(QMap<ERidgesSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        QMap<ELayoutSelection, std::any> layoutOutput;
        DomainSelection::findOnScene(&layoutOutput, x, y, screenData);

        if (layoutOutput.contains(ELayoutSelection::Domain))
        {
            auto handle = std::any_cast<QSharedPointer<DDomainHandle>>(layoutOutput[ELayoutSelection::Domain]);

            if (handle->getDomain().lock()->getType() == EDomainType::Terrain)
            {
                (*output)[ERidgesSelection::Domain] = layoutOutput[ELayoutSelection::Domain];
                return true;
            }
        }

        return false;
    }

    QMenu* RidgeDomainSelection::requestContextMenu(const std::any& data)
    {
        return new QMenu(Omnigen::get());
    }

    RidgeDomainSelection::Selection(const std::any& inHandle)
        : DomainSelection(inHandle)
    {
        DomainSelection::useGizmo = false;
        DManipulationGizmo::get()->setVisible(false);
    }
}