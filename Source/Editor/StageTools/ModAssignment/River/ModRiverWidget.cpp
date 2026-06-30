#include "stdafx.h"
#include "ModRiverWidget.h"
#include "Editor/StageTools/ModAssignment/River/ModToolsRiver.h"
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverMarker.h"
#include "Scene/Generation/Stages/TerrainMods/TerrainMod.h"

#include "QToolBar"

namespace Design
{
    ModRiverTreeItem::ModRiverTreeItem(const QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>>& riverMod, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ riverMod->getName() }, parent)
        , modGuid(riverMod->getGuid())
    {
    }

    QVariant ModRiverTreeItem::getDataByRole(int role) const
    {
        if (role == Guid)
            return modGuid;

        return {};
    }

    void QModRiverTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QModRiverTreeModel::selectRiverMod(qint64 guid)
    {
        auto matches = match(index(0, 0), ModRiverTreeItem::CustomRoles::Guid, guid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        QItemSelection s(match, match);

        s.merge(treeView->selectionModel()->selection(), QItemSelectionModel::SelectionFlag::Select);
        treeView->selectionModel()->select(s, QItemSelectionModel::SelectionFlag::Select);
    }

    void QModRiverTreeModel::addRiverMod(size_t typeHash, QSharedPointer<Editable> object)
    {
        auto&& riverMod = object.dynamicCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
        if (!riverMod)
            return;

        auto&& riverModGuid = riverMod->getGuid();
        auto matches = match(index(0, 0), ModRiverTreeItem::CustomRoles::Guid, riverModGuid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (!matches.isEmpty())
            return;

        beginResetModel();
        auto itemParent = getRootItem();
        auto newItem = new ModRiverTreeItem(riverMod, itemParent);
        itemParent->appendChild(newItem);
        endResetModel();
    }

    void QModRiverTreeModel::removeRiverMod(QSharedPointer<Editable> object)
    {
        auto&& riverMod = object.dynamicCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
        if (!riverMod)
            return;

        auto matches = match(index(0, 0), ModRiverTreeItem::CustomRoles::Guid, riverMod->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<ModRiverTreeItem*>(matches.front().internalPointer());

        beginRemoveRows(match.parent(), match.row(), match.row());
        item->getParentItem()->removeChild(item);
        endRemoveRows();
    }

    void QModRiverTreeModel::updateRiverMod(QSharedPointer<Editable> object, bool reset)
    {
        auto&& riverMod = object.dynamicCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
        if (!riverMod)
            return;

        auto matches = match(index(0, 0), ModRiverTreeItem::CustomRoles::Guid, riverMod->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<ModRiverTreeItem*>(match.internalPointer());
        item->setData(0, riverMod->getName());

        emit dataChanged(match, match);
    }

    void QModRiverTreeModel::loadRiverMods()
    {
        beginResetModel();

        for (auto&& mod : Generation::Data::get()->getTerrainMods()[Generation::ETerrainMod::River])
        {
            auto&& riverMod = mod.staticCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
            auto newItem = new ModRiverTreeItem(riverMod, getRootItem());
            getRootItem()->appendChild(newItem);
        }

        endResetModel();
    }

    void QModRiverTreeModel::clear()
    {
        beginResetModel();
        getRootItem()->clearChildren();
        endResetModel();
    }

    void QModRiverTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;

        connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QModRiverTreeModel::treeSelectionChanged);
        connect(treeView, &QTreeView::doubleClicked, this, &QModRiverTreeModel::itemDoubleClicked);
        connect(treeView, &QTreeView::customContextMenuRequested, this, &QModRiverTreeModel::outlineContextMenuRequested);
    }

    void QModRiverTreeModel::treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)))
            return;

        treeView->selectionModel()->blockSignals(true);
        auto&& selectionMgr = Design::ModSelectionMgr::get();

        for (auto&& index : selected.indexes())
        {
            auto* item = static_cast<ModRiverTreeItem*>(index.internalPointer());

            QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>> river;
            for (auto&& mod : Generation::Data::get()->getTerrainMods()[Generation::ETerrainMod::River])
            {
                if (mod->getGuid() == item->modGuid)
                    river = mod.staticCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
            }

            selectionMgr->setSelection<Generation::ETerrainMod::River>({ river });
        }

        treeView->selectionModel()->blockSignals(false);
    }

    void QModRiverTreeModel::itemDoubleClicked(const QModelIndex& idx)
    {
        QOmnigenViewportSection::getActiveViewport()->tryMoveToSelection();
    }

    void QModRiverTreeModel::outlineContextMenuRequested(const QPoint& pos)
    {
        QModelIndex index = treeView->indexAt(pos);

        if (auto* item = static_cast<ModRiverTreeItem*>(index.internalPointer()))
        {
            QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>> river;
            for (auto&& mod : Generation::Data::get()->getTerrainMods()[Generation::ETerrainMod::River])
            {
                if (mod->getGuid() == item->modGuid)
                    river = mod.staticCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
            }

            QMenu* contextMenu = RiverSelection::requestContextMenu(river);
            contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
        }
    }
}