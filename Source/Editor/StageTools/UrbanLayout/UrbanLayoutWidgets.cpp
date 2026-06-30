#include "stdafx.h"
#include "Omnigen.h"
#include "UrbanLayoutWidgets.h"
#include "UrbanLayoutSelection.h"
#include "Editor/StageTools/StageTools.h"
#include "StageToolsUrbanLayout.h"
#include "Utils/PlatformMisc.h"
#include "Utils/CoreUtils.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

namespace Design
{
    UrbanLayoutTreeItem::UrbanLayoutTreeItem(const QSharedPointer<Generation::UrbanSuggestion>& suggestion, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ suggestion->getName() }, parent),
        guid(suggestion->getGuid())
    {
    }

    QVariant UrbanLayoutTreeItem::getDataByRole(int role) const
    {
        if (role == Guid)
            return guid;

        return {};
    }

    void QUrbanLayoutTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QUrbanLayoutTreeModel::selectSuggestion(qint64 guid)
    {
        auto matches = match(index(0, 0), UrbanLayoutTreeItem::CustomRoles::Guid, guid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        QItemSelection s(match, match);

        s.merge(treeView->selectionModel()->selection(), QItemSelectionModel::SelectionFlag::Select);
        treeView->selectionModel()->select(s, QItemSelectionModel::SelectionFlag::Select);
    }

    void QUrbanLayoutTreeModel::loadSuggestions()
    {
        beginResetModel();
        for (auto&& suggestion : Generation::Data::get()->getUrbanSuggestions())
        {
            auto newItem = new UrbanLayoutTreeItem(suggestion, getRootItem());
            getRootItem()->appendChild(newItem);
        }
        endResetModel();
    }

    void QUrbanLayoutTreeModel::clear()
    {
        beginResetModel();
        getRootItem()->clearChildren();
        endResetModel();
    }

    void QUrbanLayoutTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;
        connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QUrbanLayoutTreeModel::treeSelectionChanged);
        connect(treeView, &QTreeView::doubleClicked, this, &QUrbanLayoutTreeModel::itemDoubleClicked);
        connect(treeView, &QTreeView::customContextMenuRequested, this, &QUrbanLayoutTreeModel::outlineContextMenuRequested);
    }

    void QUrbanLayoutTreeModel::treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)))
            return;

        treeView->selectionModel()->blockSignals(true);

        for (auto&& index : selected.indexes())
        {
            auto* item = static_cast<UrbanLayoutTreeItem*>(index.internalPointer());

            auto suggestion = Generation::Data::get()->findUrbanSuggestionByGuid(item->guid);
            UrbanLayoutSelectionMgr::get()->setSelection<Design::EUrbanLayoutSelection::SuggestionHandle>({ suggestion->getHandle() });
        }

        treeView->selectionModel()->blockSignals(false);
    }

    void QUrbanLayoutTreeModel::itemDoubleClicked(const QModelIndex& idx)
    {
        QOmnigenViewportSection::getActiveViewport()->tryMoveToSelection();
    }

    void QUrbanLayoutTreeModel::outlineContextMenuRequested(const QPoint& pos)
    {
        QModelIndex index = treeView->indexAt(pos);

        if (auto* item = static_cast<UrbanLayoutTreeItem*>(index.internalPointer()))
        {
            auto suggestion = Generation::Data::get()->findUrbanSuggestionByGuid(item->guid);

            QMenu* contextMenu = SuggestionSelection::requestContextMenu(suggestion->getHandle());

            contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
        }
    }

    QWidget* StageTools<EGenerationStage::UrbanLayout>::createOutlineToolbar()
    {
        auto* mainWidget = new QWidget();

        mainWidget->setContentsMargins(0, 0, 0, 0);
        mainWidget->setMaximumWidth(5000);

        auto* mainLayout = new QGridLayout(mainWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        return mainWidget;
    }
}