#include "stdafx.h"
#include "LithomapToolsWidgets.h"
#include "Omnigen.h"
#include "Utils/PlatformMisc.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/StageTools/StageTools.h"
#include "Editor/StageTools/Common/DrawUtils.h"

namespace Design
{

    LithoAssetTreeItem::LithoAssetTreeItem(const QString& nameAsset, const qint64 idAsset, OutlineTreeItem* parent) :
        OutlineTreeItem({ nameAsset }, parent),
        name(nameAsset),
        id(idAsset)
    {
    }

    void QLithoAssignmentTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QLithoAssignmentTreeModel::addBlockType(const QString& nameAsset, const qint64 idAsset)
    {
        beginResetModel();
        auto itemParent = getRootItem();
        auto newItem = new LithoAssetTreeItem(nameAsset, idAsset, itemParent);
        endResetModel();
    }

    void QLithoAssignmentTreeModel::loadLithoAssets()
    {
        beginResetModel();
        auto&& assetMgr = Omnigen::get()->getAssetsSection();
        auto&& lithoAssetsIds = assetMgr->getAssetsIds<EAsset::RockMaterial>();
        assetMgr->forceLoadAssets(EAsset::RockMaterial, lithoAssetsIds);
        auto lithoAssets = assetMgr->getAssets<EAsset::RockMaterial>();

        for (auto&& [id, rockAsset] : lithoAssets)
        {
            auto newItem = new LithoAssetTreeItem(rockAsset->name, id, getRootItem());
            getRootItem()->appendChild(newItem);
        }
        endResetModel();
    }

    void QLithoAssignmentTreeModel::clear()
    {
        beginResetModel();
        getRootItem()->clearChildren();
        endResetModel();
    }

    void QLithoAssignmentTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;
    }


    QWidget* StageTools<EGenerationStage::Lithomap>::createOutlineToolbar()
    {
        auto* omni = Omnigen::get();

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

        auto* lithoPaintingButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Litho Assignment", toolsGroup);
        lithoPaintingButton->setCheckable(true);

        toolBar->addAction(lithoPaintingButton);

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

        mainLayout->setColumnMinimumWidth(2, 4700);

        // Button logic
        connect(toolsGroup, &QActionGroup::triggered, this, [this, lithoPaintingButton, brushSizeSlider, brushSizeLabel]()
            {
                bLithoMapTool = lithoPaintingButton->isChecked();

                brushSizeSlider->setVisible(bLithoMapTool);
                brushSizeLabel->setVisible(bLithoMapTool);

                if (!bLithoMapTool)
                {
                    DrawUtils::clearBrush(brushMarker);
                }
            });

        connect(brushSizeSlider, &QSlider::valueChanged, this, [this, brushSizeSlider, brushSizeLabel]
            {
                brushSize = brushSizeSlider->value();
                brushSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });

        return mainWidget;
    }

}