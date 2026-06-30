#include "stdafx.h"
#include "RidgesWidgets.h"
#include "Utils/PlatformMisc.h"
#include "Omnigen.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "RidgesSelection.h"
#include "StageToolsRidges.h"
#include "Editor/StageTools/StageTools.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

#include <QToolBar>

namespace Design
{
    RidgesTreeItem::RidgesTreeItem(const QSharedPointer<DRidgeMarker>& ridge, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ ridge->getName() }, parent)
        , guid(ridge->getGuid())
    {
    }

    QVariant RidgesTreeItem::getDataByRole(int role) const
    {
        if (role == Guid)
            return guid;

        return {};
    }

    void QRidgesTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QRidgesTreeModel::selectRidge(qint64 guid)
    {
        auto matches = match(index(0, 0), RidgesTreeItem::CustomRoles::Guid, guid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        QItemSelection s(match, match);

        s.merge(treeView->selectionModel()->selection(), QItemSelectionModel::SelectionFlag::Select);
        treeView->selectionModel()->select(s, QItemSelectionModel::SelectionFlag::Select);
    }

    void QRidgesTreeModel::addRidge(size_t typeHash, QSharedPointer<Editable> object)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        // Ensure no duplicates
        auto matches = match(index(0, 0), RidgesTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (!matches.isEmpty())
            return;

        auto ridge = object.dynamicCast<DRidgeMarker>();
        if (!ridge)
            return;

        beginResetModel();
        // If subridge, append entry to its parent
        auto itemParent = getRootItem();
        if (ridge->getParent())
        {
            auto parentMatch = match(index(0, 0), RidgesTreeItem::CustomRoles::Guid, ridge->getParent().lock()->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
            itemParent = static_cast<RidgesTreeItem*>(parentMatch.front().internalPointer());
        }

        auto newItem = new RidgesTreeItem(ridge, itemParent);
        itemParent->appendChild(newItem);

        endResetModel();
    }

    void QRidgesTreeModel::removeRidge(QSharedPointer<Editable> object)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        auto matches = match(index(0, 0), RidgesTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<RidgesTreeItem*>(matches.front().internalPointer());

        beginRemoveRows(match.parent(), match.row(), match.row());
        item->getParentItem()->removeChild(item);
        endRemoveRows();
    }

    void QRidgesTreeModel::updateRidge(QSharedPointer<Editable> object, bool reset)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        auto matches = match(index(0, 0), RidgesTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<RidgesTreeItem*>(match.internalPointer());
        item->setData(0, object.dynamicCast<DRidgeMarker>()->getName());

        emit dataChanged(match, match);
    }

    void QRidgesTreeModel::loadRidges()
    {
        loadRidges(Generation::Data::get()->getMarkers<DRidgeMarker>());
    }

    void QRidgesTreeModel::loadRidges(const std::vector<QSharedPointer<DRidgeMarker>>& ridgeMarkers)
    {
        beginResetModel();
        for (auto&& ridge : ridgeMarkers)
        {
            auto newItem = new RidgesTreeItem(ridge, getRootItem());
            getRootItem()->appendChild(newItem);

            // Load children of main ridge
            if(!(ridge->getChildren().empty()))
                loadSubridges(newItem);
        }
        endResetModel();
    }

    void QRidgesTreeModel::loadSubridges(RidgesTreeItem* ridgeItem)
    {
        auto ridge = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(getRidgeGuidChain(ridgeItem->guid));
        auto children = ridge->getChildren();
        for (auto&& child : children)
        {
            auto newChildItem = new RidgesTreeItem(child, ridgeItem);
            ridgeItem->appendChild(newChildItem);

            // Load children of subridge
            if (!(child->getChildren().empty()))
                loadSubridges(newChildItem);
        }
    }

    void QRidgesTreeModel::clear()
    {
        beginResetModel();

        getRootItem()->clearChildren();

        endResetModel();
    }

    std::vector<qint64> QRidgesTreeModel::getRidgeGuidChain(qint64 guid)
    {
        auto matches = match(index(0, 0), RidgesTreeItem::CustomRoles::Guid, guid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        Q_ASSERT(!(matches.isEmpty()));

        auto&& match = matches.front();
        auto* item = static_cast<RidgesTreeItem*>(match.internalPointer());
        std::vector<qint64> guidChain = { guid };

        while(true)
        {
            auto* parent = static_cast<RidgesTreeItem*>(item->getParentItem());
            if (parent == getRootItem())
                break;

            item = parent;
            guidChain.emplace_back(parent->guid);
        }

        return guidChain;
    }

    void QRidgesTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;

        connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QRidgesTreeModel::treeSelectionChanged);
        connect(treeView, &QTreeView::doubleClicked, this, &QRidgesTreeModel::itemDoubleClicked);
        connect(treeView, &QTreeView::customContextMenuRequested, this, &QRidgesTreeModel::outlineContextMenuRequested);
    }

    void QRidgesTreeModel::treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)))
            return;

        treeView->selectionModel()->blockSignals(true);

        RidgesSelectionMgr::get()->clearSelection();

        for (auto&& index : selected.indexes())
        {
            auto* item = static_cast<RidgesTreeItem*>(index.internalPointer());
            auto ridge = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(getRidgeGuidChain(item->guid));
            RidgesSelectionMgr::get()->setSelection<Design::ERidgesSelection::Ridge>({ ridge });
        }

        treeView->selectionModel()->blockSignals(false);
    }

    void QRidgesTreeModel::itemDoubleClicked(const QModelIndex& idx)
    {
        QOmnigenViewportSection::getActiveViewport()->tryMoveToSelection();
    }

    void QRidgesTreeModel::outlineContextMenuRequested(const QPoint& pos)
    {
        QModelIndex index = treeView->indexAt(pos);

        if (auto* item = static_cast<RidgesTreeItem*>(index.internalPointer()))
        {
            auto ridge = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(getRidgeGuidChain(item->guid));
            QMenu* contextMenu = RidgeSelection::requestContextMenu(ridge);
            contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
        }
    }

    QWidget* StageTools<EGenerationStage::Ridges>::createOutlineToolbar()
    {
        auto* mainWidget = new QWidget();

        mainWidget->setContentsMargins(0, 0, 0, 0);
        auto* toolBar = new QToolBar();
        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        mainWidget->setMaximumWidth(5000);

        auto* mainLayout = new QGridLayout(mainWidget);

        mainLayout->addWidget(toolBar, 0, 0, 1, -1);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        auto* brushGroup = new QActionGroup(toolBar);
        brushGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        // Main ridge drawing
        auto* toggleRidgeDrawing = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Draw Ridges", brushGroup);
        toggleRidgeDrawing->setCheckable(true);

        // Ridge editing
        auto* toggleRidgeEdit = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Edit Ridge", brushGroup);
        toggleRidgeEdit->setCheckable(true);

        // 3D Ridge editing
        auto* toggle3DRidgeEdit = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Edit Ridge Height", brushGroup);
        toggle3DRidgeEdit->setCheckable(true);

        // Ridge Editing Brush Size Slider
        auto* ridgeEditBSize = new QSlider(Qt::Orientation::Horizontal);
        ridgeEditBSize->setMaximum(10);
        ridgeEditBSize->setMinimum(1);
        ridgeEditBSize->setValue(3);
        brushSize = 3;
        ridgeEditBSize->show();
        auto* ridgeEditSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));

        ridgeEditBSize->setVisible(false);
        ridgeEditSizeLabel->setVisible(false);

        // Ridge Editing Brush Strength Slider
        auto* ridgeEditBStr = new QSlider(Qt::Orientation::Horizontal);
        ridgeEditBStr->setMaximum(10);
        ridgeEditBStr->setMinimum(1);
        ridgeEditBStr->setValue(5);
        brushStrength = 5;
        ridgeEditBStr->show();
        auto* ridgeEditStrLabel = new QLabel("Brush Strength: " + QString::number(brushStrength * 20) + "%");

        ridgeEditBStr->setVisible(false);
        ridgeEditStrLabel->setVisible(false);

        // Highlight drawing toggle
        auto* ridgeEditHighlightToggle = new QCheckBox("Highlight points");
        ridgeEditHighlightToggle->setChecked(true);
        ridgeEditHighlightToggle->setVisible(false);
        connect(ridgeEditHighlightToggle, &QCheckBox::stateChanged, this, [this, ridgeEditHighlightToggle]
            {bDrawHighlights = ridgeEditHighlightToggle->isChecked(); });

        // Continuous editing or brush strokes toggle
        auto* ridgeEditStrokesToggle = new QCheckBox("Brush Strokes");
        ridgeEditStrokesToggle->setChecked(true);
        ridgeEditStrokesToggle->setVisible(false);
        connect(ridgeEditStrokesToggle, &QCheckBox::stateChanged, this, [this, ridgeEditStrokesToggle]
            {bBrushStrokes = ridgeEditStrokesToggle->isChecked(); });

        mainLayout->addWidget(ridgeEditSizeLabel, 2, 0);
        mainLayout->addWidget(ridgeEditBSize, 2, 1);
        mainLayout->addWidget(ridgeEditStrLabel, 3, 0);
        mainLayout->addWidget(ridgeEditBStr, 3, 1);
        mainLayout->addWidget(ridgeEditHighlightToggle, 4, 0);
        mainLayout->addWidget(ridgeEditStrokesToggle, 5, 0);

        // Ugly hack for the slider, as without setting a max width to main widget it expands indefinitely,
        // setting a fixed slider size does not work and there is no option to set max column width
        // Any better solutions are welcome; Will return to this during the Great UI Rework
        mainLayout->setColumnMinimumWidth(2, 4700);
        connect(ridgeEditBSize, &QSlider::valueChanged, this, [this, ridgeEditBSize, ridgeEditSizeLabel]
            {
                brushSize = ridgeEditBSize->value();
                ridgeEditSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });

        connect(ridgeEditBStr, &QSlider::valueChanged, this, [this, ridgeEditBStr, ridgeEditStrLabel]
            {
                brushStrength = ridgeEditBStr->value();
                ridgeEditStrLabel->setText("Brush Strength: " + QString::number(brushStrength * 20) + "%");
            });

        // Height Editing Toolbar
        auto* heightEditToolbar = new QToolBar();
        heightEditToolbar->setIconSize(QSize(40, 20));
        heightEditToolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        mainLayout->addWidget(heightEditToolbar, 1, 0, 1, -1);
        heightEditToolbar->setVisible(false);

        auto* toolsGroup = new QActionGroup(heightEditToolbar);
        toolsGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        // Sculpting Tool
        auto* sculptingToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Sculpting Tool", toolsGroup);
        sculptingToolButton->setCheckable(true);

        // Flatten Tool
        auto* flattenToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Flatten Tool", toolsGroup);
        flattenToolButton->setCheckable(true);

        // Smooth Tool
        auto* smoothToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Smooth Tool", toolsGroup);
        smoothToolButton->setCheckable(true);

        // Button logic
        connect(brushGroup, &QActionGroup::triggered, this, [=]()
            {
                if ((bIsRidgeEditing || bIsRidgeDrawing) && (!toggleRidgeEdit->isChecked() && !toggleRidgeDrawing->isChecked()))
                    changeTo3DRidges();
                else if (!bIsRidgeEditing && !bIsRidgeDrawing && (toggleRidgeEdit->isChecked() || toggleRidgeDrawing->isChecked()))
                    changeTo2DRidges();

                bIsRidgeEditing = toggleRidgeEdit->isChecked();
                bIsRidgeDrawing = toggleRidgeDrawing->isChecked();
                bHeightEditing = toggle3DRidgeEdit->isChecked();

                ridgeEditBSize->setVisible(toggleRidgeEdit->isChecked() || toggle3DRidgeEdit->isChecked());
                ridgeEditSizeLabel->setVisible(toggleRidgeEdit->isChecked() || toggle3DRidgeEdit->isChecked());

                ridgeEditBStr->setVisible(toggleRidgeEdit->isChecked() || toggle3DRidgeEdit->isChecked());
                ridgeEditStrLabel->setVisible(toggleRidgeEdit->isChecked() || toggle3DRidgeEdit->isChecked());
                ridgeEditHighlightToggle->setVisible(toggleRidgeEdit->isChecked());
                ridgeEditStrokesToggle->setVisible(toggleRidgeEdit->isChecked());

                heightEditToolbar->setVisible(toggle3DRidgeEdit->isChecked());
            });

        // Height Edit Button logic
        connect(toolsGroup, &QActionGroup::triggered, this, [=]()
            {
                bSculptTool = sculptingToolButton->isChecked();
                bFlattenTool = flattenToolButton->isChecked();
                bSmoothTool = smoothToolButton->isChecked();
            });

        // Main Ridge/subridge color-coding
        auto* ridgeColorToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Subridge Colors");
        ridgeColorToolButton->setCheckable(true);

        connect(ridgeColorToolButton, &QAction::triggered, this, [this, ridgeColorToolButton]
            {
                bool checked = ridgeColorToolButton->isChecked();

                if(!checked)
                {
                    std::vector<QSharedPointer<DRidgeMarker>> ridges;
                    Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

                    for (auto&& ridge : ridges)
                        ridge->setMarkerColor(QVector4D(1, 0, 0, 1));
                }
                else
                {
                    std::vector<QSharedPointer<DRidgeMarker>> ridges = Generation::Data::get()->getMarkers<DRidgeMarker>();
                    std::vector<QSharedPointer<DRidgeMarker>> nextRidges;
                    QVector4D newColor(1,0,0,1);
                    int idx = 0;
                    while (true)
                    {
                        for (auto&& ridge : ridges)
                        {
                            ridge->setMarkerColor(newColor);
                            auto&& children = ridge->getChildren();
                            nextRidges.insert(nextRidges.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
                        }

                        if (nextRidges.empty())
                            break;

                        ridges = std::move(nextRidges);
                        ++idx;

                        int x = int(idx + newColor.x()) % 2 == 0 ? 0 : 1;
                        int y = int(idx) % 2 == 0 ? 0 : 1;
                        int z = int(idx + newColor.z()) % 3 == 0 ? 0 : 1;
                        newColor = QVector4D(x, y, z, 1);
                    }
                }
            });

        toolBar->addAction(toggleRidgeDrawing);
        toolBar->addAction(toggleRidgeEdit);
        toolBar->addAction(toggle3DRidgeEdit);
        toolBar->addAction(ridgeColorToolButton);

        heightEditToolbar->addAction(sculptingToolButton);
        heightEditToolbar->addAction(flattenToolButton);
        heightEditToolbar->addAction(smoothToolButton);

        return mainWidget;
    }
}