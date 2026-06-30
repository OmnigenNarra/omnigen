#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"
#include "Source/Scene/Generation/Stages/Ridges/RidgeMarker.h"

namespace Design
{
    struct RidgesTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            Guid = Qt::UserRole
        };

        RidgesTreeItem(const QSharedPointer<DRidgeMarker>& ridge, OutlineTreeItem* parent = nullptr);

        virtual QVariant getDataByRole(int role) const override;

        qint64 guid;
    };

    class QRidgesTreeModel : public OutlineTreeModel
    {
    public:
        void clearSelection();
        void selectRidge(qint64 guid);
        void addRidge(size_t typeHash, QSharedPointer<Editable> object);
        void removeRidge(QSharedPointer<Editable> object);
        void updateRidge(QSharedPointer<Editable> object, bool reset);
        void loadRidges();
        void loadRidges(const std::vector<QSharedPointer<DRidgeMarker>>& ridgeMarkers);
        void loadSubridges(RidgesTreeItem* ridgeItem);
        void clear();
        std::vector<qint64> getRidgeGuidChain(qint64 guid);

        void setTreeView(QTreeView* inView);;

    public slots:
        void treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
        void itemDoubleClicked(const QModelIndex& idx);
        void outlineContextMenuRequested(const QPoint& pos);

    private:
        QTreeView* treeView = nullptr;
    };
}