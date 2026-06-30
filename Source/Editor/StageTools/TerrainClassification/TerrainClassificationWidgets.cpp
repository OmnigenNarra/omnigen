#include "stdafx.h"
#include "Omnigen.h"
#include "TerrainClassificationWidgets.h"
#include "Editor/StageTools/StageTools.h"
#include "StageToolsTerrainClassification.h"
#include "Utils/PlatformMisc.h"
#include "Utils/CoreUtils.h"
#include "Editor/StageTools/Common/DrawUtils.h"

namespace Design
{
    TerrainClassificationTreeItem::TerrainClassificationTreeItem(ETerrainBlock terrainType, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ toQString(terrainType) }, parent)
        , blockType(terrainType)
    {
    }

    void QTerrainClassificationTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QTerrainClassificationTreeModel::addBlockType(ETerrainBlock terrainType)
    {
        beginResetModel();
        auto itemParent = getRootItem();
        auto newItem = new TerrainClassificationTreeItem(terrainType, itemParent);
        endResetModel();
    }

    void QTerrainClassificationTreeModel::loadBlockTypes()
    {
        beginResetModel();
        for (int i = 0; i <  static_cast<int>(ETerrainBlock::Last); i++)
        {
            auto newItem = new TerrainClassificationTreeItem(ETerrainBlock(i), getRootItem());
            getRootItem()->appendChild(newItem);
        }
        endResetModel();
    }

    void QTerrainClassificationTreeModel::clear()
    {
        beginResetModel();
        getRootItem()->clearChildren();
        endResetModel();
    }

    void QTerrainClassificationTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;
        connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QTerrainClassificationTreeModel::treeSelectionChanged);
    }

    void QTerrainClassificationTreeModel::treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)))
            return;

        treeView->selectionModel()->blockSignals(true);

        for (auto&& index : selected.indexes())
            auto* item = static_cast<TerrainClassificationTreeItem*>(index.internalPointer());

        treeView->selectionModel()->blockSignals(false);
    }

    QWidget* StageTools<EGenerationStage::TerrainClassification>::createOutlineToolbar()
    {
        auto* mainWidget = new QWidget();

        mainWidget->setContentsMargins(0, 0, 0, 0);
        mainWidget->setMaximumWidth(5000);

        auto* mainLayout = new QGridLayout(mainWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        auto* toolBar = new QToolBar();
        mainLayout->addWidget(toolBar, 0, 0, 1, -1);

        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        auto* toolsGroup = new QActionGroup(toolBar);
        toolsGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        auto* blockPaintingButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Block Tool", toolsGroup);
        blockPaintingButton->setCheckable(true);

        toolBar->addAction(blockPaintingButton);

        // Tools Brush Size Slider
        auto* brushSizeSlider = new QSlider(Qt::Orientation::Horizontal);
        brushSizeSlider->setMaximum(10);
        brushSizeSlider->setMinimum(1);
        brushSizeSlider->setValue(brushSize);
        auto* brushSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));
        brushSizeSlider->setVisible(false);
        brushSizeLabel->setVisible(false);

        mainLayout->addWidget(brushSizeLabel, 1, 0);
        mainLayout->addWidget(brushSizeSlider, 1, 1);

        // UI TODO: Same hack as in Stage Tools Ridges for the QSlider to work properly in Outline Section
        mainLayout->setColumnMinimumWidth(2, 4700);

        connect(brushSizeSlider, &QSlider::valueChanged, this, [this, brushSizeSlider, brushSizeLabel]
            {
                brushSize = brushSizeSlider->value();
                brushSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });

        // Button logic
        connect(toolsGroup, &QActionGroup::triggered, this, [this, blockPaintingButton, brushSizeSlider, brushSizeLabel]()
            {
                bBlockPainting = blockPaintingButton->isChecked();

                brushSizeSlider->setVisible(bBlockPainting);
                brushSizeLabel->setVisible(bBlockPainting);

                if (!bBlockPainting)
                    DrawUtils::clearBrush(brushMarker);
            });

        return mainWidget;
    }
}