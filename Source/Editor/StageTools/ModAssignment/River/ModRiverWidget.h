#pragma once
#include "Editor/Sections/Outline/OutlineTree.h"
#include "Editor/StageTools//ModAssignment/ModToolsBase.h"

namespace Design
{
    struct ModRiverTreeItem : OutlineTreeItem
    {
        enum CustomRoles
        {
            Guid = Qt::UserRole
        };

        ModRiverTreeItem(const QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>>& riverMod, OutlineTreeItem* parent = nullptr);

        virtual QVariant getDataByRole(int role) const override;

        qint64 modGuid;
    };

    class QModRiverTreeModel : public OutlineTreeModel
    {
    public:
        void clearSelection();
        void selectRiverMod(qint64 guid);
        void addRiverMod(size_t typeHash, QSharedPointer<Editable> object);
        void removeRiverMod(QSharedPointer<Editable> object);
        void updateRiverMod(QSharedPointer<Editable> object, bool reset);
        void loadRiverMods();
        void clear();

        void setTreeView(QTreeView* inView);

    public slots:
        void treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
        void itemDoubleClicked(const QModelIndex& idx);
        void outlineContextMenuRequested(const QPoint& pos);

    private:
        QTreeView* treeView = nullptr;
    };
}