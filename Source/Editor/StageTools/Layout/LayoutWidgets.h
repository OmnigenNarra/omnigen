#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

namespace Design
{
    struct LayoutTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            Guid = Qt::UserRole,
            Type = Qt::UserRole + 1,
        };

        LayoutTreeItem(const QSharedPointer<DDomain>& domain, OutlineTreeItem* parent = nullptr);
        // Only for category item
        LayoutTreeItem(const EDomainType& type, OutlineTreeItem* parent = nullptr);

        virtual QVariant getDataByRole(int role) const override;

        qint64 guid;
        EDomainType type;
    };

    class QLayoutTreeModel : public OutlineTreeModel
    {
    public:
        virtual QVariant data(const QModelIndex& index, int role) const override;

        void clearSelection();
        void selectDomain(qint64 guid);
        void addDomain(size_t typeHash, QSharedPointer<Editable> object);
        void removeDomain(QSharedPointer<Editable> object);
        void updateDomain(QSharedPointer<Editable> object, bool reset);
        void loadDomains();
        void clear();

        void setTreeView(QTreeView* inView);

        std::map<EDomainType, LayoutTreeItem*> domainCategories;

    public slots:
        void treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
        void itemDoubleClicked(const QModelIndex& idx);
        void outlineContextMenuRequested(const QPoint& pos);

    private:
        QTreeView* treeView = nullptr;
    };
}
