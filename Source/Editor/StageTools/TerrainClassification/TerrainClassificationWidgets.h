#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"

using ETerrainBlock = Generation::ETerrainBlock;

namespace Design
{
    struct TerrainClassificationTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            BlockType = Qt::UserRole
        };

        TerrainClassificationTreeItem(ETerrainBlock terrainType, OutlineTreeItem* parent = nullptr);

        ETerrainBlock blockType;
    };

    class QTerrainClassificationTreeModel : public OutlineTreeModel
    {
    public:
        void clearSelection();
        void addBlockType(ETerrainBlock terrainType);
        void loadBlockTypes();
        void clear();
        void setTreeView(QTreeView* inView);;

    public slots:
        void treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

    private:
        QTreeView* treeView = nullptr;
    };
}