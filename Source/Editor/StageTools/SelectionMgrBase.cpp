#include "stdafx.h"
#include "SelectionMgrBase.h"
#include "Utils/PlatformMisc.h"
#include "StageTools.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"

namespace Design
{
    SelectionBase::SelectionBase()
    {
        auto* mgr = getSelectionMgr();
        bAppend = mgr->bAppend;
        bSubtract = mgr->bSubtract;
    }

    void SelectionMgrBase::clearSelection()
    {
        // Deselect all possible selections, except for frozen ones
        auto keys = selections.keys();
        for (int i : keys)
        {
            for (int j = 0; j < selections[i].size(); ++j)
            {
                if (!(selections[i][j]->isSelectionFrozen()))
                {
                    selections[i][j]->deselect();
                    selections[i].erase(selections[i].begin() + j--);
                }
            }

            // Clear all empty entires
            if (selections[i].empty())
                selections.remove(i);
        }

        onSelectionChanged();
    }

    void SelectionMgrBase::onSelectionChanged()
    {
        emit selectionChanged();

        auto* properties = Omnigen::get()->getProperties();
        if (selections.isEmpty())
            properties->clear();
        else
            properties->set(selections.first().front()->makePropertyList());
    }

    Design::SelectionMgrBase* getSelectionMgr()
    {
        return EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(Generation::Data::get()->getGenerationStage())->getSelectionMgr();
    }
}