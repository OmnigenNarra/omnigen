#include "stdafx.h"
#include "OmnigenCameraSection.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Source/Editor/Dialogs/Preferences/OmnigenPreferences.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Omnigen.h"

#include <QPushButton>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include <QMenu>
#include <QAction>
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

#define VP_INDEX QOmnigenViewportSection::getActiveViewport()->getViewportIndex()

CameraSystemTreeModel::CameraSystemTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , rootItem(new CameraSystemTreeItem({QIcon(), QString()}, 0))
{
}

QModelIndex CameraSystemTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    CameraSystemTreeItem* parentItem;

    if (!parent.isValid())
        parentItem = rootItem.get();
    else
        parentItem = static_cast<CameraSystemTreeItem*>(parent.internalPointer());

    CameraSystemTreeItem* childItem = parentItem->child(row);

    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex CameraSystemTreeModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return QModelIndex();

    CameraSystemTreeItem* childItem = static_cast<CameraSystemTreeItem*>(index.internalPointer());
    CameraSystemTreeItem* parentItem = childItem->getParentItem();

    if (parentItem == rootItem.get() || !parentItem)
        return QModelIndex();

    return createIndex(parentItem->childNumber(), 0, parentItem);
}

int CameraSystemTreeModel::rowCount(const QModelIndex& parent) const
{
    CameraSystemTreeItem* parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem.get();
    else
        parentItem = static_cast<CameraSystemTreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

int CameraSystemTreeModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return static_cast<CameraSystemTreeItem*>(parent.internalPointer())->columnCount();
    return rootItem->columnCount();
}

CameraSystemTreeItem* CameraSystemTreeModel::getItem(const QModelIndex& index) const
{
    if (index.isValid()) {
        CameraSystemTreeItem* item = static_cast<CameraSystemTreeItem*>(index.internalPointer());
        if (item)
            return item;
    }
    return rootItem.get();
}

bool CameraSystemTreeModel::removeRows(int position, int rows, const QModelIndex& parent)
{
    CameraSystemTreeItem* parentItem = getItem(parent);
    if (!parentItem)
        return false;

    beginRemoveRows(parent, position, position + rows - 1);
    const bool success = parentItem->removeChildren(position, rows);
    endRemoveRows();

    return success;
}

QModelIndex CameraSystemTreeModel::getIndex(CameraSystemTreeItem* item)
{
    return createIndex(item->childNumber(), 0, item);
}

QVariant CameraSystemTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    CameraSystemTreeItem* item = static_cast<CameraSystemTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole)
        return item->data(1);
    else if (role == Qt::DecorationRole)
        return item->data(0);
    else
        return QVariant();
}

Qt::ItemFlags CameraSystemTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant CameraSystemTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return columnNames[section];

    return QVariant();
}

CameraSystemTreeItem::CameraSystemTreeItem(const std::vector<QVariant>& data, CameraSystemTreeItem* parent)
    : itemData(data)
    , parentItem(parent)
{}

CameraSystemTreeItem::~CameraSystemTreeItem()
{
    qDeleteAll(childItems);
}

void CameraSystemTreeItem::appendChild(CameraSystemTreeItem* item)
{
    childItems.push_back(item);
}

CameraSystemTreeItem* CameraSystemTreeItem::child(int row)
{
    if (row < 0 || row >= childItems.size())
        return nullptr;
    return childItems.at(row);
}

int CameraSystemTreeItem::childCount() const
{
    return childItems.size();
}

int CameraSystemTreeItem::columnCount() const
{
    return itemData.size();
}

QVariant CameraSystemTreeItem::data(int column) const
{
    if (column < 0 || column >= itemData.size())
        return QVariant();

    return itemData.at(column);
}

CameraSystemTreeItem* CameraSystemTreeItem::getParentItem()
{
    return parentItem;
}

int CameraSystemTreeItem::childNumber() const
{
    if (parentItem)
        return indexOf(parentItem->childItems, const_cast<CameraSystemTreeItem*>(this));

    return 0;
}

bool CameraSystemTreeItem::removeChildren(int position, int count)
{
    if (position < 0 || position + count > childItems.size())
        return false;

    for (int row = 0; row < count; ++row)
        delete childItems[position + row];

    childItems.erase(childItems.begin() + position, childItems.begin() + position + count);
    return true;
}

bool CameraSystemTreeItem::setData(int column, const QVariant& value)
{
    if (column < 0 || column >= itemData.size())
        return false;

    itemData[column] = value;
    return true;
}

QOmnigenCameraSection::QOmnigenCameraSection(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout();
    auto* bottomLayout = new QHBoxLayout();
    auto* propertiesBarLayout = createCameraSystemProperties();

    parent->setLayout(layout);

    treeView = new QTreeView();
    treeView->setHeaderHidden(true);
    treeView->setStyleSheet("padding: 0ex;");
    treeView->setModel(&model);
    treeView->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    treeView->setMinimumSize(250, 50);
    treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView->setUniformRowHeights(true);
    treeView->setIconSize(QSize(75, 42));
    treeView->hideColumn(1);
    treeView->setIndentation(5);

    layout->addWidget(treeView);
    layout->addLayout(bottomLayout);
    bottomLayout->addLayout(propertiesBarLayout);
    bottomLayout->addStretch(100);
    layout->setSizeConstraint(QLayout::SizeConstraint::SetNoConstraint);
    layout->setContentsMargins(10, 10, 0, 10);

    connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QOmnigenCameraSection::changeSelectedItem);
    connect(treeView, &QTreeView::clicked, this, [this]()
        {
            OmnigenCameraMgr::get()->changeActiveCameraForViewport(VP_INDEX, selectedItem->data(1).toString());
            OmnigenCameraMgr::get()->getActiveCamera(VP_INDEX)->returnToSavedPosition();
            QOmnigenViewportSection::repaintAll(true);
        });
    connect(treeView, &QTreeView::customContextMenuRequested, this, &QOmnigenCameraSection::cameraContextMenuRequested);
    connect(OmnigenCameraMgr::get(), &OmnigenCameraMgr::cameraStateChanged, this, &QOmnigenCameraSection::restartItems);

    parent->setMinimumSize(250, 200);
    parent->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    parent->show();

    restartItems();
}

void QOmnigenCameraSection::addCameraItem(const QString& cameraName)
{
    auto newCam = OmnigenCameraMgr::get()->getCamera(cameraName);
    auto thumbnail = QIcon(newCam->getCameraThumbnail());
    auto* newItem = new CameraSystemTreeItem({ thumbnail, cameraName }, model.getRootItem());
    model.getRootItem()->appendChild(newItem);
}

void QOmnigenCameraSection::restartItems()
{
    model.beginResetModel();

    if(model.getRootItem()->childCount() > 0)
        model.removeRows(0, model.getRootItem()->childCount(), model.getIndex(model.getRootItem()));

    auto&& cameras = OmnigenCameraMgr::get()->getAllCameras();

    // Since 'cameras' is sorted alphabetically, iteration by saved cam position is required
    QMap<int, QString> camPosMap;
    for (auto&& it = cameras.keyValueBegin(); it != cameras.keyValueEnd(); it++)
        camPosMap.insert((*it).second->getCameraItemPosition(), (*it).first);

    for (auto&& it = camPosMap.keyValueBegin(); it != camPosMap.keyValueEnd(); it++)
        addCameraItem((*it).second);

    model.endResetModel();

    selectFocusedCamera(OmnigenCameraMgr::get()->getActiveCameraName(VP_INDEX));
}

void QOmnigenCameraSection::selectFocusedCamera(const QString& camName)
{
    auto matches = model.match(model.index(0, 0, model.getIndex(model.getRootItem())), Qt::DisplayRole, camName, 1, Qt::MatchStartsWith);
    if (matches.isEmpty())
    {
        OmniLog(ELoggingLevel::Warn) <<= "No camera found";
        return;
    }

    treeView->selectionModel()->clear();
    treeView->selectionModel()->select(model.getIndex(model.getItem(matches.front())), QItemSelectionModel::Select);

    selectedItem = model.getItem(matches.front());

    emit selectedItemChanged();
}

QLayout* QOmnigenCameraSection::createCameraSystemProperties()
{
    auto* layout = new QGridLayout();

    // Camera Name
    auto* nameLabel = new QLabel("Camera Name:");
    auto* cameraName = new QLineEdit("");

    // Camera Speed
    auto* speedLabel = new QLabel("Camera Speed:");
    auto* speedWidget = new QLineEdit();

	// Min View Distance
	auto* minViewLabel = new QLabel("Min View Distance:");
	auto* minViewDistance = new QLineEdit();

    // Max View Distance
    auto* maxViewLabel = new QLabel("Max View Distance:");
    auto* maxViewDistance = new QLineEdit();

    minViewDistance->setValidator(new QDoubleValidator(1.0, std::numeric_limits<double>::max(), 0));
    maxViewDistance->setValidator(new QDoubleValidator(1.0, std::numeric_limits<double>::max(), 0));

    // Bottom buttons
    auto* createNewCamera = new QPushButton("Create New Camera");
    auto* savePositionButton = new QPushButton("Save Position");
    auto* returnButton = new QPushButton("Return to Saved Position");

    connect(this, &QOmnigenCameraSection::selectedItemChanged, this, [=, this]()
        {
            cameraName->setText(selectedItem->data(1).toString());
            speedWidget->setText(QString::number(OmnigenCameraMgr::get()->getCamera(selectedItem->data(1).toString())->getCameraSpeed()));
            minViewDistance->setText(QString::number(OmnigenCameraMgr::get()->getCamera(selectedItem->data(1).toString())->getViewMinDistance()));
            maxViewDistance->setText(QString::number(OmnigenCameraMgr::get()->getCamera(selectedItem->data(1).toString())->getViewDistance()));
        });
    connect(createNewCamera, &QPushButton::clicked, this, [this]() 
        {
            QString camName = QString("Camera #%1").arg(model.getRootItem()->childCount() + 1);
            auto* cm = OmnigenCameraMgr::get();
            auto&& prevName = cm->getActiveCameraName(VP_INDEX);

            while (!cm->cloneCamera(camName, prevName))
                camName = QString(camName + "*");

            model.beginResetModel();
            addCameraItem(camName);
            selectFocusedCamera(camName);
            model.endResetModel();
        });
    connect(cameraName, &QLineEdit::editingFinished, this, [cameraName, this]()
        {
            // Prevents unnecessary name changes
            if (cameraName->text() != OmnigenCameraMgr::get()->getActiveCameraName(VP_INDEX))
            {
                // Ensure name uniqueness
                while (true)
                {
                    if (OmnigenCameraMgr::get()->getCamera(cameraName->text()))
                    {
                        cameraName->setText(cameraName->text() + "*");
                        continue;
                    }
                    break;
                }

                OmnigenCameraMgr::get()->changeCameraName(cameraName->text(), selectedItem->data(1).toString());
                selectedItem->setData(1, cameraName->text());
                model.dataChanged(model.getIndex(selectedItem), model.getIndex(selectedItem));
            }
        });
    connect(speedWidget, &QLineEdit::editingFinished, this, [this, speedWidget]() 
        { 
            if (selectedItem->data(1) == OmnigenCameraMgr::get()->getActiveCameraName(VP_INDEX))
                OmnigenCameraMgr::get()->getActiveCamera(VP_INDEX)->setCameraSpeed(fromQString<float>(speedWidget->text()));

            speedWidget->clearFocus();
        });
	connect(minViewDistance, &QLineEdit::editingFinished, this, [=, this]()
		{
			if (selectedItem->data(1) == OmnigenCameraMgr::get()->getActiveCameraName(VP_INDEX))
				OmnigenCameraMgr::get()->getActiveCamera(VP_INDEX)->setViewMinDistance(minViewDistance->text().toFloat());
		});
	connect(maxViewDistance, &QLineEdit::editingFinished, this, [=, this]()
		{
			if (selectedItem->data(1) == OmnigenCameraMgr::get()->getActiveCameraName(VP_INDEX))
				OmnigenCameraMgr::get()->getActiveCamera(VP_INDEX)->setViewDistance(maxViewDistance->text().toFloat());
		});
    connect(savePositionButton, &QPushButton::clicked, this, [this]()
        {
            auto* cm = OmnigenCameraMgr::get();
            cm->setThumbnailForCamera(cm->getActiveCameraName(VP_INDEX));
            cm->getActiveCamera(VP_INDEX)->setReturnPosition();

            selectedItem->setData(0, cm->getActiveCamera(VP_INDEX)->getCameraThumbnail());
            model.dataChanged(model.getIndex(selectedItem), model.getIndex(selectedItem));
        });
    connect(returnButton, &QPushButton::clicked, this, [this]()
        {
            OmnigenCameraMgr::get()->getActiveCamera(VP_INDEX)->returnToSavedPosition();
        });

    layout->addWidget(nameLabel, 0, 0);
    layout->addWidget(cameraName, 0, 1);
    layout->addWidget(speedLabel, 1, 0);
    layout->addWidget(speedWidget, 1, 1);
    layout->addWidget(minViewLabel, 2, 0);
    layout->addWidget(minViewDistance, 2, 1);
	layout->addWidget(maxViewLabel, 3, 0);
	layout->addWidget(maxViewDistance, 3, 1);
    layout->addWidget(savePositionButton, 4, 1);
    layout->addWidget(createNewCamera, 4, 0);
    layout->addWidget(returnButton, 5, 0, 1, 2);

    return layout;
}

void QOmnigenCameraSection::changeSelectedItem(const QItemSelection& selected, const QItemSelection& deselected)
{
    for (auto&& index : selected.indexes())
    {
        selectedItem = static_cast<CameraSystemTreeItem*>(index.internalPointer());
    }

    emit selectedItemChanged();
}

void QOmnigenCameraSection::cameraContextMenuRequested(const QPoint& pos)
{
    QModelIndex index = treeView->indexAt(pos);
    QMenu* contextMenu = new QMenu(this);

    if (auto* item = static_cast<CameraSystemTreeItem*>(index.internalPointer()))
    {
        // Don't allow to delete last 4 cameras
        if (model.getRootItem()->childCount() > 4)
        {
            QAction* deleteCamera = new QAction(QString("Delete %1").arg(selectedItem->data(1).toString()), this);
            connect(deleteCamera, &QAction::triggered, this, [this, item]()
                {
                    int idx = -1;
                    for(auto cam : OmnigenCameraMgr::get()->getAllActiveCameras())
                        if (cam == selectedItem->data(1).toString())
                            idx = OmnigenCameraMgr::get()->getAllActiveCameras().key(cam);

                    model.beginResetModel();
                    OmnigenCameraMgr::get()->removeCamera(selectedItem->data(1).toString());
                    model.removeRows(item->childNumber(), 1, model.getIndex(model.getRootItem()));
                    model.endResetModel();

                    // Reassign camera position after deleting an item
                    for (int i = 0; i < model.getRootItem()->childCount(); ++i)
                        OmnigenCameraMgr::get()->getCamera(model.getRootItem()->child(i)->data(1).toString())->setCameraItemPosition(i);

                    // Change active camera to first valid
                    if (idx > -1)
                    {
                        treeView->selectionModel()->select(model.getIndex(model.getRootItem()->child(0)), QItemSelectionModel::Select);
                        OmnigenCameraMgr::get()->changeActiveCameraForViewport(idx, selectedItem->data(1).toString());
                    }

                   //restartItems();
                });
            contextMenu->addAction(deleteCamera);
        }
        contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
    }
}

#undef VP_INDEX
