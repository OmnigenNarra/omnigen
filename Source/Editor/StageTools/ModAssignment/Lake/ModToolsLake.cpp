#include "stdafx.h"
#include "ModToolsLake.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "../../StageTools.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Utils/MarkerRecorder.h"
#include "Scene/Generation/Stages/TerrainMods/Lake/TerrainModLake.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

namespace Design
{
    void ModTools<Generation::ETerrainMod::Lake>::bind()
    {
        // Viewport events
        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void ModTools<Generation::ETerrainMod::Lake>::unbind()
    {
        // Viewport events
        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }

        isSpawningLake = false;
    }

    QWidget* ModTools<Generation::ETerrainMod::Lake>::create()
    {
        auto* toolBar = new QToolBar();
        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        auto* toggleLakeSpawning = new QAction(QIcon("Resources/Icons/icon_branch_open.png"), "Spawn Lake");
        toggleLakeSpawning->setCheckable(true);

        connect(toggleLakeSpawning, &QAction::triggered, this, [this, toggleLakeSpawning]()
            {
                isSpawningLake = toggleLakeSpawning->isChecked();
            });

        toolBar->addAction(toggleLakeSpawning);
        return toolBar;
    }

    bool ModTools<Generation::ETerrainMod::Lake>::spawnLake(const QMouseEvent& me)
    {
        using namespace Generation;

        HISTORY_PUSH(spawnLake, me);

        QSet<int> area;
        if (!HISTORY_LOAD(area))
        {
            auto&& dem = Generation::Data::get()->getDEM();
            auto&& clusterTools = getStageTools<EGenerationStage::FeatureGeneration>();

            auto clusterData = clusterTools->findClusterTriangleUnderCursor();
            if (!clusterData)
                return false;

            auto&& vertices = clusterData->cluster->section->mainBuffer->vertices;
            auto triangles = clusterData->cluster->section->mainBuffer->indices;
            auto&& p0 = vertices[triangles[clusterData->triangleIdx * 3 + 0]];
            auto&& p1 = vertices[triangles[clusterData->triangleIdx * 3 + 1]];
            auto&& p2 = vertices[triangles[clusterData->triangleIdx * 3 + 2]];
            auto triangleCenter = (p0.position + p1.position + p2.position) / 3.0;

            int targetCell = Utils::findCell(triangleCenter);
            if (targetCell == -1)
            {
                OmniLog(ELoggingLevel::Error) <<= "[Spawn Lake] Failed to find surface point.";
                spawn<DLineMarker>(triangleCenter);
                return false;
            }

            area = TerrainMod<ETerrainMod::Lake>::computeLakeArea(targetCell);
            HISTORY_SAVE(area);
        }

        MarkerRecorder rec;
        auto newLakeMod = TerrainMod<ETerrainMod::Lake>::createLake(area);
        Data::get()->addTerrainMod(ETerrainMod::Lake, newLakeMod);

        // Show markers
        Data::get()->initializeQueuedMarkers();

        qint64 modGuid = newLakeMod->getGuid();
        HISTORY_SAVE(modGuid);

        std::map<size_t, std::vector<qint64>>& hMarkersSpawned = rec.guidMap;
        HISTORY_SAVE(hMarkersSpawned);

        return true;
    }

    bool ModTools<Generation::ETerrainMod::Lake>::spawnLake_Undo(const QMouseEvent&)
    {
        HISTORY_POP();

        qint64 modGuid;
        HISTORY_LOAD(modGuid);

        Generation::Data::get()->removeTerrainMod(Generation::ETerrainMod::Lake, modGuid);

        std::map<size_t, std::vector<qint64>> hMarkersSpawned;
        HISTORY_LOAD(hMarkersSpawned);

        for (auto&& [type, guids] : hMarkersSpawned)
            for (auto guid : guids)
                Generation::Data::get()->clearSingleExactMarker(type, guid);

        return true;
    }

    bool ModTools<Generation::ETerrainMod::Lake>::eventFilter(QObject* obj, QEvent* event)
    {
        if (!isSpawningLake)
            return false;

        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonRelease)
        {
            if (mEvent->button() == Qt::LeftButton)
            {
                spawnLake(*mEvent);
            }
        }

        return false;
    }
}