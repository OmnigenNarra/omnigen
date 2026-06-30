#pragma once
#include "SelectionMgrBase.h"
#include "Utils/PlatformMisc.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "ManipulationGizmoSelection.h"

namespace Design
{
    template<typename SelectionEnum>
    class SelectionMgr;
}

namespace EAC
{
    template<typename SelectionEnum>
    struct FindSelectables
    {
        template<SelectionEnum SE>
        static void Action(QMap<SelectionEnum, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
        {
            Design::Selection<SelectionEnum, SE>::findOnScene(output, x, y, screenData);
        }
    };

    template<typename SelectionEnum>
    struct CheckIfHasGizmo
    {
        template<SelectionEnum SE>
        static constexpr bool Action()
        {
            return std::is_base_of_v<Design::SelectionManipulationGizmo, Design::Selection<SelectionEnum, SE>>;
        }
    };

    template<typename SelectionEnum>
    struct CheckIfCurrentSelection;

    template<typename SelectionEnum>
    struct CreateSelection
    {
        template<SelectionEnum SE>
        static QSharedPointer<Design::SelectionBase> Action(const std::any& data)
        {
            return QSharedPointer<Design::Selection<SelectionEnum, SE>>::create(data);
        }
    };

    template<typename SelectionEnum>
    struct HoverUpdate
    {
        template<SelectionEnum SE>
        static void Action(const std::any& data, bool isLive)
        {
            Design::Selection<SelectionEnum, SE>::hoverUpdate(data, isLive);
        }
    };

    template<typename SelectionEnum>
    struct RequestContextMenu
    {
        template<SelectionEnum SE>
        static QMenu* Action(const std::any& data)
        {
            return Design::Selection<SelectionEnum, SE>::requestContextMenu(data);
        }
    };
}

namespace Design
{
    template<typename SelectionEnum>
    int selectionRadius = 10;

    // SelectionEnum should be EAC compatible with None/Last value included at the end.
    template<typename SelectionEnum>
    class SelectionMgr : public SelectionMgrBase
    {
    public:
        static constexpr int NO_SELECTION = magic_enum::enum_count<SelectionEnum>() - 1;

        SelectionMgr() 
        {
            currentSelectionType = NO_SELECTION;
        }

        static SelectionMgr* get()
        {
            static SelectionMgr mgr;
            return &mgr;
        }

        template<SelectionEnum SE>
        QSet<typename Selection<SelectionEnum, SE>::DataType> getSelection() const
        {
            QSet<typename Selection<SelectionEnum, SE>::DataType> results;

            auto it = selections.find(int(SE));
            if (it != selections.end())
                for (auto&& sel : *it)
                    Selection<SelectionEnum, SE>::getData(sel.get(), &results);

            return results;
        }

        template<SelectionEnum SE>
        void setSelection(const QSet<typename Selection<SelectionEnum, SE>::DataType>& data)
        {
            clearSelection();
            selections[int(SE)] << Selection<SelectionEnum, SE>::createFromData(data);
            currentSelectionType = int(SE);
            onSelectionChanged();
        }

        virtual int getSelectionRadius() const override
        {
            return selectionRadius<SelectionEnum>;
        }

        virtual void hoverUpdate(int x, int y) override
        {
            auto objects = findAllObjectsUnderCursor(x, y);

            for (int i = 0; i< NO_SELECTION; ++i)
                if (auto it = objects.find(SelectionEnum(i)); it != objects.end())
                    EnumConstexpr<SelectionEnum>::Interface::template UseIn<EAC::HoverUpdate<SelectionEnum>>(SelectionEnum(i), *it, true);
                else
                    EnumConstexpr<SelectionEnum>::Interface::template UseIn<EAC::HoverUpdate<SelectionEnum>>(SelectionEnum(i), std::any(), false);
        }

        virtual void selectObjects(int x, int y, ESelectionStep ss) override
        {
            auto objects = findAllObjectsUnderCursor(x, y);

            bAppend = isKeyDown(VK_SHIFT);
            bSubtract = !bAppend && isKeyDown(VK_CONTROL);

            if (ss == ESelectionStep::Press)
            {
                if (objects.isEmpty() )
                {
                    if (!bAppend && !bSubtract && !bIsMovingGizmo)
                    {
                        currentSelectionType = NO_SELECTION;
                        clearSelection();
                        return;
                    }
                }
                else
                {
                    SelectionEnum firstHit = objects.firstKey();

                    // Prevents selecting other objects than those currently being appended or subtracted, and allows start for those operation from SE::None
                    if (currentSelectionType != NO_SELECTION && currentSelectionType != int(firstHit) && (bAppend || bSubtract))
                        return;

                    // If object with visible gizmo is selected second time begin gizmo logic
                    if (!bSubtract && DManipulationGizmo::get()->isVisible())
                        if (EnumConstexpr<SelectionEnum>::Interface::template UseIn<EAC::CheckIfHasGizmo<SelectionEnum>>(firstHit))
                            bIsMovingGizmo = EnumConstexpr<SelectionEnum>::Interface::template UseIn<EAC::CheckIfCurrentSelection<SelectionEnum>>(firstHit, this, objects[firstHit]);

                    bIsSelecting = true;
                    workSelection = EnumConstexpr<SelectionEnum>::Interface::template UseIn<EAC::CreateSelection<SelectionEnum>>(firstHit, objects[firstHit]);
                    currentSelectionType = int(firstHit);

                    if (bIsMovingGizmo)
                    {
                        workSelection.staticCast<SelectionManipulationGizmo>()->grabGizmo(x, y);
                        return;
                    }

                    if (!bAppend && !bSubtract)
                        clearSelection();

                    selectionsSnapshot = selections;
                    workSelection->update(objects[firstHit], &selections[int(firstHit)], selectionsSnapshot[int(firstHit)]);
                }
            }
            // Skip if movement on empty without gizmo (does not end selection process thus release on empty is possible)
            else if (ss == ESelectionStep::Move && !(objects.isEmpty() && !bIsMovingGizmo))
            {
                if (bIsMovingGizmo)
                {
                    workSelection.staticCast<SelectionManipulationGizmo>()->moveObject(x, y);
                    return;
                }

                SelectionEnum firstHit = objects.firstKey();

                if (currentSelectionType != int(firstHit))
                    return;

                auto cType = SelectionEnum(currentSelectionType);
                if (objects.contains(cType))
                    workSelection->update(objects[cType], &selections[currentSelectionType], selectionsSnapshot[currentSelectionType]);
            }
            else if (ss == ESelectionStep::Release) // Release
            {
                if (bIsMovingGizmo)
                {
                    workSelection.staticCast<SelectionManipulationGizmo>()->endGizmoMove();
                    bIsMovingGizmo = false;
                    return;
                }

                workSelection->save(&selections[currentSelectionType]);
                bIsSelecting = false;

                // end if subtract deselects last object
                if (bSubtract && selections[currentSelectionType].empty())
                {
                    clearSelection();
                    currentSelectionType = NO_SELECTION;
                    return;
                }

                onSelectionChanged();
            }
        }

        virtual void rightClick(QMouseEvent* me) override
        {
            hoverUpdate(me->x(), me->y());

            auto objects = findAllObjectsUnderCursor(me->x(), me->y());
            if (objects.isEmpty())
                return;

            SelectionEnum firstHit = objects.firstKey();
            auto* menu = EnumConstexpr<SelectionEnum>::Interface::template UseIn<EAC::RequestContextMenu<SelectionEnum>>(firstHit, objects[firstHit]);

            menu->exec(me->globalPos());
            delete menu;
        }

    protected:
        QMap<SelectionEnum, std::any> findAllObjectsUnderCursor(int x, int y) const
        {
            auto viewportData = QOmnigenViewportSection::getActiveViewport()->getSelectionData();

            QMap<SelectionEnum, std::any> objects;
            EnumConstexpr<SelectionEnum>::Interface::template UseAllIn<EAC::FindSelectables<SelectionEnum>>(&objects, x, y, viewportData);
            return objects;
        }
    };
}

namespace EAC
{
    template<typename SelectionEnum>
    struct CheckIfCurrentSelection
    {
        template<SelectionEnum SE>
        static bool Action(Design::SelectionMgrBase* selMgr, const std::any& data)
        {
            if constexpr (int(SE) != Design::SelectionMgr<SelectionEnum>::NO_SELECTION)
            {
                auto currentSelections = static_cast<Design::SelectionMgr<SelectionEnum>*>(selMgr)->getSelection<SE>();
                for (auto&& res : currentSelections)
                    if (res == std::any_cast<Design::Selection<SelectionEnum, SE>::DataType>(data))
                        return true;
            }

            return false;
        }
    };
}

template<typename SelectionEnum, SelectionEnum SE>
std::any getSelectionData(Design::SelectionBase* sel)
{
    using SelType = Design::Selection<SelectionEnum, SE>;
    QSet<typename SelType::DataType> dataSet;
    SelType::getData(sel, &dataSet);
    return *dataSet.begin();
};