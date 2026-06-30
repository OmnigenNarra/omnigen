#include "stdafx.h"
#include "OmnigenHistoryStackSection.h"
#include "Editor/Sections/History/History.h"
#include "Utils/CoreUtils.h"

#include <QHBoxLayout>

HistoryStackTreeModel::HistoryStackTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , rootItem(new HistoryStackTreeItem({ "" }, 0))
{
}

int HistoryStackTreeModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return static_cast<HistoryStackTreeItem*>(parent.internalPointer())->columnCount();
    return rootItem->columnCount();
}

QVariant HistoryStackTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    HistoryStackTreeItem* item = static_cast<HistoryStackTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole)
        return item->data(index.column());
    else if (role == Qt::UserRole)
        return item->ID;
    else
        return QVariant();
}

Qt::ItemFlags HistoryStackTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant HistoryStackTreeModel::headerData(int section, Qt::Orientation orientation,
    int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return rootItem->data(section);

    return QVariant();
}

QModelIndex HistoryStackTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    HistoryStackTreeItem* parentItem;

    if (!parent.isValid())
        parentItem = rootItem.get();
    else
        parentItem = static_cast<HistoryStackTreeItem*>(parent.internalPointer());

    HistoryStackTreeItem* childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex HistoryStackTreeModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return QModelIndex();

    HistoryStackTreeItem* childItem = static_cast<HistoryStackTreeItem*>(index.internalPointer());
    HistoryStackTreeItem* parentItem = childItem->getParentItem();

    if (!parentItem || (parentItem == rootItem.get()))
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int HistoryStackTreeModel::rowCount(const QModelIndex& parent) const
{
    HistoryStackTreeItem* parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem.get();
    else
        parentItem = static_cast<HistoryStackTreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

HistoryStackTreeItem::HistoryStackTreeItem(const std::vector<QVariant>& data, unsigned int id, HistoryStackTreeItem* parent)
    : itemData(data)
    , parentItem(parent)
    , ID(id)
{
}

HistoryStackTreeItem::~HistoryStackTreeItem()
{
    qDeleteAll(childItems);
}

bool HistoryStackTreeItem::appendChild(HistoryStackTreeItem* item)
{
    for (auto&& c : childItems)
        if (c->ID == item->ID)
            return false;

    item->parentItem = this;
    childItems << item;
    return true;
}

void HistoryStackTreeItem::removeChild(HistoryStackTreeItem* child)
{
    removeOne(childItems, child);
    delete child;
}

void HistoryStackTreeItem::clearChildren()
{
    for (auto* c : childItems)
    {
        c->clearChildren();
        delete c;
    }

    childItems.clear();
}

HistoryStackTreeItem* HistoryStackTreeItem::child(int row)
{
    if (row < 0 || row >= childItems.size())
        return nullptr;
    return childItems.at(row);
}

int HistoryStackTreeItem::childCount() const
{
    return childItems.size();
}

int HistoryStackTreeItem::columnCount() const
{
    return itemData.size();
}

QVariant HistoryStackTreeItem::data(int column) const
{
    if (column < 0 || column >= itemData.size())
        return QVariant();

    return itemData.at(column);
}

HistoryStackTreeItem* HistoryStackTreeItem::getParentItem()
{
    return parentItem;
}

int HistoryStackTreeItem::row() const
{
    if (parentItem)
        return indexOf(parentItem->childItems, const_cast<HistoryStackTreeItem*>(this));

    return 0;
}

QOmnigenHistoryStackSection::QOmnigenHistoryStackSection(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    treeView = new QTreeView;
    treeView->setHeaderHidden(true);
    treeView->setStyleSheet("padding: 0ex;");
    treeView->setModel(&model);
    layout->addWidget(treeView);
    treeView->show();

    resize(INT_MAX, INT_MAX);
    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum));

    // Disallow click-based selection
    treeView->setSelectionMode(QAbstractItemView::NoSelection);
}

void QOmnigenHistoryStackSection::updateStack(int present)
{
    auto* rootItem = model.getRootItem();

    ///////////////////////////////////////////////////////////////
    // Save states
    QSet<int> expansionSet;
    saveExpansionState(&expansionSet);

    // Reset begin
    model.beginResetModel();

    auto stackData = History::GetContext()->GetStackData();
    rootItem->clearChildren();

    for (auto* historyObject : stackData)
        if (historyObject)
            loadHistoryObject(rootItem, historyObject);

    model.endResetModel();
    // Reset end

    if (present >= 0)
        treeView->selectionModel()->select(QItemSelection(model.index(present - 1, 0), model.index(present - 1, 0)), QItemSelectionModel::Select);

    // Load states
    loadExpansionState(expansionSet);
}

void QOmnigenHistoryStackSection::loadHistoryObject(HistoryStackTreeItem* parent, History* object)
{
    auto* newItem = new HistoryStackTreeItem({ QString::fromStdString(object->GetLabel()) }, object->GetId(), parent);
    if (!parent->appendChild(newItem))
        return;

    for (auto* subobject : object->GetSubcontext().GetStackData())
        if (subobject)
            loadHistoryObject(newItem, subobject);
}

void QOmnigenHistoryStackSection::saveExpansionState(QSet<int>* savedData, HistoryStackTreeItem* item)
{
    if (!item)
        item = model.rootItem.get();

    auto matches = model.match(model.index(0,0), Qt::UserRole, item->ID, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
    if (!matches.isEmpty() && treeView->isExpanded(matches.front()))
        savedData->insert(item->ID);

    for (int i = 0; i < item->childCount(); ++i)
        saveExpansionState(savedData, item->child(i));
}

void QOmnigenHistoryStackSection::loadExpansionState(const QSet<int>& savedData)
{
    for (int id : savedData)
    {
        auto matches = model.match(model.index(0,0), Qt::UserRole, id, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        treeView->expand(matches.front());
    }
}