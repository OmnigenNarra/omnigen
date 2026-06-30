#include "stdafx.h"
#include "OutlineTree.h"
#include "Utils/CoreUtils.h"

OutlineTreeItem::OutlineTreeItem(const std::vector<QVariant>& inRowData, OutlineTreeItem* parent /*= nullptr*/) 
    : parentItem(parent)
    , rowData(inRowData)
{
}

OutlineTreeItem::~OutlineTreeItem()
{
    qDeleteAll(childItems);
}

QVariant OutlineTreeItem::getDataByRole(int role) const
{
    return QVariant();
}

void OutlineTreeItem::appendChild(OutlineTreeItem* child)
{
    child->parentItem = this;
    childItems.push_back(child);
}

void OutlineTreeItem::removeChild(OutlineTreeItem* child)
{
    removeOne(childItems, child);
    delete child;
}

void OutlineTreeItem::clearChildren()
{
    for (auto* c : childItems)
    {
        c->clearChildren();
        delete c;
    }

    childItems.clear();
}

OutlineTreeItem* OutlineTreeItem::child(int row)
{
    if (row < 0 || row >= childItems.size())
        return nullptr;

    return childItems.at(row);
}

int OutlineTreeItem::childCount() const
{
    return childItems.size();
}

int OutlineTreeItem::columnCount() const
{
    return rowData.size();
}

QVariant OutlineTreeItem::data(int column) const
{
    if (column < 0 || column >= rowData.size())
        return QVariant();

    return rowData.at(column);
}

int OutlineTreeItem::row() const
{
    if (parentItem)
        return indexOf(parentItem->childItems, const_cast<OutlineTreeItem*>(this));

    return 0;
}

int OutlineTreeItem::getChildIndex(OutlineTreeItem* child) const
{
    return indexOf(childItems, child);
}

void OutlineTreeItem::setData(int col, QVariant value)
{
    rowData[col] = value;
}

/// <summary>
/// /////////////////////////////////////////////////////////////////////////////////////
/// </summary>
/// <param name="parent"></param>

OutlineTreeModel::OutlineTreeModel(QObject* parent /*= nullptr*/)
    : QAbstractItemModel(parent)
    , rootItem(new OutlineTreeItem({ "" }))
{
}

QVariant OutlineTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    auto* item = static_cast<OutlineTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole)
        return item->data(index.column());

    return item->getDataByRole(role);
}

Qt::ItemFlags OutlineTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant OutlineTreeModel::headerData(int section, Qt::Orientation orientation, int role /*= Qt::DisplayRole*/) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return rootItem->data(section);

    return QVariant();
}

QModelIndex OutlineTreeModel::index(int row, int column, const QModelIndex& parent /*= QModelIndex()*/) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    auto* parentItem = parent.isValid()
        ? static_cast<OutlineTreeItem*>(parent.internalPointer())
        : rootItem.get();

    if (auto* childItem = parentItem->child(row); childItem)
        return createIndex(row, column, childItem);

    return QModelIndex();
}

QModelIndex OutlineTreeModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return QModelIndex();

    auto* childItem = static_cast<OutlineTreeItem*>(index.internalPointer());
    auto* parentItem = childItem->getParentItem();

    if (!parentItem || (parentItem == rootItem.get()))
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int OutlineTreeModel::rowCount(const QModelIndex& parent /*= QModelIndex()*/) const
{
    if (parent.column() > 0)
        return 0;

    auto* parentItem = parent.isValid()
        ? static_cast<OutlineTreeItem*>(parent.internalPointer())
        : rootItem.get();

    return parentItem->childCount();
}

int OutlineTreeModel::columnCount(const QModelIndex& parent /*= QModelIndex()*/) const
{
    if (parent.isValid())
        return static_cast<OutlineTreeItem*>(parent.internalPointer())->columnCount();

    return rootItem->columnCount();
}
