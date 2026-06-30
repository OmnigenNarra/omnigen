#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"

namespace Design
{
    struct LithoAssetTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            BlockType = Qt::UserRole
        };

        LithoAssetTreeItem(const QString& nameAsset, const qint64 idAsset, OutlineTreeItem* parent = nullptr);

        QString name;
        qint64 id;
    };

    class QLithoAssignmentTreeModel : public OutlineTreeModel
    {
    public:
        void clearSelection();
        void addBlockType(const QString& nameAsset, const qint64 idAsset);
        void loadLithoAssets();
        void clear();
        void setTreeView(QTreeView* inView);

    private:
        QTreeView* treeView = nullptr;
    };
}

