#include "stdafx.h"
#include <execution>
#include "Omnigen.h"
#include "StageToolsTerrainClassification.h"
#include "../StageTools.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Scene/Generation/Stages/TerrainClassification/StageGeneration_TerrainClassification.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"

#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/StageTools/Common/DrawUtils.h"
#include "BlockTypeMarker.h"

#include "tbb/parallel_for.h"
#include "tbb/parallel_for_each.h"

namespace Design
{
    StageTools<EGenerationStage::TerrainClassification>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::TerrainClassification>::getSelectionMgr() const
    {
        return StageToolsBase::getSelectionMgr();
    }

    void StageTools<EGenerationStage::TerrainClassification>::bind()
    {
        StageToolsBase::bind();

        visualizeBlockTypes();

        auto* omnigen = Omnigen::get();
        auto* outline = omnigen->getOutline();
        auto* toolbar = createOutlineToolbar();

        treeView = new OutlineTreeView;
        treeView->setModel(&treeModel);
        treeModel.setTreeView(treeView);
        treeModel.loadBlockTypes();
        treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        treeView->setUniformRowHeights(true);

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, treeView });

        treeView->show();

        bBlockPainting = false;

        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }

        onGenerationFinished = Generation::getEventMgr().AddEventListener<Generation::EGenerationEvent::Generated>(this, &StageTools<EGenerationStage::TerrainClassification>::updateBlockTypesVisualization);
    }

    void StageTools<EGenerationStage::TerrainClassification>::unbind()
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
        onGenerationFinished.Disconnect();
    }

    void StageTools<EGenerationStage::TerrainClassification>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();

        // Block type map
        writer << genData->getBlockTypeMap();
    }

    void StageTools<EGenerationStage::TerrainClassification>::load(OmniBin<std::ios::in>& reader)
    {
        Generation::StageGen<EGenerationStage::Lithomap>::finalize();

        OmniProfile("Block type");
        auto&& genData = Generation::Data::get();

        // Block type map
        std::vector<Generation::ETerrainBlock> typeMap;
        reader >> typeMap;
        genData->setBlockTypeMap(typeMap);
    }

    void StageTools<EGenerationStage::TerrainClassification>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::TerrainClassification>::modifyNode);
    }

    void StageTools<EGenerationStage::TerrainClassification>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::TerrainClassification>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::TerrainClassification>::clearNodes()
    {
    }

    void StageTools<EGenerationStage::TerrainClassification>::cleanNodesState()
    {
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        auto&& cellNodes = lithomapStageTools->getCellNodes();

        for (auto&& [cellCenter, cellNode] : cellNodes)
        {
            cellNode->setCreatedOnCurrentStage(false);
            cellNode->clearSnapshot();
        }
    }

    void StageTools<EGenerationStage::TerrainClassification>::updateParentNodes()
    {
    }

    void StageTools<EGenerationStage::TerrainClassification>::loadSnapshotData()
    {
        disconnectNodes();

        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        auto&& cellNodes = lithomapStageTools->getCellNodes();
        auto&& diagram = Generation::Data::get()->getTerrainCells();

        for (auto&& [cellCenter, cellNode] : cellNodes)
            if (auto&& cellSnapshot = cellNode->getSnapshot())
            {
                auto cellId = diagram->getCellIndexFromCenter(cellCenter);
                Generation::Data::get()->setBlockTypeForCell(cellId, cellSnapshot->blockType);

                auto blockColor = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(cellSnapshot->blockType);
                gBlockTypeMarker->painter.cellColors[cellId] = blockColor;
                gBlockTypeMarker->painter.bNeedsBufferUpdate = true;
            }

        connectNodes();
    }

    bool StageTools<EGenerationStage::TerrainClassification>::validatePipeline()
    {
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        auto&& cellNodes = lithomapStageTools->getCellNodes();
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
                if (Generation::Data::get()->getBlockTypeMap().at(diagram->getCellIndexFromCenter(cellCenter)) != snapshot->blockType)
                    return false;
        }

        return true;
    }

    void StageTools<EGenerationStage::TerrainClassification>::updatePipeline()
    {
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();

        if (!Generation::Data::get()->getBlockTypeMap().empty())
        {
            connectNodes();
            Generation::StageGen<EGenerationStage::TerrainClassification>::selectBlocks();
            clearAllBatches<BlockTypeBatchParams>();
            visualizeBlockTypes();
            disconnectNodes();
        }

        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::TerrainClassification>::modifyNode(QSharedPointer<Editable> object)
    {
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        auto&& cellNodes = lithomapStageTools->getCellNodes();

        if (auto&& cellUpdate = object.dynamicCast<Voronoi::CellUpdate>())
        {
            // use parallel only here as it does not modify map + is used while editing
            tbb::parallel_for(0, int(cellUpdate->cellCenters.size()), [&](int i)
                {
                    GVector2D cellCenter = cellUpdate->cellCenters[i];
                    if (cellNodes.contains(cellCenter) && !cellNodes.at(cellCenter)->getSnapshot())
                        cellNodes.at(cellCenter)->makeSnapshot();
                });
        }
    }

    bool StageTools<EGenerationStage::TerrainClassification>::eventFilter(QObject* obj, QEvent* event)
    {
        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if(bBlockPainting)
        {
            if (mEvent->type() == QEvent::MouseButtonPress)
            {
                if (mEvent->buttons().testFlag(Qt::LeftButton))
                    paintCells();
            }
            else if (mEvent->type() == QEvent::MouseMove)
            {
                // Draw brush circle
                if (!mEvent->buttons().testFlag(Qt::RightButton))
                    DrawUtils::drawBrushCircle(mEvent, 36, brushSize * 250.0f, brushMarker);
                else if (brushMarker)
                    DrawUtils::clearBrush(brushMarker);

                if (mEvent->buttons().testFlag(Qt::LeftButton))
                    paintCells();
            }
            else if (mEvent->type() == QEvent::MouseButtonRelease)
            {
                if(mEvent->button() == Qt::LeftButton)
                    blockTypeAssign();
            }
        }

        return false;
    }

    void StageTools<EGenerationStage::TerrainClassification>::visualizeBlockTypes()
    {
        // Already present?
        if (std::scoped_lock instanceLock(gBlockTypeMarkerGuard); gBlockTypeMarker)
            if (auto&& [batches, guard] = gBlockTypeMarker->getBatches(); !batches.empty())
                return;

        auto&& data = Generation::Data::get();
        auto&& dem = data->getDEM();
        auto&& blockTypeMap = data->getBlockTypeMap();
        auto&& cells = data->getTerrainCells()->getCells();

        tbb::parallel_for(0, int(cells.size()), [&](int i)
            {
                GeometryData<CellVertex> geom;
                auto& verts = geom.vertices;
                auto& triangles = geom.indices;

                std::vector<GVector2D> pts = cells[i]->getPts();
                pts.emplace_back(cells[i]->getCenter());

                for (auto&& pt : pts)
                    verts.push_back(CellVertex{ QVector3D(pt.x, dem->heightData.sample(pt), pt.z), dem->heightData.sampleNormal(pt), i });

                std::vector<IndexType> trPts = triangulate2D(pts);
                triangles = std::move(trPts);

                auto section = spawnBatched(std::move(geom), BlockTypeBatchParams());
                section->cellIdx = i;
            });

        // Update triangles map
        auto&& painter = gBlockTypeMarker->painter;

        auto&& [batches, batchesGuard] = gBlockTypeMarker->getBatches();
        auto&& batch = batches.begin()->second;

        painter.trianglesToCells.clear();
        painter.trianglesToCells.resize(batch.geometry->indices.size() / 3);

        for (auto&& [guid, section] : batch.sections)
        {
            IndexType trisBegin = section->getIndexBufferOffset() / 3;
            IndexType trisEnd = trisBegin + (section->getIndexBufferSize() / 3);
            for (IndexType ti = trisBegin; ti < trisEnd; ++ti)
                painter.trianglesToCells[ti] = section->cellIdx;
        }

        // Cell states init
        painter.cellColors.resize(cells.size());
        painter.cellStates.resize(cells.size());
        updateBlockTypesVisualization();

        data->initializeQueuedMarkers();
    }

    void StageTools<EGenerationStage::TerrainClassification>::updateBlockTypesVisualization()
    {
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();
        tbb::parallel_for(0, int(blockTypeMap.size()), [&](int i)
            {
                auto blockColor = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(blockTypeMap[i]);
                gBlockTypeMarker->painter.cellColors[i] = blockColor;
            });

        gBlockTypeMarker->painter.bNeedsBufferUpdate = true;
    }

    void StageTools<EGenerationStage::TerrainClassification>::paintCells()
    {
        // Get block type selected in Outline Section
        QModelIndex selectedIndex = treeView->currentIndex();
        if (!selectedIndex.isValid())
            return;

        // Find all cells under brush circle using quad tree and change their type & color
        auto cellSection = SelectionMgrBase::findObjectUnderCursor<DBlockTypeMarker>();
        if (!cellSection)
            return;

        int cell = gBlockTypeMarker->painter.trianglesToCells[cellSection->primitive];
        auto&& cellNodes = DrawUtils::getCellsUnderBrush(cell, brushSize * 250.0f);

        auto&& data = Generation::Data::get();
        auto&& cells = data->getTerrainCells()->getCells();
        auto&& blockTypeMap = data->getBlockTypeMap();

        auto* item = static_cast<TerrainClassificationTreeItem*>(selectedIndex.internalPointer());
        ETerrainBlock terrainBlock = item->blockType;

        auto&& cellUpdate = QSharedPointer<Voronoi::CellUpdate>::create();

        for (auto&& blockNode : cellNodes)
            if (auto cellId = blockNode->data; blockTypeMap[cellId] != terrainBlock)
                cellUpdate->cellCenters << cells[cellId].getVoronoiCenter();

        emit Editable::aboutToBeModified(cellUpdate);

        for (auto&& blockNode : cellNodes)
            if (auto cellId = blockNode->data; blockTypeMap[cellId] != terrainBlock)
            {
                auto blockColor = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(terrainBlock);

                // Undo/Redo data
                blockTypeModifications.emplace_back(cellId, static_cast<int>(blockTypeMap[cellId]), static_cast<int>(terrainBlock));

                data->setBlockTypeForCell(cellId, terrainBlock);

                gBlockTypeMarker->painter.cellColors[cellId] = blockColor;
                gBlockTypeMarker->painter.bNeedsBufferUpdate = true;
            }

        emit Editable::modified(cellUpdate);
    }

    bool StageTools<EGenerationStage::TerrainClassification>::blockTypeAssign()
    {
        HISTORY_PUSH(blockTypeAssign);
        std::vector<std::tuple<int, int, int>> blockTypeMap;

        if (!HISTORY_LOAD(blockTypeMap))
        {
            blockTypeMap = blockTypeModifications;
            HISTORY_SAVE(blockTypeMap);
            blockTypeModifications.clear();
        }
        // Redo
        else
        {
            auto&& data = Generation::Data::get();
            auto&& cells = data->getTerrainCells()->getCells();

            auto&& cellUpdate = QSharedPointer<Voronoi::CellUpdate>::create();

            for (auto&& bt : blockTypeMap)
                cellUpdate->cellCenters << cells[std::get<0>(bt)].getVoronoiCenter();

            emit Editable::aboutToBeModified(cellUpdate);

            for(auto&& bt : blockTypeMap)
            {
                auto cellId = std::get<0>(bt);
                auto newBlockType = static_cast<ETerrainBlock>(std::get<2>(bt));

                auto blockColor = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(newBlockType);
                data->setBlockTypeForCell(cellId, newBlockType);

                gBlockTypeMarker->painter.cellColors[cellId] = blockColor;
                gBlockTypeMarker->painter.bNeedsBufferUpdate = true;
            }

            emit Editable::modified(cellUpdate);
        }

        return true;
    }

    bool StageTools<EGenerationStage::TerrainClassification>::blockTypeAssign_Undo()
    {
        HISTORY_POP();
        std::vector<std::tuple<int, int, int>> blockTypeMap;
        HISTORY_LOAD(blockTypeMap);

        auto&& data = Generation::Data::get();
        auto&& cells = data->getTerrainCells()->getCells();

        auto&& cellUpdate = QSharedPointer<Voronoi::CellUpdate>::create();

        for (auto&& bt : blockTypeMap)
            cellUpdate->cellCenters << cells[std::get<0>(bt)].getVoronoiCenter();

        emit Editable::aboutToBeModified(cellUpdate);

        for (auto&& bt : blockTypeMap)
        {
            auto cellId = std::get<0>(bt);
            auto oldBlockType = static_cast<ETerrainBlock>(std::get<1>(bt));

            auto blockColor = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(oldBlockType);
            data->setBlockTypeForCell(cellId, oldBlockType);

            gBlockTypeMarker->painter.cellColors[cellId] = blockColor;
            gBlockTypeMarker->painter.bNeedsBufferUpdate = true;
        }

        emit Editable::modified(cellUpdate);

        return true;
    }
}