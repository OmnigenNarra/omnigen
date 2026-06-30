#include "stdafx.h"
#include "StageToolsBase.h"
#include "Omnigen.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/History/OmnigenHistoryStackSection.h"
#include "SelectionMgr.h"

namespace Design
{
    enum class ESelectionDummy
    {
        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(ESelectionDummy, ESelectionDummy::None);

    Design::SelectionMgrBase* StageToolsBase::getSelectionMgr() const
    {
        static SelectionMgr<ESelectionDummy> dummy;
        return &dummy;
    }

    void StageToolsBase::bind()
    {
        auto* historyStack = Omnigen::get()->getHistoryStack();
        historyContext.BindOnStackChanged(std::bind(&QOmnigenHistoryStackSection::updateStack, historyStack, std::placeholders::_1));

        History::SetContext(&historyContext);

        Omnigen::get()->getHistoryStack()->updateStack();

        getSelectionMgr()->clearSelection();
    }

    void StageToolsBase::unbind()
    {
        getSelectionMgr()->clearSelection();

        for (auto&& conn : qConnections)
            disconnect(conn);

        qConnections.clear();

        for (auto&& conn : gConnections)
            conn.Disconnect();

        gConnections.clear();

        for (auto* shortcut : shortcuts)
            delete shortcut;

        shortcuts.clear();

        Omnigen::get()->getOutline()->clearSection();

        historyContext.UnbindOnStackChanged();

        History::SetContext(nullptr);
    }

    void StageToolsBase::clearHistory()
    {
        historyContext.Clear();
    }

    void StageToolsBase::disconnectNodes()
    {
        for (auto&& conn : qNodesConnections)
            conn.Disconnect();
        qNodesConnections.clear();
    }
}