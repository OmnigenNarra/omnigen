#include "stdafx.h"
#include "TerrainModelWidgets.h"
#include "StageToolsTerrainModel.h"
#include "Editor/StageTools/StageTools.h"

namespace Design
{
    QWidget* StageTools<EGenerationStage::TerrainModel>::createOutlineToolbar()
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

        // Sculpting Tool
        auto* sculptingToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Sculpting Tool", toolsGroup);
        sculptingToolButton->setCheckable(true);

        // Flatten Tool
        auto* flattenToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Flatten Tool", toolsGroup);
        flattenToolButton->setCheckable(true);

        // Smooth Tool
        auto* smoothToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Smooth Tool", toolsGroup);
        smoothToolButton->setCheckable(true);

        toolBar->addAction(sculptingToolButton);
        toolBar->addAction(flattenToolButton);
        toolBar->addAction(smoothToolButton);

        // Tools Brush Size Slider
        auto* brushSizeSlider = new QSlider(Qt::Orientation::Horizontal);
        brushSizeSlider->setMaximum(10);
        brushSizeSlider->setMinimum(1);
        brushSizeSlider->setValue(brushSize);
        auto* brushSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));
        brushSizeSlider->setVisible(false);
        brushSizeLabel->setVisible(false);

        // Tools Brush Strength Slider
        auto* brushStrengthSlider = new QSlider(Qt::Orientation::Horizontal);
        brushStrengthSlider->setMaximum(10);
        brushStrengthSlider->setMinimum(1);
        brushStrengthSlider->setValue(brushSize);
        auto* brushStrengthLabel = new QLabel("Brush Strength: " + QString::number(brushStrength * 20) + "%");
        brushStrengthSlider->setVisible(false);
        brushStrengthLabel->setVisible(false);

        mainLayout->addWidget(brushSizeLabel, 1, 0);
        mainLayout->addWidget(brushSizeSlider, 1, 1);
        mainLayout->addWidget(brushStrengthLabel, 2, 0);
        mainLayout->addWidget(brushStrengthSlider, 2, 1);

        // UI TODO: Same hack as in Stage Tools Ridges for the QSlider to work properly in Outline Section
        mainLayout->setColumnMinimumWidth(2, 4700);

        connect(brushSizeSlider, &QSlider::valueChanged, this, [this, brushSizeSlider, brushSizeLabel]
            {
                brushSize = brushSizeSlider->value();
                brushSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });

        connect(brushStrengthSlider, &QSlider::valueChanged, this, [this, brushStrengthSlider, brushStrengthLabel]
            {
                brushStrength = brushStrengthSlider->value();
                brushStrengthLabel->setText("Brush Strength: " + QString::number(brushStrength * 20) + "%");
            });

        // Button logic
        connect(toolsGroup, &QActionGroup::triggered, this, [=, this]()
            {
                if (sculptingToolButton->isChecked())
                    toolMode = EToolMode::Sculpt;
                else if (flattenToolButton->isChecked())
                    toolMode = EToolMode::Flatten;
                else if (smoothToolButton->isChecked())
                    toolMode = EToolMode::Smooth;
                else
                    toolMode = EToolMode::None;

                bool anyTool = (toolMode != EToolMode::None);
                if (!anyTool)
                    clearBrush();

                brushSizeSlider->setVisible(anyTool);
                brushSizeLabel->setVisible(anyTool);
                brushStrengthSlider->setVisible(anyTool);
                brushStrengthLabel->setVisible(anyTool);
            });

        return mainWidget;
    }
}