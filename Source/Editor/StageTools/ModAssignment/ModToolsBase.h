#pragma once
#include "Scene/Generation/Stages/TerrainMods/TerrainModBase.h"
#include "Editor/StageTools/SelectionMgr.h"
#include "Source/Editor/Sections/Outline/OutlineTree.h"

namespace Design
{
    struct ModToolsBase : QObject
    {
        Q_OBJECT

    public:
        virtual void bind() {};
        virtual void unbind() {};
        virtual QWidget* create() { return new QWidget(); };
        virtual OutlineTreeModel* getTreeModel() { return nullptr; };

        virtual void loadTreeViewModEntries() {};
        virtual void onModSelectionChanged() {};
        virtual void addModEntry(size_t typeHash, QSharedPointer<Editable> object) {};
        virtual void removeModEntry(QSharedPointer<Editable> object) {};
        virtual void updateModEntry(QSharedPointer<Editable> object, bool reset) {};
        virtual void setModToolTreeView(QTreeView* treeView) {};
    };

    template<Generation::ETerrainMod TM>
    struct ModTools : ModToolsBase
    {
    };

    using ModSelectionMgr = SelectionMgr<Generation::ETerrainMod>;
}