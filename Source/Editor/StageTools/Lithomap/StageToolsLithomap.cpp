#include "stdafx.h"
#include "StageToolsLithomap.h"
#include "Utils/Voronoi/Voronoi.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"

#include "Omnigen.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "Scene/Generation/Stages/StageGeneration.h"
#include "Editor/StageTools/Common/DrawUtils.h"
#include "Scene/Generation/Stages/Lithomap/LithomapMarker.h"

#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

namespace Design
{
    StageTools<EGenerationStage::Lithomap>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::Lithomap>::getSelectionMgr() const
    {
        return StageToolsBase::getSelectionMgr();
    }

    void StageTools<EGenerationStage::Lithomap>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* outline = omnigen->getOutline();
        auto* toolbar = createOutlineToolbar();

        treeView = new OutlineTreeView;
        treeView->setModel(&treeModel);
        treeModel.setTreeView(treeView);
        treeModel.loadLithoAssets();
        treeView->setUniformRowHeights(true);

        outline->applyTreeStyle(treeView);
        treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        outline->fillSection({ toolbar, treeView });

        toolbar->show();

        bLithoMapTool = false;
        
        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void StageTools<EGenerationStage::Lithomap>::unbind()
    {
        StageToolsBase::unbind();

        if (brushMarker)
            DrawUtils::clearBrush(brushMarker);

        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }

        treeModel.clear();
    }

    void StageTools<EGenerationStage::Lithomap>::save(OmniBin<std::ios::out>& writer) const
    {
        // TODO: FIX / CLEAN
        if (Generation::Data::get()->getGenerationStage() == EGenerationStage::Lithomap)
        {
            Generation::Data::get()->setCurrentGeneratedStage(EGenerationStage::Lithomap);
            Generation::StageGen<EGenerationStage::Lithomap>::finalize();
            Generation::Data::get()->setCurrentGeneratedStage({});
        }

        auto&& genData = Generation::Data::get();

        writer << genData->getTerrainCells();

        auto&& [lithomap, lithoClusters] = genData->getLithomap();
        writer << lithomap << lithoClusters;
        writer << genData->getLargestVoronoiCellRadius();

        writer << cellNodes;
    }

    void StageTools<EGenerationStage::Lithomap>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();

        QSharedPointer<Voronoi::BoxDiagram> terrainCells;
        reader >> terrainCells;

        std::vector<int> lithomap; // Cell idx -> Cluster idx
        std::vector<QSharedPointer<Generation::LithoCluster>> lithoClusters;
        reader >> lithomap >> lithoClusters;

        float largestCellRadius;
        reader >> largestCellRadius;

        genData->setTerrainCells(terrainCells);
        genData->setLithomap(lithomap, lithoClusters);
        genData->setLargestVoronoiCellRadius(largestCellRadius);

        Generation::StageGen<EGenerationStage::Lithomap>::updateCellDataFromLithomap();
        Generation::StageGen<EGenerationStage::Lithomap>::visualizeLithomap();
        genData->initializeQueuedMarkers();

        reader >> cellNodes;
    }

    void StageTools<EGenerationStage::Lithomap>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::Lithomap>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::Lithomap>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::Lithomap>::modifyNode);
    }

    void StageTools<EGenerationStage::Lithomap>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::Lithomap>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::Lithomap>::clearNodes()
    {
        cellNodes.clear();
    }

    void StageTools<EGenerationStage::Lithomap>::cleanNodesState()
    {
        std::erase_if(cellNodes, [](auto& kv) { return !kv.second->getCell(); });

        auto&& diagram = Generation::Data::get()->getTerrainCells();
        auto&& [lithomap, lithoClusters] = Generation::Data::get()->getLithomap();

        for (auto&& [cellCenter, cellNode] : cellNodes)
        {
            cellNode->setCreatedOnCurrentStage(false);
            if (cellNode->getSnapshot())
                cellNode->updateLithoSnapshot();
        }
    }

    void StageTools<EGenerationStage::Lithomap>::updateParentNodes()
    {
        auto&& terrainModelStageTools = getStageTools<EGenerationStage::TerrainModel>();
        auto&& demNodes = terrainModelStageTools->getDEMPointNodes();

        if (demNodes.empty())
            return;

        auto&& heightData = Generation::Data::get()->getDEM()->heightData;
        auto&& diagram = Generation::Data::get()->getTerrainCells();

        tbb::parallel_for(tbb::blocked_range2d<int, int>(0, heightData.getSize().x, 0, heightData.getSize().z), [&](tbb::blocked_range2d<int>& r)
            {
                for (int z = r.cols().begin(); z <= r.cols().end(); ++z)
                    for (int x = r.rows().begin(); x <= r.rows().end(); ++x)
                    {
                        auto&& demPoint = heightData.getPoint2D(x, z);
                        auto&& cell = diagram->getCellAt(Generation::Utils::findCell(demPoint));
                        demNodes.at(demPoint)->setCellNode(cell.getVoronoiCenter());
                    }
            });
    }

    void StageTools<EGenerationStage::Lithomap>::loadSnapshotData()
    {
        disconnectNodes();

        auto&& diagram = Generation::Data::get()->getTerrainCells();

        for (auto&& [cellCenter, cellNode] : cellNodes)
            if (auto&& cellSnapshot = cellNode->getSnapshot())
            {
                Generation::StageGen<EGenerationStage::Lithomap>::setCellLithoType(diagram->getCellIndexFromCenter(cellCenter), cellSnapshot->lithoType);
            }

        connectNodes();
    }

    bool StageTools<EGenerationStage::Lithomap>::validatePipeline()
    {
        auto&& diagram = Generation::Data::get()->getTerrainCells();

        for (auto&& [cellCenter, cellNode] : cellNodes)
        {
            // removed cell
            if (!cellNode->getCell())
                return false;
            // added cell
            else if (cellNode->isCreatedOnCurrentStage())
                return false;
            // modified cell
            else if (auto&& snapshot = cellNode->getSnapshot())
                if (Generation::StageGen<EGenerationStage::Lithomap>::getCellLithoType(diagram->getCellIndexFromCenter(cellCenter)) != snapshot->lithoType)
                    return false;
        }

        return true;
    }

    void StageTools<EGenerationStage::Lithomap>::updatePipeline()
    {
        auto&& terrainClassificationStageTools = getStageTools<EGenerationStage::TerrainClassification>();

        if (Generation::Data::get()->getTerrainCells())
        {
            clearAllBatches<LithomapBatchParams>();
            Generation::StageGen<EGenerationStage::Lithomap>::visualizeLithomap();
        }

        terrainClassificationStageTools->updatePipeline();
        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::Lithomap>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        if (auto&& cellUpdate = object.dynamicCast<Voronoi::CellUpdate>())
        {
            for (auto&& center : cellUpdate->cellCenters)
            {
                if (!cellNodes.contains(center))
                    cellNodes[center] = QSharedPointer<CellNode>::create(center);
                else if (!cellNodes[center]->getCell())
                    cellNodes[center]->setCell(center);
            }
        }
    }

    void StageTools<EGenerationStage::Lithomap>::removeNode(QSharedPointer<Editable> object)
    {
        if (auto&& cellUpdate = object.dynamicCast<Voronoi::CellUpdate>())
        {
            for (auto&& center : cellUpdate->cellCenters)
                if (cellNodes.contains(center))
                {
                    if (cellNodes[center]->isCreatedOnCurrentStage())
                        cellNodes.erase(center);
                    else
                    {
                        if (!cellNodes[center]->getSnapshot())
                            cellNodes[center]->makeSnapshot();

                        cellNodes[center]->nullifyCell();
                    }
                }
        }
    }

    void StageTools<EGenerationStage::Lithomap>::modifyNode(QSharedPointer<Editable> object)
    {
        if (auto&& cellUpdate = object.dynamicCast<Voronoi::CellUpdate>())
        {
            // use parallel only here as it does not modify map + is used while editing
            tbb::parallel_for(0, int(cellUpdate->cellCenters.size()), [&](int i)
                {
                    GVector2D cellCenter = cellUpdate->cellCenters[i];
                    if (cellNodes.contains(cellCenter) && !cellNodes[cellCenter]->isCreatedOnCurrentStage() && !cellNodes[cellCenter]->getSnapshot())
                        cellNodes[cellCenter]->makeSnapshot();
                });
        }
    }

    bool StageTools<EGenerationStage::Lithomap>::eventFilter(QObject* obj, QEvent* event)
    {
        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (bLithoMapTool)
        {
            if (mEvent->type() == QEvent::MouseButtonPress)
            {
                if (mEvent->buttons().testFlag(Qt::LeftButton))
                {
                    paintCells();
                }
            }
            else if (mEvent->type() == QEvent::MouseMove)
            {
                DrawUtils::drawBrushCircle(mEvent, 36, brushSize * 250.f, brushMarker);
                if (mEvent->buttons().testFlag(Qt::LeftButton))
                {
                    paintCells();
                }
            }
            else if (mEvent->type() == QEvent::MouseButtonRelease)
            {
                if (mEvent->button() == Qt::LeftButton)
                    applyLithoTypeChanges();
            }
        }

        return false;
    }

    void StageTools<EGenerationStage::Lithomap>::paintCells()
    {
        // Get block type selected in Outline Section
        QModelIndex selectedIndex = treeView->currentIndex();
        if (!selectedIndex.isValid())
            return;

        // Find all cells under brush circle using quad tree and change their type & color
        auto cellSection = SelectionMgrBase::findObjectUnderCursor<DLithomapMarker>();
        if (!cellSection)
            return;

        int cell = gLithomapMarker->painter.trianglesToCells[cellSection->primitive];
        auto&& cellNodes = DrawUtils::getCellsUnderBrush(cell, brushSize * 250.0f);

        auto&& data = Generation::Data::get();
        auto&& cells = data->getTerrainCells()->getCells();

        auto* item = static_cast<LithoAssetTreeItem*>(selectedIndex.internalPointer());
        qint64 idAsset = item->id;

        for (auto&& blockNode : cellNodes)
        {
            auto cellId = blockNode->data;

            qint64 oldType = Generation::StageGen<EGenerationStage::Lithomap>::getCellLithoType(cellId);
            if (oldType != idAsset)
            {
                changesByCellId[cellId] = { oldType, idAsset };
                Generation::StageGen<EGenerationStage::Lithomap>::setCellLithoType(cellId, idAsset);
            }
        }
    }

    bool StageTools<EGenerationStage::Lithomap>::applyLithoTypeChanges()
    {
        HISTORY_PUSH(applyLithoTypeChanges);
        std::unordered_map<int, std::array<qint64, 2>> innerChangesByCellId;
        if (!HISTORY_LOAD(innerChangesByCellId))
        {
            innerChangesByCellId = changesByCellId;
            HISTORY_SAVE(innerChangesByCellId);
            changesByCellId.clear();
        }
        else
        {
            for (auto [cellId, change] : innerChangesByCellId)
            {
                Generation::StageGen<EGenerationStage::Lithomap>::setCellLithoType(cellId, change[1]);
            }
        }
        
        return true;
    }

    bool StageTools<EGenerationStage::Lithomap>::applyLithoTypeChanges_Undo()
    {
        HISTORY_POP();
        std::unordered_map<int, std::array<qint64, 2>> innerChangesByCellId;
        HISTORY_LOAD(innerChangesByCellId);
        for (auto [cellId, change] : innerChangesByCellId)
        {
            Generation::StageGen<EGenerationStage::Lithomap>::setCellLithoType(cellId, change[0]);
        }
        return true;
    }
}