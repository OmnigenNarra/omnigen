#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanSuggestion.h"

namespace Design
{
    struct UrbanLayoutTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            Guid = Qt::UserRole
        };

        UrbanLayoutTreeItem(const QSharedPointer<Generation::UrbanSuggestion>& suggestion, OutlineTreeItem* parent = nullptr);

        virtual QVariant getDataByRole(int role) const override;

        qint64 guid;
    };

    class QUrbanLayoutTreeModel : public OutlineTreeModel
    {
    public:
        void clearSelection();
        void selectSuggestion(qint64 guid);
        void loadSuggestions();
        void clear();
        void setTreeView(QTreeView* inView);;

    public slots:
        void treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
        void itemDoubleClicked(const QModelIndex& idx);
        void outlineContextMenuRequested(const QPoint& pos);

    private:
        QTreeView* treeView = nullptr;
    };
}