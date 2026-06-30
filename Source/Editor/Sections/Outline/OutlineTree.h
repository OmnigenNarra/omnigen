#pragma once
#include <QTreeView>
#include "Scene/Generation/OmnigenGenerationStage.h"

namespace Design
{
    template<EGenerationStage GS>
    class StageTools;
}

class OutlineTreeItem
{
public:
    explicit OutlineTreeItem(const std::vector<QVariant>& inRowData, OutlineTreeItem* parent = nullptr);
    ~OutlineTreeItem();

    virtual QVariant getDataByRole(int role) const;

    auto* getParentItem() { return parentItem; }

    OutlineTreeItem* child(int row);
    int getChildIndex(OutlineTreeItem* child) const;
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    int row() const;

    void setData(int col, QVariant value);
    void appendChild(OutlineTreeItem* child);
    void removeChild(OutlineTreeItem* child);
    void clearChildren();

private:
    OutlineTreeItem* parentItem;
    std::vector<OutlineTreeItem*> childItems;

    std::vector<QVariant> rowData;

    template<EGenerationStage>
    friend class Design::StageTools;
};

class OutlineTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    OutlineTreeModel(QObject* parent = nullptr);

    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    virtual QModelIndex parent(const QModelIndex& index) const override;
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    auto* getRootItem() const { return rootItem.get(); }

private:
    QScopedPointer<OutlineTreeItem> rootItem;

    template<EGenerationStage>
    friend class Design::StageTools;
};