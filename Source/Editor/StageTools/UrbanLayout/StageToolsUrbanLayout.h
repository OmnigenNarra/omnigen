#pragma once
#include "../StageToolsBase.h"
#include "UrbanLayoutWidgets.h"

namespace Design
{
    template<>
    class StageTools<EGenerationStage::UrbanLayout> final : public StageToolsBase
    {
    public:
        StageTools() = default;

        virtual SelectionMgrBase* getSelectionMgr() const override;

        virtual void bind() override;
        virtual void unbind() override;

        virtual void save(OmniBin<std::ios::out>& writer) const override;
        virtual void load(OmniBin<std::ios::in>& reader) override;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

    private:
        QWidget* createOutlineToolbar();

        // Event reactions
        void updateTreeViewSelection();

        QToolBar* toolBar = nullptr;

        QUrbanLayoutTreeModel treeModel;
        QTreeView* treeView = nullptr;

        friend class UrbanLayaoutWidgets;
    };
}
