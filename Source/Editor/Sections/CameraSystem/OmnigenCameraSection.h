#pragma once
#include <QTreeView>
#include "Editor/Sections/Viewport/OmnigenViewport.h"

class CameraSystemTreeItem;
class Omnigen;

// UI for viewport -> camera system
// Ideally all camera settings should be incorporated into the viewport itself.

class CameraSystemTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    CameraSystemTreeModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    CameraSystemTreeItem* getItem(const QModelIndex& index) const;
    bool removeRows(int position, int rows, const QModelIndex& parent);
    QModelIndex getIndex(CameraSystemTreeItem* item);

    CameraSystemTreeItem* getRootItem() const { return rootItem.get(); };

private:
    std::array<QString, 2> columnNames = {"Thumbnail", "Camera Name"};
    QScopedPointer<CameraSystemTreeItem> rootItem;

    friend class QOmnigenCameraSection;
};

class CameraSystemTreeItem
{
public:

    explicit CameraSystemTreeItem(const std::vector<QVariant>& data, CameraSystemTreeItem* parent = nullptr);
    ~CameraSystemTreeItem();

    void appendChild(CameraSystemTreeItem* child);

    CameraSystemTreeItem* child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    CameraSystemTreeItem* getParentItem();
    int childNumber() const;
    bool removeChildren(int position, int count);
    bool setData(int column, const QVariant& value);

private:
    std::vector<CameraSystemTreeItem*> childItems;
    std::vector<QVariant> itemData;
    CameraSystemTreeItem* parentItem;
};

class QOmnigenCameraSection : public QWidget
{
    Q_OBJECT

public:
    QOmnigenCameraSection(QWidget* parent);
    void restartItems();
    void selectFocusedCamera(const QString& camName);

signals:
    void selectedItemChanged();

private slots:
    void changeSelectedItem(const QItemSelection& selected, const QItemSelection& deselected);
    void cameraContextMenuRequested(const QPoint& pos);

private:
    QLayout* createCameraSystemProperties();
    void addCameraItem(const QString& cameraName);

    QTreeView* treeView = nullptr;
    CameraSystemTreeModel model;
    CameraSystemTreeItem* selectedItem;
};
