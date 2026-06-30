#pragma once
#include <QTreeView>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>

class HistoryStackTreeItem;
class History;

// A view-only widget showing current undo/redo stack.
// Adding go-to functionality is possible.

class HistoryStackTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    HistoryStackTreeModel(QObject* parent = nullptr);

    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    virtual QModelIndex parent(const QModelIndex& index) const override;
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    HistoryStackTreeItem* getRootItem() const { return rootItem.get(); }

private:
    QScopedPointer<HistoryStackTreeItem> rootItem;

    friend class QOmnigenHistoryStackSection;
};

class HistoryStackTreeItem
{
public:
    explicit HistoryStackTreeItem(const std::vector<QVariant>& data, unsigned int id, HistoryStackTreeItem* parentItem = nullptr);
    ~HistoryStackTreeItem();

    bool appendChild(HistoryStackTreeItem* child);
    void removeChild(HistoryStackTreeItem* child);
    void clearChildren();
    HistoryStackTreeItem* child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    int row() const;
    HistoryStackTreeItem* getParentItem();

    const unsigned int ID;

private:
    std::vector<HistoryStackTreeItem*> childItems;
    std::vector<QVariant> itemData;
    HistoryStackTreeItem* parentItem;
};

class QOmnigenHistoryStackSection : public QWidget
{
    Q_OBJECT

public:
    QOmnigenHistoryStackSection(QWidget* parent);

    QTreeView* treeView = nullptr;
    HistoryStackTreeModel model;

    void updateStack(int present = -1);
    void loadHistoryObject(HistoryStackTreeItem* parent, History* object);

private:
    void saveExpansionState(QSet<int>* savedData, HistoryStackTreeItem* item = nullptr);
    void loadExpansionState(const QSet<int>& savedData);
};