#include "stdafx.h"
#include "ModToolsRiver.h"
#include "Utils/PlatformMisc.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/TerrainMods/River/TerrainModRiver.h"
#include "Utils/MarkerRecorder.h"
#include "../../StageTools.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverMarker.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

namespace Design
{
    void ModTools<Generation::ETerrainMod::River>::bind()
    {
        treeModel.loadRiverMods();

        // Viewport events
        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void ModTools<Generation::ETerrainMod::River>::unbind()
    {
        treeModel.clear();

        // Viewport events
        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }

        if (brushMarker)
        {
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
            brushMarker = nullptr;
        }

        isSpawningRiver = false;
    }

    QWidget* ModTools<Generation::ETerrainMod::River>::create()
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

        auto* riverGroup = new QActionGroup(toolBar);
        riverGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        auto* toggleRiverSpawning = new QAction(QIcon("Resources/Icons/icon_branch_open.png"), "Spawn River", riverGroup);
        toggleRiverSpawning->setCheckable(true);

        auto* toggleRiverDrawing = new QAction(QIcon("Resources/Icons/icon_branch_open.png"), "Draw River", riverGroup);
        toggleRiverDrawing->setCheckable(true);

        auto* editDrawnRiver = new QAction(QIcon("Resources/Icons/icon_branch_open.png"), "Edit");
        editDrawnRiver->setVisible(false);
        editDrawnRiver->setCheckable(true);

        auto* finishRiverDrawing = new QAction(QIcon("Resources/Icons/icon_branch_open.png"), "Finalize River");
        finishRiverDrawing->setVisible(false);

        // Editing Brush Size Slider
        auto* riverEditBSize = new QSlider(Qt::Orientation::Horizontal);
        riverEditBSize->setMaximum(10);
        riverEditBSize->setMinimum(1);
        riverEditBSize->setValue(3);
        brushSize = 3;
        riverEditBSize->show();
        auto* riverEditSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));

        riverEditBSize->setVisible(false);
        riverEditSizeLabel->setVisible(false);

        // Editing Brush Strength Slider
        auto* riverEditBStr = new QSlider(Qt::Orientation::Horizontal);
        riverEditBStr->setMaximum(10);
        riverEditBStr->setMinimum(1);
        riverEditBStr->setValue(5);
        brushStrength = 5;
        riverEditBStr->show();
        auto* riverEditStrLabel = new QLabel("Brush Strength: " + QString::number(brushStrength * 20) + "%");

        riverEditBStr->setVisible(false);
        riverEditStrLabel->setVisible(false);

        connect(toggleRiverSpawning, &QAction::triggered, this, [this, toggleRiverSpawning]()
            {
                isSpawningRiver = toggleRiverSpawning->isChecked();
            });

        connect(finishRiverDrawing, &QAction::triggered, this, [this, finishRiverDrawing]()
            {
                if (riverPreview)
                {
                    QMouseEvent dummyEvent((QEvent::MouseMove), QPoint(0,0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                    spawnRiver(dummyEvent, riverPreview->getControlPoints());
                    endRiverDrawing();
                }
            });

        connect(toggleRiverDrawing, &QAction::triggered, this, [this, toggleRiverDrawing, finishRiverDrawing, editDrawnRiver]()
            {
                isDrawingRiver = toggleRiverDrawing->isChecked();
                finishRiverDrawing->setVisible(toggleRiverDrawing->isChecked());
                editDrawnRiver->setVisible(toggleRiverDrawing->isChecked());
            });

        connect(editDrawnRiver, &QAction::triggered, this, [this, editDrawnRiver, riverEditBSize, riverEditSizeLabel, riverEditBStr, riverEditStrLabel]()
            {
                isEditingDrawn = editDrawnRiver->isChecked();
                riverEditBSize->setVisible(editDrawnRiver->isChecked());
                riverEditSizeLabel->setVisible(editDrawnRiver->isChecked());
                riverEditBStr->setVisible(editDrawnRiver->isChecked());
                riverEditStrLabel->setVisible(editDrawnRiver->isChecked());

                if (brushMarker)
                {
                    Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
                    brushMarker = nullptr;
                }
            });

        connect(riverEditBSize, &QSlider::valueChanged, this, [this, riverEditBSize, riverEditSizeLabel]
            {
                brushSize = riverEditBSize->value();
                riverEditSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });

        connect(riverEditBStr, &QSlider::valueChanged, this, [this, riverEditBStr, riverEditStrLabel]
            {
                brushStrength = riverEditBStr->value();
                riverEditStrLabel->setText("Brush Strength: " + QString::number(brushStrength * 20) + "%");
            });

        mainLayout->addWidget(riverEditSizeLabel, 1, 0);
        mainLayout->addWidget(riverEditBSize, 1, 1);
        mainLayout->addWidget(riverEditStrLabel, 2, 0);
        mainLayout->addWidget(riverEditBStr, 2, 1);

        // Ugly hack for the slider, as without setting a max width to main widget it expands indefinitely,
        // setting a fixed slider size does not work and there is no option to set max column width
        // Any better solutions are welcome; Will return to this during the Great UI Rework
        mainLayout->setColumnMinimumWidth(2, 4700);

        toolBar->addAction(toggleRiverSpawning);
        toolBar->addAction(toggleRiverDrawing);
        toolBar->addAction(finishRiverDrawing);
        toolBar->addAction(editDrawnRiver);
        return mainWidget;
    }

    OutlineTreeModel* ModTools<Generation::ETerrainMod::River>::getTreeModel()
    {
        return &treeModel;
    }

    void ModTools<Generation::ETerrainMod::River>::loadTreeViewModEntries()
    {
        treeModel.loadRiverMods();
    }

    void ModTools<Generation::ETerrainMod::River>::onModSelectionChanged()
    {
        blockSignals(true);

        treeModel.clearSelection();
        auto* selMgr = Design::ModSelectionMgr::get();
        auto&& allSelectedRivers = selMgr->getSelection<Generation::ETerrainMod::River>();

        for (auto&& riverMod : allSelectedRivers)
        {
            auto&& guid = riverMod->getGuid();
            treeModel.selectRiverMod(guid);
        }

        blockSignals(false);
        QOmnigenViewportSection::repaintAll();
    }

    void ModTools<Generation::ETerrainMod::River>::addModEntry(size_t typeHash, QSharedPointer<Editable> object)
    {
        treeModel.addRiverMod(typeHash, object);
    }

    void ModTools<Generation::ETerrainMod::River>::removeModEntry(QSharedPointer<Editable> object)
    {
        treeModel.removeRiverMod(object);
    }

    void ModTools<Generation::ETerrainMod::River>::updateModEntry(QSharedPointer<Editable> object, bool reset)
    {
        treeModel.updateRiverMod(object, reset);
    }

    void ModTools<Generation::ETerrainMod::River>::setModToolTreeView(QTreeView* treeView)
    {
        treeModel.setTreeView(treeView);
    }

    bool ModTools<Generation::ETerrainMod::River>::spawnRiver(const QMouseEvent& me, const std::vector<QVector3D>& riverPoints)
    {
        HISTORY_PUSH(spawnRiver, me, {});
        IHSrcInfo source;
        std::vector<QVector3D> riverPts = riverPoints;

        if (!HISTORY_LOAD(riverPts) && !riverPts.empty())
            HISTORY_SAVE(riverPts);

        if (riverPts.empty())
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

            if (!HISTORY_LOAD(source))
            {
                // Temp until we can spawn rivers from anywhere
                auto&& ihQTree = gBatchingMarkerInstance<IsohypseBatchParams>->getQuadTree();
                auto nearestIHPoint = ihQTree.find_nearest(triangleCenter.x(), triangleCenter.z(), 10000);
                if (!nearestIHPoint)
                {
                    HISTORY_ABORT_PUSH();
                    OmniLog(ELoggingLevel::Error) <<= "[Spawn River] Failed to find IH point.";
                    spawn<DLineMarker>(triangleCenter);
                    return false;
                }

                source.ih = nearestIHPoint->data.section;
                source.idx = nearestIHPoint->data.idx;
                HISTORY_SAVE(source);
            }
        }

        MarkerRecorder rec;
        auto newRiver = DRiverMarker::generateOne(source, riverPts);
        auto newMod = Generation::TerrainMod<Generation::ETerrainMod::River>::processSingleRiver(newRiver);

        // Show markers
        Generation::Data::get()->initializeQueuedMarkers();

        qint64 riverGuid = newRiver->getGuid();
        qint64 modGuid = newMod->getGuid();
        HISTORY_SAVE2(riverGuid, modGuid);

        if (auto parent = newRiver->getParent(); parent)
        {
            qint64 parentRiverGuid = parent.lock()->getGuid();
            HISTORY_SAVE(parentRiverGuid);
        }

        std::map<size_t, std::vector<qint64>>& hMarkersSpawned = rec.guidMap;
        HISTORY_SAVE(hMarkersSpawned);

        return true;
    }

    bool ModTools<Generation::ETerrainMod::River>::spawnRiver_Undo(const QMouseEvent&, const std::vector<QVector3D>&)
    {
        HISTORY_POP();

        qint64 riverGuid, modGuid;
        HISTORY_LOAD2(riverGuid, modGuid);
        auto river = Generation::Data::get()->findMarkerByGuid<DRiverMarker>(riverGuid);
        Q_ASSERT(river);

        qint64 parentRiverGuid;
        if (HISTORY_LOAD(parentRiverGuid))
        {
            auto parent = Generation::Data::get()->findMarkerByGuid<DRiverMarker>(parentRiverGuid);
            Q_ASSERT(parent);
            parent->removeChild(river);
        }

        Generation::Data::get()->removeTerrainMod(Generation::ETerrainMod::River, modGuid);

        std::map<size_t, std::vector<qint64>> hMarkersSpawned;
        HISTORY_LOAD(hMarkersSpawned);

        for (auto&& [type, guids] : hMarkersSpawned)
            for (auto guid : guids)
                Generation::Data::get()->clearSingleExactMarker(type, guid);

        return true;
    }

    void ModTools<Generation::ETerrainMod::River>::startRiverDrawing(const QMouseEvent& me)
    {
        startingPoint = findPointOnMap(me);
        if (!startingPoint)
            return;

        drawingOperation = true;
    }

    bool ModTools<Generation::ETerrainMod::River>::riverDrawing(const QMouseEvent& me)
    {
        auto point = findPointOnMap(me);
        QVector3D lastPoint = riverPreview ? riverPreview->getControlPoints().back() : *startingPoint;

        if (!point || lastPoint.distanceToPoint(*point) <= 500.0f)
            return false;

        if (point->y() > lastPoint.y())
            return true;

        if (!riverPreview)
            riverPreview = Generation::Data::get()->createMarker<DLineMarker, true>(std::vector<QVector3D>({*startingPoint, *point}), QVector4D(1, 0, 1, 1), false, 60);
        else
            riverPreview->extendMarker(*point);

        return true;
    }

    bool ModTools<Generation::ETerrainMod::River>::endRiverDrawing()
    {
        drawingOperation = false;
        startingPoint = {};
        Generation::Data::get()->clearSingleExactMarker<DLineMarker>(riverPreview->getGuid());
        riverPreview.clear();
        return true;
    }

    void ModTools<Generation::ETerrainMod::River>::editDrawnRiver(const QMouseEvent& me)
    {
        if (auto&& point = findPointOnMap(me); point)
        {
            if (!oldBrushPos)
            {
                oldBrushPos = point;
                return;
            }

            float brushS = brushSize * 500.0f;
            auto moveDelta = (*point - *oldBrushPos).normalized();
            oldBrushPos = point;

            auto pointToCheck = *point + QVector3D(brushS * moveDelta);
            Segment2D brushSeg(*point, pointToCheck);
            auto&& oldPts = riverPreview->getControlPoints();
            std::vector<QVector3D> newPts(oldPts.size());
            int clostestIdx = -1;

            for (int i = 0; i < oldPts.size() - 1; ++i)
                if (Segment2D(oldPts[i], oldPts[i + 1]).intersects(brushSeg, true))
                    clostestIdx = oldPts[i].distanceToLine(*point, moveDelta) < oldPts[i + 1].distanceToLine(*point, moveDelta) ? i : i + 1;

            auto&& dem = Generation::Data::get()->getDEM();
            if (clostestIdx > -1)
            {
                for (int i = 0; i < oldPts.size(); ++i)
                {
                    float distanceToBrush = point->distanceToPoint(oldPts[i]);
                    float factor = 1.0f - (distanceToBrush / brushS);

                    QVector3D newPoint = oldPts[i] + QVector3D((brushS * moveDelta.x() * factor * (brushStrength / 5.0f)), 0.0f, (brushS * moveDelta.z() * factor * (brushStrength / 5.0f)));
                    float newHeight = dem->heightData.sample(GVector2D(newPoint));
                    if (newHeight <= point->y())
                        newPoint.setY(dem->heightData.sample(GVector2D(newPoint)));
                    else
                        newPoint = oldPts[i];

                    // THIS IS WRONG
                    if (distanceToBrush > brushS)
                        newPoint = oldPts[i];

                    newPts[i] = newPoint;
                }

                riverPreview->movePoints(newPts);
            }
        }
    }

    void ModTools<Generation::ETerrainMod::River>::drawBrushCircle(QMouseEvent* mEvent, int circleDetail)
    {
        if (auto mPoint = findPointOnMap(*mEvent); mPoint)
        {
            int brushS = brushSize * 500;
            auto&& dem = Generation::Data::get()->getDEM();

            std::vector<QVector3D> circlePoints;

            for (int i = 0; i <= circleDetail; i++)
            {
                auto angle = ((std::numbers::pi * 2) / circleDetail) * i;

                QVector3D newPoint = { (*mPoint).x() + (brushS * std::cosf(angle)), 0.0f, (*mPoint).z() + (brushS * std::sinf(angle)) };
                newPoint.setY(dem->heightData.sample(GVector2D(newPoint)) + 60.0f);
                circlePoints.emplace_back(newPoint);
            }

            if (!brushMarker)
                brushMarker = Generation::Data::get()->createMarker<DLineMarker, true>(circlePoints, QVector4D(0.2, 1, 0.2, 0.6), true);
            else
                brushMarker->movePoints(circlePoints);
        }
    }

    std::optional<QVector3D> ModTools<Generation::ETerrainMod::River>::findPointOnMap(const QMouseEvent& me)
    {
        auto&& clusterTools = getStageTools<EGenerationStage::FeatureGeneration>();
        auto clusterData = clusterTools->findClusterTriangleUnderCursor();
        if (!clusterData)
            return {};

        auto&& vertices = clusterData->cluster->section->mainBuffer->vertices;
        auto triangles = clusterData->cluster->section->mainBuffer->indices;
        auto&& p0 = vertices[triangles[clusterData->triangleIdx * 3 + 0]];
        auto&& p1 = vertices[triangles[clusterData->triangleIdx * 3 + 1]];
        auto&& p2 = vertices[triangles[clusterData->triangleIdx * 3 + 2]];
        auto triangleCenter = (p0.position + p1.position + p2.position) / 3.0;

        return triangleCenter;
    }

    bool ModTools<Generation::ETerrainMod::River>::eventFilter(QObject* obj, QEvent* event)
    {
        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
                if (isDrawingRiver)
                {
                    if (isEditingDrawn)
                        isEditing = true;
                    else if (!drawingOperation)
                        startRiverDrawing(*mEvent);
                }

            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            if (isEditingDrawn && !mEvent->buttons().testFlag(Qt::RightButton))
            {
                drawBrushCircle(mEvent, 24);
            }

            if (isEditing)
                editDrawnRiver(*mEvent);
            else if (drawingOperation)
                riverDrawing(*mEvent);
        }
        else if (mEvent->type() == QEvent::MouseButtonRelease)
        {
            if (mEvent->button() == Qt::LeftButton)
            {
                if (isSpawningRiver)
                    spawnRiver(*mEvent);
                else if (isEditingDrawn)
                    isEditing = false;
                else if (isDrawingRiver)
                    drawingOperation = false;

                oldBrushPos = {};
            }
        }

        return false;
    }

    RiverSelection::Selection(const std::any& inRMod)
        : riverModPtr(std::any_cast<QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>>>(inRMod))
    {
        select();
    }

    bool RiverSelection::findOnScene(QMap<Generation::ETerrainMod, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        if (!Generation::Data::get())
            return false;

        auto&& diagram = Generation::Data::get()->getTerrainCells();

        for (auto&& mod : Generation::Data::get()->getTerrainMods()[Generation::ETerrainMod::River])
        {
            for (int i : mod->getArea())
            {
                auto& cell = diagram->getCellAt(i);
                const float height = Generation::Data::get()->getDEM()->heightData.sample(cell->getCenter());

                if (auto point = OmnigenCameraMgr::get()->findPointInWorld(height, x, y); point)
                {
                    if (cell->contains(*point))
                    {
                        (*output)[Generation::ETerrainMod::River] = mod.staticCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
                        return true;
                    }
                }
                else
                    return false;
            }
        }

        return false;
    }


    QSharedPointer<OmnigenPropertyListBase> RiverSelection::makePropertyList()
    {
        QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>> ptr = riverModPtr;
        qint64 guid = ptr->getGuid();
        auto props = QSharedPointer<OmnigenPropertyListBase>::create(guid, sharedFromThis());
        auto getOwner = [guid]() 
        { 
            auto&& riverMods = Generation::Data::get()->getTerrainMods()[Generation::ETerrainMod::River];
            auto&& mod = std::find_if(riverMods.begin(), riverMods.end(), [guid](const auto& ele) { return ele->getGuid() == guid; });
            Q_ASSERT(mod != riverMods.end());
            auto&& riverMod = mod->staticCast<Generation::TerrainMod<Generation::ETerrainMod::River>>();
            return riverMod;
        };

        props->addField(QSharedPointer<TOmnigenField<QString>>::create(
            "Name",
            [getOwner]()
            {
                return getOwner()->getName();
            },
            [getOwner](auto&& newName)
            {
                auto river = getOwner();

                if (History::GetContext()->IsUndoingOrRedoing())
                    Design::ModSelectionMgr::get()->setSelection<Generation::ETerrainMod::River>({ river });

                river->setName(newName);
                return true;
            }));

        return props;
    }

    QMenu* RiverSelection::requestContextMenu(const std::any& data)
    {
        QMenu* menu = new QMenu(Omnigen::get());
        return menu;
    }

    void RiverSelection::getData(const SelectionBase* obj, QSet<DataType>* data)
    {
        (*data) += static_cast<const RiverSelection*>(obj)->riverModPtr;
    }

    std::vector<QSharedPointer<SelectionBase>> RiverSelection::createFromData(
        const QSet<QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>>>& inRMods)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& rmod : inRMods)
        {
            auto sel = QSharedPointer<RiverSelection>::create();
            sel->riverModPtr = rmod;
            sel->select();
            results << sel;
        }

        return results;
    }

    void RiverSelection::update(const std::any& newRidge, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        //if (!bSubtract)
        //    return;

        //// Deselect all matches
        //for (int i = 0; i < currentSelections->size(); ++i)
        //{
        //    auto* ridgeSel = static_cast<RidgeSelection*>(currentSelections->at(i).get());
        //    if (ridgeSel->ridgePtr->getGuid() == riverPtr->getGuid())
        //    {
        //        ridgeSel->deselect();
        //        currentSelections->erase(currentSelections->begin() + i--);
        //    }
        //}
    }

    void RiverSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        //if (bSubtract)
        //    return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D RiverSelection::getPosition() const
    {
        qint64 markerGuid = riverModPtr->getMarkerGuid();
        auto&& riverMarker = Generation::Data::get()->findMarkerByGuid<DRiverMarker>(markerGuid);
        auto&& cpts = riverMarker->getControlPoints();
        return cpts[cpts.size() / 2];
    }

    void RiverSelection::select() const
    {
        qint64 markerGuid = riverModPtr->getMarkerGuid();
        auto&& riverMarker = Generation::Data::get()->findMarkerByGuid<DRiverMarker>(markerGuid);
        riverMarker->setSelected(true);
    }

    void RiverSelection::deselect() const
    {
        qint64 markerGuid = riverModPtr->getMarkerGuid();
        auto&& riverMarker = Generation::Data::get()->findMarkerByGuid<DRiverMarker>(markerGuid);
        riverMarker->setSelected(false);
    }
}
