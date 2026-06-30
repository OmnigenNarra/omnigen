#include "stdafx.h"
#include "ContourLinesWidgets.h"
#include "StageToolsContourLines.h"
#include "Scene/Generation/OmnigenGenerationData.h"

#include <QToolBar>

namespace Design
{
    QWidget* StageTools<EGenerationStage::ContourLines>::createOutlineToolbar()
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

        auto* togglePeakMarker = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Draw Peak Markers");
        togglePeakMarker->setCheckable(true);

        connect(togglePeakMarker, &QAction::triggered, this, [this, togglePeakMarker]
            {
                if(togglePeakMarker->isChecked())
                {
                    for (auto&& peak : peakPoints)
                        peakMarkers.push_back(spawn<DLineMarker, true>(peak, 5000, QVector4D(1, 0, 1, 1))->getGuid());
                }
                else
                {
                    for (auto&& markerGuid : peakMarkers)
                        Generation::Data::get()->clearSingleExactMarker<DLineMarker>(markerGuid);

                    peakMarkers.clear();
                }
            });

        toolBar->addAction(togglePeakMarker);

        return mainWidget;
    }
}