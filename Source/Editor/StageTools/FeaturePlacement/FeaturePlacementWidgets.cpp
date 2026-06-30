#include "stdafx.h"
#include "Omnigen.h"
#include "FeaturePlacementWidgets.h"
#include "Editor/StageTools/StageTools.h"
#include "StageToolsFeaturePlacement.h"
#include "Utils/PlatformMisc.h"
#include "Utils/CoreUtils.h"
#include "Editor/StageTools/Common/DrawUtils.h"

namespace Design
{
    QWidget* StageTools<EGenerationStage::FeaturePlacement>::createOutlineToolbar()
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

        auto* clusterSelectButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Select", toolsGroup);
        clusterSelectButton->setCheckable(true);

        // Tools Select Size Slider
        auto* selectSizeSlider = new QSlider(Qt::Orientation::Horizontal);
        selectSizeSlider->setMaximum(30);
        selectSizeSlider->setMinimum(1);
        selectSize = 5;
        selectSizeSlider->setValue(selectSize);
        selectSizeSlider->show();
        auto* selectSizeLabel = new QLabel("Select Size: " + QString::number(selectSize));
        selectSizeSlider->setVisible(false);
        selectSizeLabel->setVisible(false);

        connect(selectSizeSlider, &QSlider::valueChanged, this, [this, selectSizeSlider, selectSizeLabel]
            {
                selectSize = selectSizeSlider->value();
                selectSizeLabel->setText("Select Size: " + QString::number(selectSize));
            });

        auto* clusterBrushButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Cluster Brush", toolsGroup);
        clusterBrushButton->setCheckable(true);

        // Tools Brush Size Slider
        auto* brushSizeSlider = new QSlider(Qt::Orientation::Horizontal);
        brushSizeSlider->setMaximum(30);
        brushSizeSlider->setMinimum(1);
        brushSize = 5;
        brushSizeSlider->setValue(brushSize);
        brushSizeSlider->show();
        auto* brushSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));
        brushSizeSlider->setVisible(false);
        brushSizeLabel->setVisible(false);

        connect(brushSizeSlider, &QSlider::valueChanged, this, [this, brushSizeSlider, brushSizeLabel]
            {
                brushSize = brushSizeSlider->value();
                brushSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });




        // Brush Clusters Toolbar
        auto* clusterBrushToolbar = new QToolBar();
        clusterBrushToolbar->setIconSize(QSize(40, 20));
        clusterBrushToolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        mainLayout->addWidget(clusterBrushToolbar, 1, 0, 1, -1);
        clusterBrushToolbar->setVisible(false);

        auto* clusterBrushToolsGroup = new QActionGroup(clusterBrushToolbar);
        clusterBrushToolsGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::Exclusive);

        // Cluster Brush Tool
        auto* clusterBrushToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Clusters", clusterBrushToolsGroup);
        clusterBrush = false;
        clusterBrushToolButton->setCheckable(true);
        clusterBrushToolButton->setChecked(clusterBrush);

        // Meta Cluster Brush Tool
        auto* metaClusterBrushToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Meta Clusters", clusterBrushToolsGroup);
        metaClusterBrush = true;
        metaClusterBrushToolButton->setCheckable(true);
        metaClusterBrushToolButton->setChecked(true);

        connect(clusterBrushToolsGroup, &QActionGroup::triggered, this, [=]()
            {
                clusterBrush = clusterBrushToolButton->isChecked();
                metaClusterBrush = metaClusterBrushToolButton->isChecked();
            });

        clusterBrushToolbar->addAction(metaClusterBrushToolButton);
        clusterBrushToolbar->addAction(clusterBrushToolButton);

        mainLayout->addWidget(brushSizeLabel, 2, 0);
        mainLayout->addWidget(brushSizeSlider, 2, 1);
        mainLayout->addWidget(selectSizeLabel, 2, 0);
        mainLayout->addWidget(selectSizeSlider, 2, 1);
        // UI TODO: Same hack as in Stage Tools Ridges for the QSlider to work properly in Outline Section
        mainLayout->setColumnMinimumWidth(2, 4700);

        // Button logic
        connect(toolsGroup, &QActionGroup::triggered, this, [=]()
            {
                if (clusterSelectButton->isChecked())
                    selectedTool = EFeaturePlacementToolType::Select;
                else if (clusterBrushButton->isChecked())
                    selectedTool = EFeaturePlacementToolType::Brush;
                else
                    selectedTool = EFeaturePlacementToolType::None;

                selectSizeSlider->setVisible(selectedTool == EFeaturePlacementToolType::Select);
                selectSizeLabel->setVisible(selectedTool == EFeaturePlacementToolType::Select);

                brushSizeSlider->setVisible(selectedTool == EFeaturePlacementToolType::Brush);
                brushSizeLabel->setVisible(selectedTool == EFeaturePlacementToolType::Brush);
                clusterBrushToolbar->setVisible(selectedTool == EFeaturePlacementToolType::Brush);
            });

        toolBar->addAction(clusterSelectButton);
        toolBar->addAction(clusterBrushButton);

        return mainWidget;
    }
}