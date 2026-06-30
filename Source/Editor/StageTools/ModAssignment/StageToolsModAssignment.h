#pragma once
#include "../StageToolsBase.h"
#include "ModToolsBase.h"
#include "Source/Editor/Sections/Outline/OutlineTree.h"

namespace Design
{
    template<>
    class StageTools<EGenerationStage::ModAssignment> final : public StageToolsBase
    {
    public:
        StageTools();

        void loadTreeViewEntries();
        void onSelectionChanged();
        void addEntry(size_t typeHash, QSharedPointer<Editable> object);
        void removeEntry(QSharedPointer<Editable> object);
        void updateEntry(QSharedPointer<Editable> object, bool reset);

        virtual SelectionMgrBase* getSelectionMgr() const override;

        virtual void bind() override;
        virtual void unbind() override;

        virtual void save(OmniBin<std::ios::out>& writer) const override;
        virtual void load(OmniBin<std::ios::in>& reader) override;

    private:
        QWidget* createToolbar();
        void createModTools();

        QTreeView* treeView = nullptr;
        OutlineTreeModel* treeModel;

        std::array<QWidget*, int(Generation::ETerrainMod::Last)> modTools;
        std::optional<int> boundModTools;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;
    };
}
