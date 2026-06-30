#include "stdafx.h"
#include "LayoutSelection.h"
#include "Utils/PlatformMisc.h"
#include "../StageTools.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"

namespace Design
{
    bool GridSelection::findOnScene(QMap<ELayoutSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        if(auto point = OmnigenCameraMgr::get()->findGridPoint(x, y); point)
        {
            (*output)[ELayoutSelection::Grid] = (*point);
            return true;
        }

        return false;
    }

    QMenu* GridSelection::requestContextMenu(const std::any& data)
    {
        // Create domain
        QMenu* menu = new QMenu(Omnigen::get());
        auto&& layoutTools = getStageTools<EGenerationStage::Layout>();

        QMenu* createDomainMenu = new QMenu("Create new domain", menu);

        createDomainMenu->addAction(layoutTools->actions[ELayoutAction::CreateTerrain]);
        createDomainMenu->addAction(layoutTools->actions[ELayoutAction::CreateBiome]);
        createDomainMenu->addAction(layoutTools->actions[ELayoutAction::CreateWater]);

        // Extract domain
        QMenu* extractDomainMenu = new QMenu("Extract new domain");

        extractDomainMenu->addAction(layoutTools->actions[ELayoutAction::ExtractTerrain]);
        extractDomainMenu->addAction(layoutTools->actions[ELayoutAction::ExtractBiome]);
        extractDomainMenu->addAction(layoutTools->actions[ELayoutAction::ExtractWater]);

        // Finalize
        menu->addMenu(createDomainMenu);
        menu->addMenu(extractDomainMenu);

        return menu;
    }

    void GridSelection::getData(const SelectionBase* obj, QSet<GPoint>* data)
    {
        (*data) += static_cast<const GridSelection*>(obj)->squares;
    }

    std::vector<QSharedPointer<SelectionBase>> GridSelection::createFromData(const QSet<GPoint>& inSquares)
    {
        auto sel = QSharedPointer<GridSelection>::create();
        sel->squares = inSquares;
        sel->select();
        return { sel };
    }

    GridSelection::Selection(const std::any& sq)
        : squares({ std::any_cast<GPoint>(sq) })
        , startCoord(*squares.begin())
        , endCoord(*squares.begin())
        , bDiagonal(!isKeyDown(VK_MENU))
    {
    }

    void GridSelection::update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        endCoord = std::any_cast<GPoint>(newSquare);

        GridSelection* gridSnapshot = selectionsSnapshot.empty() ? nullptr : static_cast<GridSelection*>(selectionsSnapshot.at(0).get());
        GridSelection* gridCurrent = currentSelections->empty() ? nullptr : static_cast<GridSelection*>(currentSelections->at(0).get());

        // Restart from snapshot
        if (gridSnapshot)
        {
            gridCurrent->deselect();
            *gridCurrent = *gridSnapshot;
        }

        // Diagonal-based selection.
        if (bDiagonal)
        {
            // Rebuild diagonal
            if (!bSubtract)
                deselect();

            squares.clear();

            // Sort diagonal coords.
            if (startCoord.x < endCoord.x)
            {
                lowerCoord.x = startCoord.x;
                higherCoord.x = endCoord.x;
            }
            else
            {
                lowerCoord.x = endCoord.x;
                higherCoord.x = startCoord.x;
            }
            if (startCoord.z < endCoord.z)
            {
                lowerCoord.z = startCoord.z;
                higherCoord.z = endCoord.z;
            }
            else
            {
                lowerCoord.z = endCoord.z;
                higherCoord.z = startCoord.z;
            }

            lowerCoord.x = std::clamp(lowerCoord.x, 0, GRID_SEGMENT_COUNT - 1);
            lowerCoord.z = std::clamp(lowerCoord.z, 0, GRID_SEGMENT_COUNT - 1);
            higherCoord.x = std::clamp(higherCoord.x, 0, GRID_SEGMENT_COUNT - 1);
            higherCoord.z = std::clamp(higherCoord.z, 0, GRID_SEGMENT_COUNT - 1);

            for (int i = lowerCoord.x; i <= higherCoord.x; ++i)
                for (int j = lowerCoord.z; j <= higherCoord.z; ++j)
                    squares.insert({ i, j });
        }
        // Painting selection
        else
        {
            squares.insert(endCoord);
        }

        apply(currentSelections);
    }

    void GridSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract || bAppend)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D GridSelection::getPosition() const
    {
        auto&& [x, z] = *squares.begin();
        return QVector3D(x * GRID_SEGMENT_WIDTH, OmnigenCameraMgr::get()->getCameraForActiveViewport()->getPosition().y(), z * GRID_SEGMENT_WIDTH);
    }

    void GridSelection::select() const
    {
        for (auto&& sq : squares)
        {
            GLuint singleDimIdx = sq.x * GRID_SEGMENT_COUNT + sq.z;
            // Only 31 flag bits.
            GLuint unitIdx = singleDimIdx / 31;
            GLuint unitBit = singleDimIdx % 31;
            DEditorGrid::shaderSelectionData[unitIdx] |= 1 << unitBit;
        }
    }

    void GridSelection::deselect() const
    {
        for (auto&& sq : squares)
        {
            GLuint singleDimIdx = sq.x * GRID_SEGMENT_COUNT + sq.z;
            // Only 31 flag bits.
            GLuint unitIdx = singleDimIdx / 31;
            GLuint unitBit = singleDimIdx % 31;
            DEditorGrid::shaderSelectionData[unitIdx] &= ~(1 << unitBit);
        }
    }

    void GridSelection::apply(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        GridSelection* target = currentSelections->empty() ? this : static_cast<GridSelection*>(currentSelections->at(0).get());

        if (!bSubtract)
        {
            if (target == this)
            {
                select();
            }
            else
            {
                target->squares += squares;
                target->select();
            }
        }
        else
        {
            if (target != this)
            {
                target->squares -= squares;
                target->select();
            }
        }
    }

    bool DomainSelection::findOnScene(QMap<ELayoutSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        std::optional<QSharedPointer<DDomainHandle>> result;

        if (DManipulationGizmo::get()->isVisible())
            if (checkIfGizmo(x, y))
            {
                if(!getSelectionMgr()->getAllSelection()[int(ELayoutSelection::Domain)].empty())
                {
                    auto sel = getSelectionMgr()->getAllSelection()[int(ELayoutSelection::Domain)].front();
                    (*output)[ELayoutSelection::Domain] = getSelectionData<ELayoutSelection, ELayoutSelection::Domain>(sel.get());
                    return true;
                }
            }

        float minDist = FLT_MAX;
        auto* camera = OmnigenCameraMgr::get()->getCameraForActiveViewport();

        for (auto&& [handle, domain] : Generation::Data::get()->getAllDomains())
        {
            if (handle.isNull())
                continue;

            if (!OmnigenCameraMgr::get()->isSpriteHit(handle->getPosition(), handle->getSpriteSize(), x, y))
                continue;

            // Sprite found! Check if it's the closest one to the camera.
            float d = distance(handle->getPosition(), camera->getPosition());
            if (d < minDist)
            {
                result = handle;
                minDist = d;
            }
        }

        if (result)
        {
            (*output)[ELayoutSelection::Domain] = *result;
            return true;
        }

        return false;
    }

    void DomainSelection::hoverUpdate(const std::any& data, bool isLive)
    {
        if (!isLive)
        {
            currentHover.clear();
            return;
        }

        currentHover = std::any_cast<QSharedPointer<DDomainHandle>>(data);
    }

    QMenu* DomainSelection::requestContextMenu(const std::any& data)
    {
        auto&& layoutTools = getStageTools<EGenerationStage::Layout>();

        auto contextMenuDomainHandle = std::any_cast<QSharedPointer<DDomainHandle>>(data);
        contextMenuDomain = contextMenuDomainHandle->getDomain().lock();

        ELayoutSelection stype = ELayoutSelection(LayoutSelectionMgr::get()->getSelectionType());

        QMenu* menu = new QMenu(Omnigen::get());

        if (stype == ELayoutSelection::Grid)
        {
            layoutTools->actions[ELayoutAction::AppendToDomain]->setText(QString("Add selection to %1").arg(contextMenuDomain->getName()));
            layoutTools->actions[ELayoutAction::ExtractIntoDomain]->setText(QString("Extract selection into %1").arg(contextMenuDomain->getName()));
            layoutTools->actions[ELayoutAction::SubtractFromDomain]->setText(QString("Subtract selection from %1").arg(contextMenuDomain->getName()));

            menu->addAction(layoutTools->actions[ELayoutAction::AppendToDomain]);
            menu->addAction(layoutTools->actions[ELayoutAction::ExtractIntoDomain]);
            menu->addAction(layoutTools->actions[ELayoutAction::SubtractFromDomain]);
        }
        else if (stype == ELayoutSelection::Domain)
        {
            otherSelectedDomains.clear();
            for (auto&& dh : LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Domain>())
                if (auto domain = dh->getDomain().lock(); dh != contextMenuDomainHandle && domain->getType() == contextMenuDomain->getType())
                    otherSelectedDomains << domain;

            if (!otherSelectedDomains.empty())
            {
                layoutTools->actions[ELayoutAction::MergeDomains]->setText(QString("Merge %1 into %2").arg((otherSelectedDomains.size() == 1) ? otherSelectedDomains.front()->getName() : "compatible selection").arg(contextMenuDomain->getName()));
                menu->addAction(layoutTools->actions[ELayoutAction::MergeDomains]);
            }

            menu->addAction(layoutTools->actions[ELayoutAction::DeteleSelectedDomains]);
        }
        
        return menu;
    }

    void DomainSelection::getData(const SelectionBase* obj, QSet<QSharedPointer<DDomainHandle>>* data)
    {
        (*data) += static_cast<const DomainSelection*>(obj)->handle;
    }

    std::vector<QSharedPointer<SelectionBase>> DomainSelection::createFromData(const QSet<QSharedPointer<DDomainHandle>>& inHandles)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& handle : inHandles)
        {
            auto sel = QSharedPointer<DomainSelection>::create();
            sel->handle = handle;
            sel->select();
            results << sel;
        }

        return results;
    }

    bool DomainSelection::isDomainHovered(const QSharedPointer<DDomainHandle>& hnd)
    {
        return currentHover.lock() == hnd;
    }

    DomainSelection::Selection(const std::any& inHandle)
        : handle(std::any_cast<QSharedPointer<DDomainHandle>>(inHandle))
        , bRainbow(handle->isRainbow())
    {
        if (!bSubtract)
            select();
    }

    QSharedPointer<OmnigenPropertyListBase> DomainSelection::makePropertyList()
    {
        auto domain = handle->getDomain().lock();
        qint64 guid = domain->getGuid();

        auto props = QSharedPointer<OmnigenPropertyListBase>::create(guid, sharedFromThis());

        domain->getData()->fillProps(props);
        return props;
    }

    void DomainSelection::update(const std::any& newDomain, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        if (!bSubtract)
            return;

        // Deselect all matches
        for (int i=0; i<currentSelections->size(); ++i)
        {
            auto* domainSel = static_cast<DomainSelection*>(currentSelections->at(i).get());
            if (domainSel->handle->getGuid() == handle->getGuid())
            {
                domainSel->deselect();
                currentSelections->erase(currentSelections->begin() + i--);
            }
        }
    }

    void DomainSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    void DomainSelection::select() const
    {
        if (bRainbow)
            rainbowSplit();
        else if (useGizmo)
        {
            handle->setSelected(true);
            QWeakPointer<DDomainHandle> handlePtr = handle;

            // Show gizmo only for Terrain
            if (handle->getDomain().lock()->getType() == EDomainType::Terrain)
            {
                const auto& sel = getSelectionMgr()->getAllSelection()[int(ELayoutSelection::Domain)];

                // Show gizmo only for single selection (but prevent gizmo hiding if selecting again self)
                if (sel.size() == 0)
                {
                    showGizmo(handle->getPosition(), false, true, false);
                    return;
                }
                else if (sel.front()->getPosition() == handle->getPosition() && sel.size() == 1)
                    return;
            }

            DManipulationGizmo::get()->setVisible(false);
        }
    }

    void DomainSelection::deselect() const
    {
        handle->setSelected(false);
        DManipulationGizmo::get()->setVisible(false);

        if (bRainbow)
            handle->update();
    }

    QVector3D DomainSelection::getPosition() const
    {
        return handle->getPosition();
    }

    void DomainSelection::grabGizmo(int mousePosX, int mousePosY)
    {
        if (useGizmo)
            SelectionManipulationGizmo::grabAxis(mousePosX, mousePosY, EArrowType::YArrow);
    }

    void DomainSelection::moveObject(int mousePosX, int mousePosY)
    {
        if (useGizmo)
        {
            auto deltaMovement = SelectionManipulationGizmo::moveGizmo(mousePosX, mousePosY);
            emit Editable::aboutToBeModified(handle->getDomain().lock());
            handle->getDomain().lock()->getData<EDomainType::Terrain>()->maxHeight += deltaMovement.y();
            emit Editable::modified(handle->getDomain().lock());
        }
    }

    void DomainSelection::endGizmoMove()
    {
        if (useGizmo)
        {
            SelectionManipulationGizmo::endGizmoMove();
            Omnigen::get()->getProperties()->set(getSelectionMgr()->getAllSelection()[int(ELayoutSelection::Domain)].front()->makePropertyList());
        }
    }

    void DomainSelection::rainbowSplit() const
    {
        auto overlappingHandles = Generation::Data::get()->getDomainHandlesAt(handle->getPosition());
        float handleSize = overlappingHandles[0] ? overlappingHandles[0]->getSpriteSize() : 0.f;
        float angleStep = 360.0f / float(overlappingHandles.size());

        auto&& cameraRotation = OmnigenCameraMgr::get()->getCameraForActiveViewport()->getRotation();
        auto&& lookAt = OmnigenCameraMgr::get()->getCameraForActiveViewport()->getLookAt();
        QVector3D cameraLeft = cameraRotation.rotatedVector({ -handleSize * 0.75f, 0, 0 });

        for (int i = 0; i < overlappingHandles.size(); ++i)
        {
            QVector3D adjustment = QQuaternion::fromAxisAndAngle(lookAt, float(i) * angleStep).rotatedVector(cameraLeft);
            overlappingHandles[i]->adjustPosition(adjustment);
        }
    }
}