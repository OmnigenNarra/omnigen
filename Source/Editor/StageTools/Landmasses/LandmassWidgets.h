#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"

namespace Design
{
    struct LandmassTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            Guid = Qt::UserRole
        };

        LandmassTreeItem(const QSharedPointer<DLandmassMarker>& landmass, OutlineTreeItem* parent = nullptr);
        LandmassTreeItem(const QSharedPointer<DShorelineMarker>& shoreline, OutlineTreeItem* parent = nullptr);

        virtual QVariant getDataByRole(int role) const override;

        qint64 guid;
    };

    class QLandmassTreeModel : public OutlineTreeModel
    {
    public:
        void clearSelection();
        void selectItem(qint64 guid);
        void addItem(size_t typeHash, QSharedPointer<Editable> object);
        void removeItem(QSharedPointer<Editable> object);
        void updateItem(QSharedPointer<Editable> object, bool reset);
        void loadLandmasses();
        void clear();

        void setTreeView(QTreeView* inView);;

    public slots:
        void treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
        void itemDoubleClicked(const QModelIndex& idx);
        void outlineContextMenuRequested(const QPoint& pos);

    private:
        void addLandmass(QSharedPointer<DLandmassMarker> landmass);
        void addShoreline(QSharedPointer<DShorelineMarker> shoreline);


        QTreeView* treeView = nullptr;
    };
}