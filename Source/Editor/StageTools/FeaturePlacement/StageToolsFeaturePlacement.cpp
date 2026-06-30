#include "stdafx.h"
#include "Omnigen.h"
#include "StageToolsFeaturePlacement.h"
#include "FeaturePlacementSelection.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/StageTools/Common/DrawUtils.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Utils/PlatformMisc.h"
#include "Editor/StageTools/StageTools.h"
#include "../TerrainClassification/BlockTypeMarker.h"

#include "tbb/parallel_for.h"
#include "tbb/spin_mutex.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

#define DEBUG_CLUSTER_VALIDATION 0

namespace Design
{
    StageTools<EGenerationStage::FeaturePlacement>::StageTools()
        : StageToolsBase()
    {
        setupActions();
    }

    SelectionMgrBase* StageTools<EGenerationStage::FeaturePlacement>::getSelectionMgr() const
    {
        return FeaturePlacementSelectionMgr::get();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* outline = omnigen->getOutline();
        auto* toolbar = createOutlineToolbar();

        treeView = new OutlineTreeView;
        //treeView->setModel(&treeModel);
        //treeModel.setTreeView(treeView);
        treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        treeView->setUniformRowHeights(true);

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, treeView });

        treeView->show();

        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }

        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();
        auto&& metaClusters = Generation::Data::get()->getTerrainMetaClusters();
        terrainMetaClustersMap.clear();
        terrainMetaClustersMap.resize(cells.size());

        for (auto&& metaClusterVec : metaClusters)
            tbb::parallel_for(0, int(metaClusterVec.size()), [&](int idx)
                {
                    for (auto&& cell : metaClusterVec[idx]->getCells())
                        terrainMetaClustersMap[cell] = metaClusterVec[idx];
                });

        for (auto&& metaClusterVec : metaClusters)
            for (auto&& metaCluster : metaClusterVec)
            {
                currentMetaClusters[metaCluster->getGuid()] = metaCluster;
                for (auto&& cluster : metaCluster->getClusters())
                    currentClusters[cluster->getGuid()] = cluster;
            }

        auto&& terrainClassificationTools = getStageTools<EGenerationStage::TerrainClassification>();
        terrainClassificationTools->visualizeBlockTypes();

        showClusterBorders();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::unbind()
    {
        StageToolsBase::unbind();

        // Clear markers created during this stage
        clearAllBatches<ClusterBorderBatchParams>();

        metaClusterMarkers.clear();
        clusterMarkers.clear();
        terrainMetaClustersMap.clear();

        DrawUtils::clearBrush(brushMarker);

        selectedTool = EFeaturePlacementToolType::None;
        clusterBrush = false;
        metaClusterBrush = false;

        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }
    }

    void StageTools<EGenerationStage::FeaturePlacement>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();

        // Clusters
        auto&& metaClusters = genData->getTerrainMetaClusters();
        writer << metaClusters.size();

        for (auto cit = metaClusters.keyValueBegin(); cit != metaClusters.keyValueEnd(); ++cit)
        {
            auto&& [type, metaClusterVec] = *cit;

            writer << type;
            writer << metaClusterVec.size();
            for (auto&& metaCluster : metaClusterVec)
            {
                Generation::ETerrainBlockConstexpr::UseIn<EAC::SaveTerrainMetaCluster>(type, metaCluster, writer);
            }
        }

        writer << clusterNodes;
        writer << metaClusterNodes;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::load(OmniBin<std::ios::in>& reader)
    {
        OmniProfile("Clusters");
        auto&& genData = Generation::Data::get();

        // Clusters
        size_t blockCount = Generation::Data::get()->getTerrainCells()->getCells().size();
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> clustersMap(blockCount);

        QMap<Generation::ETerrainBlock, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>> metaClusters;
        int usedTypesCount;
        reader >> usedTypesCount;

        for (int i = 0; i < usedTypesCount; ++i)
        {
            Generation::ETerrainBlock type;
            reader >> type;

            size_t metaClustersCount;
            reader >> metaClustersCount;

            metaClusters[type].resize(metaClustersCount);
            for (auto&& metaCluster : metaClusters[type])
            {
                metaCluster = Generation::ETerrainBlockConstexpr::UseIn<EAC::LoadTerrainMetaCluster>(type, reader);
                metaCluster->setType(type);
            }

            // Recompute clusters map for this type
            tbb::parallel_for(0, int(metaClusters[type].size()), [&](int i)
                {
                    std::unordered_set<int> metaClusterCells;
                    for (auto&& cluster : metaClusters[type][i]->getClusters())
                        for (int cellId : cluster->cells)
                        {
                            clustersMap[cellId] = cluster;
                            metaClusterCells.insert(cellId);
                        }
                    metaClusters[type][i]->setCells(metaClusterCells);
                });
        }

        genData->setTerrainClusters(metaClusters, clustersMap);

        genData->initializeQueuedMarkers();

        reader >> clusterNodes;
        reader >> metaClusterNodes;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::FeaturePlacement>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::FeaturePlacement>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::FeaturePlacement>::modifyNode);
    }

    void StageTools<EGenerationStage::FeaturePlacement>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::clearNodes()
    {
        clusterNodes.clear();
        metaClusterNodes.clear();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::cleanNodesState()
    {
        std::erase_if(clusterNodes, [](auto& kv) { return !kv.second->getCluster(); });
        std::erase_if(metaClusterNodes, [](auto& kv) { return !kv.second->getMetaCluster(); });

        for (auto&& [clusterGuid, clusterNode] : clusterNodes)
        {
            clusterNode->setCreatedOnCurrentStage(false);
            clusterNode->clearSnapshot();
        }
        for (auto&& [metaClusterGuid, metaClusterNode] : metaClusterNodes)
        {
            metaClusterNode->setCreatedOnCurrentStage(false);
            metaClusterNode->clearSnapshot();
        }
    }

    void StageTools<EGenerationStage::FeaturePlacement>::updateParentNodes()
    {
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        auto&& cellNodes = lithomapStageTools->getCellNodes();

        auto&& diagram = Generation::Data::get()->getTerrainCells();

        for (auto&& [cellCenter, cellNode] : cellNodes)
        {
            cellNode->clearClusterNode();
            cellNode->clearMetaClusterNode();
        }

        for (auto&& [clusterGuid, clusterNode] : clusterNodes)
        {
            if (!clusterNode->getCluster())
                continue;

            for (auto&& cell : clusterNode->getCluster()->cells)
                cellNodes.at(diagram->getCellAt(cell).getVoronoiCenter())->setClusterNode(clusterGuid);
        }

        for (auto&& [clusterGuid, clusterNode] : metaClusterNodes)
        {
            if (!clusterNode->getMetaCluster())
                continue;

            for (auto&& cell : clusterNode->getMetaCluster()->getCells())
                cellNodes.at(diagram->getCellAt(cell).getVoronoiCenter())->setMetaClusterNode(clusterGuid);
        }
    }

    void StageTools<EGenerationStage::FeaturePlacement>::loadSnapshotData()
    {
        disconnectNodes();

        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> metaClustersToUpdate;
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> clustersToUpdate;

        for (auto it = metaClusterNodes.begin(); it != metaClusterNodes.end();)
            if (auto&& [metaClusterGuid, metaClusterNode] = *it; metaClusterNode->isCreatedOnCurrentStage())
            {
                metaClustersToUpdate << metaClusterNode->getMetaCluster();

                Generation::Data::get()->removeMetaCluster(metaClusterNode->getType(), metaClusterNode->getMetaCluster());
                for (auto&& cell : metaClusterNode->getMetaCluster()->getCells())
                    terrainMetaClustersMap[cell] = nullptr;

                currentMetaClusters.erase(currentMetaClusters.find(metaClusterGuid));
                it = metaClusterNodes.erase(it);
            }
            else
                it++;

        for (auto it = clusterNodes.begin(); it != clusterNodes.end();)
            if (auto&& [clusterGuid, clusterNode] = *it; clusterNode->isCreatedOnCurrentStage())
            {
                clustersToUpdate << clusterNode->getCluster();

                for(auto&& cell : clusterNode->getCluster()->cells)
                    Generation::Data::get()->setClusterForCell(cell, nullptr);
                if (auto&& metaCluster = clusterNode->getCluster()->metaCluster)
                    metaCluster->removeCluster(clusterNode->getCluster());
                currentClusters.erase(currentClusters.find(clusterGuid));
                it = clusterNodes.erase(it);
            }
            else
                it++;

        for (auto&& [metaClusterGuid, metaClusterNode] : metaClusterNodes)
            if (auto&& snapshotMetaCluster = metaClusterNode->getSnapshot())
            {
                // modify
                if (metaClusterNode->getMetaCluster())
                {
                    auto&& clusterToUpdate = metaClusterNode->getMetaCluster();
                    clusterToUpdate->setCells(snapshotMetaCluster->cells);
                    clusterToUpdate->setTerrainTexPack(snapshotMetaCluster->terrainTexPack);
                    clusterToUpdate->setBiomeTexPack(snapshotMetaCluster->biomeTexPack);
                    clusterToUpdate->setPackParams(snapshotMetaCluster->packParams);
                }
                // add
                else
                {
                    auto&& newMetaCluster = spawnMetaCluster(snapshotMetaCluster->cells, metaClusterNode->getType(), false, metaClusterGuid);
                    newMetaCluster->setTerrainTexPack(snapshotMetaCluster->terrainTexPack);
                    newMetaCluster->setBiomeTexPack(snapshotMetaCluster->biomeTexPack);
                    newMetaCluster->setPackParams(snapshotMetaCluster->packParams);

                    metaClusterNode->setMetaCluster(newMetaCluster);
                }

                for (auto&& cell : metaClusterNode->getMetaCluster()->getCells())
                    terrainMetaClustersMap[cell] = metaClusterNode->getMetaCluster();

                metaClustersToUpdate << metaClusterNode->getMetaCluster();

                metaClusterNode->clearSnapshot();
            }

        // clusters needs meta cluster to exist already
        for (auto&& [clusterGuid, clusterNode] : clusterNodes)
            if (auto&& snapshotCluster = clusterNode->getSnapshot())
            {
                auto&& metaCluster = metaClusterNodes[snapshotCluster->metaClusterGuid]->getMetaCluster();

                // modify
                if (clusterNode->getCluster())
                {
                    auto&& clusterToUpdate = clusterNode->getCluster();
                    clusterToUpdate->cells = snapshotCluster->cells;
                    clusterToUpdate->keyCell = snapshotCluster->keyCell;
                    clusterToUpdate->borderPoints = snapshotCluster->borderPoints;
                    clusterToUpdate->temperatureRange = snapshotCluster->temperatureRange;
                    clusterToUpdate->humidityRange = snapshotCluster->humidityRange;

                    if (!contains(metaCluster->getClusters(), clusterToUpdate))
                        metaCluster->addCluster(clusterToUpdate);
                }
                // add
                else
                {
                    auto newCluster = spawnCluster(snapshotCluster->cells, metaCluster, clusterGuid);
                    newCluster->keyCell = snapshotCluster->keyCell;
                    newCluster->borderPoints = snapshotCluster->borderPoints;
                    newCluster->temperatureRange = snapshotCluster->temperatureRange;
                    newCluster->humidityRange = snapshotCluster->humidityRange;

                    clusterNode->setCluster(newCluster);
                }

                for (auto&& cell : clusterNode->getCluster()->cells)
                    Generation::Data::get()->setClusterForCell(cell, clusterNode->getCluster());

                clustersToUpdate << clusterNode->getCluster();

                clusterNode->clearSnapshot();
            }

        showClusterBorders(true);
        connectNodes();
    }

    bool StageTools<EGenerationStage::FeaturePlacement>::validatePipeline()
    {
        // modified meta cluster == changed cluster or metacluster
        for (auto&& [metaClusterGuid, metaClusterNode] : metaClusterNodes)
        {
            // removed metacluster
            if (!metaClusterNode->getMetaCluster())
                return false;
            // added metacluster
            else if (metaClusterNode->isCreatedOnCurrentStage())
                return false;
            // modified metacluster
            else if (metaClusterNode->getSnapshot())
                return false;
        }

        return true;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::updatePipeline()
    {
        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        std::scoped_lock lock(nodesGuard);

        if (auto&& cluster = object.dynamicCast<Generation::TerrainBlockClusterBase>(); cluster && !clusterNodes.contains(cluster->getGuid()))
            clusterNodes[cluster->getGuid()] = QSharedPointer<ClusterNode>::create(cluster);
        else if (auto&& metaCluster = object.dynamicCast<Generation::TerrainBlockMetaClusterBase>(); metaCluster && !metaClusterNodes.contains(metaCluster->getGuid()))
            metaClusterNodes[metaCluster->getGuid()] = QSharedPointer<MetaClusterNode>::create(metaCluster);
    }

    void StageTools<EGenerationStage::FeaturePlacement>::removeNode(QSharedPointer<Editable> object)
    {
        std::scoped_lock lock(nodesGuard);

        if (auto&& cluster = object.dynamicCast<Generation::TerrainBlockClusterBase>(); cluster && clusterNodes.contains(cluster->getGuid()))
        {
            if (clusterNodes[cluster->getGuid()]->isCreatedOnCurrentStage())
                clusterNodes.erase(cluster->getGuid());
            else
            {
                if (!clusterNodes[cluster->getGuid()]->getSnapshot())
                    clusterNodes[cluster->getGuid()]->makeSnapshot();

                clusterNodes[cluster->getGuid()]->nullifyCluster();
            }
        }
        else if (auto&& metaCluster = object.dynamicCast<Generation::TerrainBlockMetaClusterBase>(); metaCluster && metaClusterNodes.contains(metaCluster->getGuid()))
        {
            if (metaClusterNodes[metaCluster->getGuid()]->isCreatedOnCurrentStage())
                metaClusterNodes.erase(metaCluster->getGuid());
            else
            {
                if (!metaClusterNodes[metaCluster->getGuid()]->getSnapshot())
                    metaClusterNodes[metaCluster->getGuid()]->makeSnapshot();

                metaClusterNodes[metaCluster->getGuid()]->nullifyMetaCluster();
            }
        }
    }

    void StageTools<EGenerationStage::FeaturePlacement>::modifyNode(QSharedPointer<Editable> object)
    {
        std::scoped_lock lock(nodesGuard);

        if (auto&& cluster = object.dynamicCast<Generation::TerrainBlockClusterBase>(); cluster && clusterNodes.contains(cluster->getGuid()) && !clusterNodes[cluster->getGuid()]->isCreatedOnCurrentStage() && !clusterNodes[cluster->getGuid()]->getSnapshot())
            clusterNodes[cluster->getGuid()]->makeSnapshot();
        else if (auto&& metaCluster = object.dynamicCast<Generation::TerrainBlockMetaClusterBase>(); metaCluster && metaClusterNodes.contains(metaCluster->getGuid()) && !metaClusterNodes[metaCluster->getGuid()]->isCreatedOnCurrentStage() && !metaClusterNodes[metaCluster->getGuid()]->getSnapshot())
            metaClusterNodes[metaCluster->getGuid()]->makeSnapshot();
    }

    void StageTools<EGenerationStage::FeaturePlacement>::setupActions()
    {
        actions[EFeaturePlacementAction::AutoGenerateSelection] = new QAction(QIcon(), "Auto Generate Selection", this);
        connect(actions[EFeaturePlacementAction::AutoGenerateSelection], &QAction::triggered, this, [this]()
            {
                auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();
                auto&& diagram = *Generation::Data::get()->getTerrainCells();

                auto&& selection = FeaturePlacementSelectionMgr::get()->getSelection<EFeaturePlacementSelection::Cell>();
                auto&& selectedCells = std::unordered_set<int>(selection.begin(), selection.end());

                std::unordered_set<int> assignedCells;
                for (int i = 0; i < diagram.getCells().size(); i++)
                    if (!selectedCells.contains(i))
                        assignedCells.insert(i);

                static const auto typeGeter = [](const ETerrainBlock& type) { return type; };

                std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> modifiedMetaClusters;
                while (true)
                {
                    if (selectedCells.empty())
                        break;
                    auto idx = *selectedCells.begin();

                    auto&& maxSize = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetClusterTraits>(blockTypeMap[idx]).maxSize;
                    auto&& metaClusterCells = convertQSetToSTL(Generation::Utils::createMetaCluster(blockTypeMap, typeGeter, diagram, idx, &assignedCells, maxSize));
                    modifiedMetaClusters << spawnMetaCluster(metaClusterCells, blockTypeMap[*metaClusterCells.begin()], true);
                    for (auto&& cell : metaClusterCells)
                        selectedCells.erase(cell);
                }

                std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> modifiedClusters;
                for (auto&& metaCluster : modifiedMetaClusters)
                    modifiedClusters << metaCluster->getClusters();

                auto&& [clustersToUpdate, metaClustersToUpdate] = updateDataByModifiedMetaClusters(modifiedMetaClusters);
                updateDataByModifiedClusters(modifiedClusters);
                metaClustersToUpdate << modifiedMetaClusters;
                clustersToUpdate << modifiedClusters;

                updateClusterMarkers(metaClustersToUpdate, clustersToUpdate);

                finishEditing(editedClustersData);
                editedClustersData.clear();
            });

        actions[EFeaturePlacementAction::CreateMetaClusters] = new QAction(QIcon(), "Create Meta Clusters", this);
        connect(actions[EFeaturePlacementAction::CreateMetaClusters], &QAction::triggered, this, [this]()
            {
                auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

                auto&& selection = FeaturePlacementSelectionMgr::get()->getSelection<EFeaturePlacementSelection::Cell>();
                auto&& selectedCells = std::unordered_set<int>(selection.begin(), selection.end());
                auto&& cellClusters = clusterSelectedCells(selectedCells);

                std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> modifiedMetaClusters;
                for (auto&& cellCluster : cellClusters)
                    modifiedMetaClusters << spawnMetaCluster(cellCluster, blockTypeMap[*cellCluster.begin()]);

                auto&& [clustersToUpdate, metaClustersToUpdate] = updateDataByModifiedMetaClusters(modifiedMetaClusters);
                metaClustersToUpdate << modifiedMetaClusters;

                updateClusterMarkers(metaClustersToUpdate, clustersToUpdate);

                finishEditing(editedClustersData);
                editedClustersData.clear();
            });

        actions[EFeaturePlacementAction::RemoveMetaClusterCells] = new QAction(QIcon(), "Remove Cells From Meta Clusters", this);
        connect(actions[EFeaturePlacementAction::RemoveMetaClusterCells], &QAction::triggered, this, [this]()
            {
                auto&& selection = FeaturePlacementSelectionMgr::get()->getSelection<EFeaturePlacementSelection::Cell>();
                auto&& selectedCells = std::unordered_set<int>(selection.begin(), selection.end());

                auto&& [clustersToUpdate, metaClustersToUpdate] = updateDataByDeletedCells(selectedCells);
                updateClusterMarkers(metaClustersToUpdate, clustersToUpdate);

                finishEditing(editedClustersData);
                editedClustersData.clear();
            });

        actions[EFeaturePlacementAction::CreateClusters] = new QAction(QIcon(), "Create Clusters", this);
        connect(actions[EFeaturePlacementAction::CreateClusters], &QAction::triggered, this, [this]()
            {
                auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

                auto&& selection = FeaturePlacementSelectionMgr::get()->getSelection<EFeaturePlacementSelection::Cell>();
                auto&& selectedCells = std::unordered_set<int>(selection.begin(), selection.end());
                auto&& cellClusters = clusterSelectedCells(selectedCells, true);

                std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> modifiedClusters;
                for (auto&& cellCluster : cellClusters)
                    modifiedClusters << spawnCluster(cellCluster, terrainMetaClustersMap[*cellCluster.begin()]);

                auto&& clustersToUpdate = updateDataByModifiedClusters(modifiedClusters);
                clustersToUpdate << modifiedClusters;

                updateClusterMarkers({}, clustersToUpdate);

                finishEditing(editedClustersData);
                editedClustersData.clear();
            });

        actions[EFeaturePlacementAction::RemoveClusterCells] = new QAction(QIcon(), "Remove Cells From Clusters", this);
        connect(actions[EFeaturePlacementAction::RemoveClusterCells], &QAction::triggered, this, [this]()
            {
                auto&& selection = FeaturePlacementSelectionMgr::get()->getSelection<EFeaturePlacementSelection::Cell>();
                auto&& selectedCells = std::unordered_set<int>(selection.begin(), selection.end());

                auto&& [clustersToUpdate, metaClustersToUpdate] = updateDataByDeletedCells(selectedCells, false);
                updateClusterMarkers(metaClustersToUpdate, clustersToUpdate);

                finishEditing(editedClustersData);
                editedClustersData.clear();
            });
    }

    bool StageTools<EGenerationStage::FeaturePlacement>::eventFilter(QObject* obj, QEvent* event)
    {
        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            // Draw brush circle
            if (selectedTool == EFeaturePlacementToolType::Brush)
            {
                if (!mEvent->buttons().testFlag(Qt::RightButton))
                    DrawUtils::drawBrushCircle(mEvent, 36, brushSize * brushScale, brushMarker);
                else
                    DrawUtils::clearBrush(brushMarker);
            }
            else if (brushMarker)
                DrawUtils::clearBrush(brushMarker);

            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
                if (selectedTool == EFeaturePlacementToolType::Brush)
                {
                    if (clusterBrush)
                        brushClusters(mEvent);
                    else if (metaClusterBrush)
                        brushMetaClusters(mEvent);
                }
            }
        }
        else if (mEvent->type() == QEvent::MouseButtonRelease)
        {
            if (mEvent->button() == Qt::LeftButton)
            {
                brushEditedCluster = nullptr;
                brushEditedMetaCluster = nullptr;

                if (selectedTool == EFeaturePlacementToolType::Brush)
                    if (!editedClustersData.empty())
                    {
                        finishEditing(editedClustersData);
                        editedClustersData.clear();
                    }
            }
            else if (mEvent->button() == Qt::RightButton)
            {
                if (!QApplication::overrideCursor())
                    FeaturePlacementSelectionMgr::get()->rightClick(mEvent);
            }
        }

        return false;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::brushClusters(QMouseEvent* mEvent)
    {
        auto cellSection = SelectionMgrBase::findObjectUnderCursor<DBlockTypeMarker>();
        if (!cellSection)
            return;

        int cellHit = gBlockTypeMarker->painter.trianglesToCells[cellSection->primitive];
        auto&& cellNodes = DrawUtils::getCellsUnderBrush(cellHit, brushSize * brushScale);

        if (!terrainMetaClustersMap[cellHit])
            return;

        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> clustersToUpdate;

        bool append = isKeyDown(VK_SHIFT);
        bool extract = isKeyDown(VK_CONTROL);

        if (extract)
        {
            std::unordered_set<int> cellsToDelete;

            for (auto&& blockNode : cellNodes)
                if (terrainMetaClustersMap[blockNode->data] == terrainMetaClustersMap[cellHit])
                    cellsToDelete.insert(blockNode->data);

            std::tie(clustersToUpdate, std::ignore) = updateDataByDeletedCells(cellsToDelete, false);
        }
        else
        {
            if (!brushEditedCluster)
            {
                if (append)
                {
                    brushEditedCluster = clusterMap[cellHit];
                    extendCluster(brushEditedCluster, cellNodes);
                }
                else
                {
                    brushEditedCluster = spawnCluster(cellNodes, cellHit, terrainMetaClustersMap[cellHit]);
                }
            }
            else
            {
                extendCluster(brushEditedCluster, cellNodes);
            }

            clustersToUpdate = updateDataByModifiedClusters({ brushEditedCluster });
            clustersToUpdate << brushEditedCluster;
        }

        updateClusterMarkers({}, clustersToUpdate);
    }

    void StageTools<EGenerationStage::FeaturePlacement>::brushMetaClusters(QMouseEvent* mEvent)
    {
        auto cellSection = SelectionMgrBase::findObjectUnderCursor<DBlockTypeMarker>();
        if (!cellSection)
            return;

        int cellHit = gBlockTypeMarker->painter.trianglesToCells[cellSection->primitive];
        auto&& cellNodes = DrawUtils::getCellsUnderBrush(cellHit, brushSize * brushScale);

        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> clustersToUpdate;
        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> metaClustersToUpdate;

        bool append = isKeyDown(VK_SHIFT);
        bool extract = isKeyDown(VK_CONTROL);

        if (extract)
        {
            std::unordered_set<int> cellsToDelete;

            for (auto&& blockNode : cellNodes)
                cellsToDelete.insert(blockNode->data);

            std::tie(clustersToUpdate, metaClustersToUpdate) = updateDataByDeletedCells(cellsToDelete);
        }
        else
        {
            if (!brushEditedMetaCluster)
            {
                if (append && terrainMetaClustersMap[cellHit])
                {
                    brushEditedMetaCluster = terrainMetaClustersMap[cellHit];
                    extendMetaCluster(brushEditedMetaCluster, cellNodes);
                }
                else
                {
                    brushEditedMetaCluster = spawnMetaCluster(cellNodes, cellHit);
                }
            }
            else
            {
                extendMetaCluster(brushEditedMetaCluster, cellNodes);
            }

            std::tie(clustersToUpdate, metaClustersToUpdate) = updateDataByModifiedMetaClusters({ brushEditedMetaCluster });
            metaClustersToUpdate << brushEditedMetaCluster;
            clustersToUpdate << brushEditedMetaCluster->getClusters();
        }

        updateClusterMarkers(metaClustersToUpdate, clustersToUpdate);
    }

    QSharedPointer<Generation::TerrainBlockMetaClusterBase> StageTools<EGenerationStage::FeaturePlacement>::spawnMetaCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, int sourceCell)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();
        auto&& type = blockTypeMap[sourceCell];

        auto&& selectedCells = selectCellsForMetaCluster(foundBlockNodes, {}, type);

        if (auto&& occupyingCluster = clusterMap[sourceCell])
            if (selectedCells == occupyingCluster->metaCluster->getCells())
                return occupyingCluster->metaCluster;

        return spawnMetaCluster(selectedCells, type);
    }

    QSharedPointer<Generation::TerrainBlockMetaClusterBase> StageTools<EGenerationStage::FeaturePlacement>::spawnMetaCluster(const std::unordered_set<int>& cells, ETerrainBlock type, bool autoGenCluster /*= false*/, std::optional<qint64> guid /*= std::nullopt*/)
    {
        auto metaCluster = Generation::ETerrainBlockConstexpr::UseIn<EAC::CreateMetaCluster>(type, cells, guid);

        if (autoGenCluster)
        {
            metaCluster->spawnBigClusters();
            for(auto&& cluster : metaCluster->getClusters())
                currentClusters[cluster->getGuid()] = cluster;
        }
        Generation::Data::get()->addMetaCluster(metaCluster->getType(), metaCluster);
        currentMetaClusters[metaCluster->getGuid()] = metaCluster;

        return metaCluster;
    }

    QSharedPointer<Generation::TerrainBlockClusterBase> StageTools<EGenerationStage::FeaturePlacement>::spawnCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, int sourceCell, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& selectedCells = selectCellsForCluster(foundBlockNodes, {}, metaCluster);

        auto&& occupyingCluster = clusterMap[sourceCell];
        if (occupyingCluster && occupyingCluster->metaCluster == metaCluster && selectedCells == occupyingCluster->cells)
            return occupyingCluster;

        return spawnCluster(selectedCells, metaCluster);
    }

    QSharedPointer<Generation::TerrainBlockClusterBase> StageTools<EGenerationStage::FeaturePlacement>::spawnCluster(const std::unordered_set<int>& cells, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster, std::optional<qint64> guid /*= std::nullopt*/)
    {
        auto clusterData = Generation::ETerrainBlockConstexpr::UseIn<EAC::CreateClusterData>(metaCluster->getType(), metaCluster.get(), *cells.begin());
        clusterData->cells = cells;

        auto&& cluster = Generation::ETerrainBlockConstexpr::UseIn<EAC::CreateTerrainCluster>(metaCluster->getType(), *clusterData.get(), guid);
        metaCluster->addCluster(cluster);
        currentClusters[cluster->getGuid()] = cluster;

        return cluster;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::extendMetaCluster(const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster, const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes)
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();

        auto&& selectedCells = selectCellsForMetaCluster(foundBlockNodes, metaCluster->getCells(), metaCluster->getType());
        metaCluster->addCells(selectedCells);
    }

    void StageTools<EGenerationStage::FeaturePlacement>::extendCluster(const QSharedPointer<Generation::TerrainBlockClusterBase>& cluster, const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes)
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();

        std::unordered_set<int> candidateCells;
        for (auto&& blockNode : foundBlockNodes)
            if (cluster->metaCluster->getCells().contains(blockNode->data))
                candidateCells.insert(blockNode->data);

        while (true)
        {
            // find candidate if it is neighboring cluster
            auto&& candidateCell = std::find_if(candidateCells.begin(), candidateCells.end(), [&](int c1)
                { return std::any_of(cluster->cells.begin(), cluster->cells.end(), [&](int c2) { return cells[c1].isNeighbor(c2); }); });

            if (candidateCell == candidateCells.end())
                break;

            cluster->addCells({ *candidateCell });
            candidateCells.erase(candidateCell);
        }
    }

    std::unordered_set<int> StageTools<EGenerationStage::FeaturePlacement>::selectCellsForMetaCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, const std::unordered_set<int>& assignedCells, Generation::ETerrainBlock blockType)
    {
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

        std::unordered_set<int> candidateCells;
        for (auto&& blockNode : foundBlockNodes)
            if (!assignedCells.contains(blockNode->data) && blockType == blockTypeMap[blockNode->data])
                candidateCells.insert(blockNode->data);

        return selectCellsIfNeighboring(assignedCells, std::move(candidateCells));
    }

    std::unordered_set<int> StageTools<EGenerationStage::FeaturePlacement>::selectCellsForCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, const std::unordered_set<int>& assignedCells, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster)
    {
        auto&& metaClusterCells = metaCluster->getCells();

        std::unordered_set<int> candidateCells;
        for (auto&& blockNode : foundBlockNodes)
            if (!assignedCells.contains(blockNode->data) && metaClusterCells.contains(blockNode->data))
                candidateCells.insert(blockNode->data);

        return selectCellsIfNeighboring(assignedCells, std::move(candidateCells));
    }

    std::unordered_set<int> StageTools<EGenerationStage::FeaturePlacement>::selectCellsIfNeighboring(const std::unordered_set<int>& assignedCells, std::unordered_set<int>&& candidateCells)
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();

        std::unordered_set<int> selectedCells;

        std::unordered_set<int> usedCells;
        if (assignedCells.empty())
        {
            usedCells.insert(*candidateCells.begin());
            selectedCells.insert(*candidateCells.begin());
            candidateCells.erase(candidateCells.begin());
        }
        else
            usedCells = assignedCells;

        while (true)
        {
            // find candidate if it is neighboring cluster
            auto candidateCell = std::find_if(candidateCells.begin(), candidateCells.end(), [&](int c1)
                { return std::any_of(usedCells.begin(), usedCells.end(), [&](int c2) { return cells[c1].isNeighbor(c2); }); });

            if (candidateCell == candidateCells.end())
                break;

            selectedCells.insert(*candidateCell);
            usedCells.insert(*candidateCell);
            candidateCells.erase(candidateCell);
        }

        return selectedCells;
    }

    std::vector<std::unordered_set<int>> StageTools<EGenerationStage::FeaturePlacement>::clusterSelectedCells(const std::unordered_set<int>& selectedCells, bool boundByMetaClusters /*= false*/, const std::optional<int>& maxSize /*= std::nullopt*/)
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

        std::vector<std::unordered_set<int>> cellClusters;

        tbb::spin_mutex push_guard;
        tbb::parallel_for(0, (int)magic_enum::enum_count<ETerrainBlock>(), [&](int i)
            {
                std::unordered_set<int> cellsPerType;

                auto&& type = magic_enum::enum_value<ETerrainBlock>(i);
                for (auto&& cell : selectedCells)
                    if (type == blockTypeMap[cell] && (!boundByMetaClusters || terrainMetaClustersMap[cell]))
                        cellsPerType << cell;

                while (true)
                {
                    if (cellsPerType.empty())
                        break;

                    std::unordered_set<int> cellCluster{ *cellsPerType.begin() };
                    std::unordered_set<int> cellsToConsider{ *cellsPerType.begin() };
                    auto&& currentMetaCluster = terrainMetaClustersMap[*cellsPerType.begin()];
                    cellsPerType -= *cellsPerType.begin();

                    while (true)
                    {
                        if (cellsToConsider.empty())
                            break;

                        if (maxSize && cellCluster.size() >= maxSize.value())
                            break;

                        auto nextCell = *cellsToConsider.begin();

                        cellCluster.insert(nextCell);
                        cellsToConsider.erase(nextCell);
                        cellsPerType.erase(nextCell);

                        for (auto it = cells[nextCell].getNeighbors().begin(); it != cells[nextCell].getNeighbors().end(); it++)
                            if (!cellCluster.contains(it.key()) && cellsPerType.contains(it.key()) && (!boundByMetaClusters || currentMetaCluster == terrainMetaClustersMap[it.key()]))
                                cellsToConsider.insert(it.key());
                    }

                    {
                        std::scoped_lock lock(push_guard);
                        cellClusters.push_back(cellCluster);
                    }
                }
            });

        return cellClusters;
    }

    std::pair<std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>> StageTools<EGenerationStage::FeaturePlacement>::updateDataByDeletedCells(const std::unordered_set<int>& cellsToDelete, bool updateMeta /*= true*/)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> editedClusters;
        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> editedMetaClusters;

        for (auto&& cellId : cellsToDelete)
        {
            saveClusterEdit(cellId);

            if (auto editedMetaCluster = terrainMetaClustersMap[cellId]; updateMeta && editedMetaCluster)
            {
                editedMetaCluster->removeCells({ cellId });
                terrainMetaClustersMap[cellId] = nullptr;
                if (std::find(editedMetaClusters.begin(), editedMetaClusters.end(), editedMetaCluster) == editedMetaClusters.end())
                    editedMetaClusters << editedMetaCluster;
            }

            if (auto editedCluster = clusterMap[cellId])
            {
                editedCluster->removeCells({ cellId }, updateMeta);
                Generation::Data::get()->setClusterForCell(cellId, nullptr);
                if (std::find(editedClusters.begin(), editedClusters.end(), editedCluster) == editedClusters.end())
                    editedClusters << editedCluster;
            }
        }

        auto&& newClusters = perserveClustersIntegrity(editedClusters);
        editedClusters << newClusters;

        if (updateMeta)
        {
            auto&& newMetaClusters = perserveMetaClustersIntegrity(editedMetaClusters);
            editedMetaClusters << newMetaClusters;
        }

        return { editedClusters, editedMetaClusters };
    }

    std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> StageTools<EGenerationStage::FeaturePlacement>::updateDataByModifiedClusters(const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& modifiedClusters)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> editedClusters;

        for (auto&& cluster : modifiedClusters)
            for (auto&& cellId : cluster->cells)
            {
                saveClusterEdit(cellId);

                if (!clusterMap[cellId])
                {
                    Generation::Data::get()->setClusterForCell(cellId, cluster);
                    continue;
                }

                // DO NOT USE REF
                auto editedCluster = clusterMap[cellId];
                auto editedMetaCluster = clusterMap[cellId]->metaCluster;

                if (std::find(modifiedClusters.begin(), modifiedClusters.end(), editedCluster) != modifiedClusters.end())
                    continue;

                editedCluster->removeCells({ cellId }, false);
                Generation::Data::get()->setClusterForCell(cellId, cluster);

                if (std::find(editedClusters.begin(), editedClusters.end(), editedCluster) == editedClusters.end())
                    editedClusters << editedCluster;
            }

        auto&& newClusters = perserveClustersIntegrity(editedClusters);
        editedClusters << newClusters;

        return editedClusters;
    }

    std::pair<std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>> StageTools<EGenerationStage::FeaturePlacement>::updateDataByModifiedMetaClusters(const std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>& modifiedMetaClusters)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> editedClusters;
        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> editedMetaClusters;

        for (auto&& metaCluster : modifiedMetaClusters)
            for (auto&& cellId : metaCluster->getCells())
            {
                saveClusterEdit(cellId);

                if (!terrainMetaClustersMap[cellId])
                {
                    terrainMetaClustersMap[cellId] = metaCluster;
                    Generation::Data::get()->setClusterForCell(cellId, nullptr);
                    continue;
                }

                // DO NOT USE REF
                auto editedMetaCluster = terrainMetaClustersMap[cellId];
                auto editedCluster = clusterMap[cellId];

                if (std::find(modifiedMetaClusters.begin(), modifiedMetaClusters.end(), editedMetaCluster) != modifiedMetaClusters.end())
                    continue;

                editedMetaCluster->removeCells({cellId});
                terrainMetaClustersMap[cellId] = metaCluster;
                if (std::find(editedMetaClusters.begin(), editedMetaClusters.end(), editedMetaCluster) == editedMetaClusters.end())
                    editedMetaClusters << editedMetaCluster;

                if (editedCluster)
                {
                    editedCluster->removeCells({ cellId });
                    Generation::Data::get()->setClusterForCell(cellId, nullptr);
                    if (std::find(editedClusters.begin(), editedClusters.end(), editedCluster) == editedClusters.end())
                        editedClusters << editedCluster;
                }
            }

        auto&& newClusters = perserveClustersIntegrity(editedClusters);
        editedClusters << newClusters;

        auto&& newMetaClusters = perserveMetaClustersIntegrity(editedMetaClusters);
        editedMetaClusters << newMetaClusters;

        return { editedClusters, editedMetaClusters };
    }

    std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> StageTools<EGenerationStage::FeaturePlacement>::perserveClustersIntegrity(const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& editedClusters)
    {
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> newClusters;

        for (auto&& editedCluster : editedClusters)
            if (editedCluster->cells.empty())
            {
                editedCluster->metaCluster->removeCluster(editedCluster);
                currentClusters.erase(currentClusters.find(editedCluster->getGuid()));
            }
            else
            {
                auto&& cellClusters = clusterSelectedCells(editedCluster->cells);

                if (cellClusters.size() > 1)
                {
                    editedCluster->setCells(cellClusters.front());

                    for (auto&& cellId : editedCluster->cells)
                        saveClusterEdit(cellId);

                    for (int i = 1; i < cellClusters.size(); i++)
                    {
                        auto&& newCluster = spawnCluster(cellClusters[i], editedCluster->metaCluster);
                        for (auto&& cellId : newCluster->cells)
                        {
                            saveClusterEdit(cellId);
                            Generation::Data::get()->setClusterForCell(cellId, newCluster);
                        }
                        newClusters.push_back(newCluster);
                    }
                }
            }

        return newClusters;
    }

    std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> StageTools<EGenerationStage::FeaturePlacement>::perserveMetaClustersIntegrity(const std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>& editedMetaClusters)
    {
        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> newMetaClusters;

        for (auto&& editedMetaCluster : editedMetaClusters)
            if (editedMetaCluster->getCells().empty())
            {
                Generation::Data::get()->removeMetaCluster(editedMetaCluster->getType(), editedMetaCluster);
                currentMetaClusters.erase(currentMetaClusters.find(editedMetaCluster->getGuid()));
            }
            else
            {
                // copy, editedMetaCluster is later changed
                auto clusters = editedMetaCluster->getClusters();

                auto&& nonClusterCellsClusters = clusterSelectedCells(editedMetaCluster->selectNonClusterCells());
                auto&& clusterCells = editedMetaCluster->selectCellsPerCluster();
                clusterCells << nonClusterCellsClusters;

                auto getClusterInfo = [&](const std::vector<int>& clusterOfClusters)
                {
                    std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> newClusters;
                    std::unordered_set<int> nonClusterCells;

                    for (auto&& idx : clusterOfClusters)
                        if (idx < clusters.size())
                            newClusters << clusters[idx];
                        else
                            nonClusterCells += clusterCells[idx];

                    return std::pair{ newClusters , nonClusterCells };
                };

                auto&& clusterCluesters = clusterSelectedClusters(clusterCells);

                if (clusterCluesters.size() > 1)
                {
                    auto&& [frontClusters, frontNonClusterCells] = getClusterInfo(clusterCluesters.front());
                    editedMetaCluster->setClusters(frontClusters);
                    editedMetaCluster->addCells(frontNonClusterCells);

                    for(auto&& cellId : editedMetaCluster->getCells())
                        saveClusterEdit(cellId);

                    for (int i = 1; i < clusterCluesters.size(); i++)
                    {
                        auto&& [nextClusters, nextNonClusterCells] = getClusterInfo(clusterCluesters[i]);

                        std::unordered_set<int> cells;
                        for (auto&& cluster : nextClusters)
                            cells.insert(cluster->cells.begin(), cluster->cells.end());

                        auto metaCluster = spawnMetaCluster(cells, editedMetaCluster->getType());
                        metaCluster->setClusters(nextClusters);
                        metaCluster->addCells(nextNonClusterCells);
                        for (auto&& cellId : metaCluster->getCells())
                        {
                            saveClusterEdit(cellId);
                            terrainMetaClustersMap[cellId] = metaCluster;
                        }

                        newMetaClusters.push_back(metaCluster);
                    }
                }
            }

        return newMetaClusters;
    }

    std::vector<std::vector<int>> StageTools<EGenerationStage::FeaturePlacement>::clusterSelectedClusters(const std::vector<std::unordered_set<int>>& selectedClusters)
    {
        auto&& clustersNeighbors = calculateClusterNeighborRelation(selectedClusters);

        std::vector<std::vector<int>> clusterClusters;
        std::vector<bool> assignedClusters(selectedClusters.size());

        auto findNeighborCluster = [&](const std::vector<int>& clusterOfClusters) -> std::optional<int>
        {
            for (int i = 0; i < selectedClusters.size(); i++)
            {
                if (assignedClusters[i])
                    continue;

                auto&& clusterNeighbors = clustersNeighbors[i];

                for (auto&& clusterToCheck : clusterOfClusters)
                    if (clusterNeighbors.contains(clusterToCheck))
                        return i;
            }

            return std::nullopt;
        };

        while (true)
        {
            int notAssignedCluster = std::distance(assignedClusters.begin(), std::find(assignedClusters.begin(), assignedClusters.end(), false));

            if (notAssignedCluster >= assignedClusters.size())
                break;

            std::vector<int> clusterOfClusters{ notAssignedCluster };
            assignedClusters[notAssignedCluster] = true;

            while (true)
            {
                auto clusterToAdd = findNeighborCluster(clusterOfClusters);

                if (!clusterToAdd)
                    break;

                clusterOfClusters.push_back(*clusterToAdd);
                assignedClusters[*clusterToAdd] = true;
            }

            clusterClusters.push_back(clusterOfClusters);
        }

        return clusterClusters;
    }

    std::vector<std::unordered_set<int>> StageTools<EGenerationStage::FeaturePlacement>::calculateClusterNeighborRelation(const std::vector<std::unordered_set<int>>& selectedClusters)
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        std::unordered_map<int /*cellId*/, int /*cluster*/> selectedClusterMap;
        for (int i = 0; i < selectedClusters.size(); i++)
            for (auto&& cell : selectedClusters[i])
                selectedClusterMap[cell] = i;

        std::vector<std::unordered_set<int>> clustersNeighbors(selectedClusters.size());

        tbb::parallel_for(0, int(selectedClusters.size()), [&](int i)
            {
                std::unordered_set<int> neighborCells;

                auto&& cluster = selectedClusters[i];
                for (auto&& cell : cluster)
                {
                    auto&& neighborSet = cells[cell].getNeighborsSet();
                    neighborCells.insert(neighborSet.begin(), neighborSet.end());
                }
                for(auto&& cell : cluster)
                    neighborCells.erase(cell);

                for (auto&& cell : neighborCells)
                    if (selectedClusterMap.contains(cell))
                        clustersNeighbors[i].insert(selectedClusterMap[cell]);
            });

        return clustersNeighbors;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::saveClusterEdit(int cellId)
    {
        if (editedClustersData.contains(cellId))
            return;

        std::optional<std::pair<qint64, std::optional<qint64>>> newClusterData;

        if (auto&& metaCluster = terrainMetaClustersMap[cellId])
            newClusterData = { metaCluster->getGuid(), std::nullopt };
        if (auto&& cluster = Generation::Data::get()->getTerrainClustersMap()[cellId])
            newClusterData->second = cluster->getGuid();

        editedClustersData[cellId] = newClusterData;
    }

    bool StageTools<EGenerationStage::FeaturePlacement>::finishEditing(const std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>>& clustersChanges)
    {
        HISTORY_PUSH(finishEditing, {});

        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> modifiedMetaClusters;
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> modifiedClusters;

        std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>> oldClustersData;
        std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>> newClustersData;

        if (!HISTORY_LOAD2(newClustersData, oldClustersData))
        {
            oldClustersData = clustersChanges;

            for (auto&& [cellId, clustersData] : oldClustersData)
            {
                std::optional<std::pair<qint64, std::optional<qint64>>> newClusterData;

                if (auto&& metaCluster = terrainMetaClustersMap[cellId])
                    newClusterData = { metaCluster->getGuid(), std::nullopt};
                if (auto&& cluster = clusterMap[cellId])
                    newClusterData->second = cluster->getGuid();

                newClustersData[cellId] = newClusterData;
            }

            HISTORY_SAVE2(newClustersData, oldClustersData);
        }
        else
        {
            applyEditClusterData(newClustersData);
        }

        return true;
    }

    bool StageTools<EGenerationStage::FeaturePlacement>::finishEditing_Undo(const std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>>& clustersChanges)
    {
        HISTORY_POP();

        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> modifiedMetaClusters;
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> modifiedClusters;

        std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>> oldClustersData;
        std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>> newClustersData;
        HISTORY_LOAD2(newClustersData, oldClustersData);
        applyEditClusterData(oldClustersData);

        return true;
    }

    void StageTools<EGenerationStage::FeaturePlacement>::applyEditClusterData(const std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>>& clustersChanges)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> modifiedMetaClusters;
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> modifiedClusters;

        for (auto&& [cellId, clustersData] : clustersChanges)
        {
            // Meta cluster
            if (clustersData)
            {
                auto&& [metaClusterGuid, clusterGuid] = *clustersData;

                QSharedPointer<Generation::TerrainBlockMetaClusterBase> metaCluster;

                if (!currentMetaClusters.contains(metaClusterGuid))
                    metaCluster = spawnMetaCluster({ cellId }, blockTypeMap[cellId], false, metaClusterGuid);
                else
                {
                    metaCluster = currentMetaClusters[metaClusterGuid];
                    metaCluster->addCells({ cellId });
                }

                if (std::find(modifiedMetaClusters.begin(), modifiedMetaClusters.end(), metaCluster) == modifiedMetaClusters.end())
                    modifiedMetaClusters << metaCluster;

                if (auto&& previousMetaCluster = terrainMetaClustersMap[cellId]; previousMetaCluster && previousMetaCluster != metaCluster)
                {
                    if (std::find(modifiedMetaClusters.begin(), modifiedMetaClusters.end(), previousMetaCluster) == modifiedMetaClusters.end())
                        modifiedMetaClusters << previousMetaCluster;
                    previousMetaCluster->removeCells({ cellId });
                }
                terrainMetaClustersMap[cellId] = metaCluster;

                // Cluster
                if (clusterGuid)
                {
                    QSharedPointer<Generation::TerrainBlockClusterBase> cluster;

                    if (!currentClusters.contains(*clusterGuid))
                        cluster = spawnCluster({ cellId }, metaCluster, *clusterGuid);
                    else
                    {
                        cluster = currentClusters[*clusterGuid];
                        cluster->addCells({ cellId });
                    }

                    if (cluster->metaCluster != metaCluster)
                    {
                        cluster->metaCluster->removeCluster(cluster);
                        metaCluster->addCluster(cluster);
                    }

                    if (std::find(modifiedClusters.begin(), modifiedClusters.end(), cluster) == modifiedClusters.end())
                        modifiedClusters << cluster;

                    if (auto&& previousCluster = clusterMap[cellId]; previousCluster && previousCluster != cluster)
                    {
                        if (std::find(modifiedClusters.begin(), modifiedClusters.end(), previousCluster) == modifiedClusters.end())
                            modifiedClusters << previousCluster;
                        previousCluster->removeCells({ cellId }, previousCluster->metaCluster != metaCluster);
                    }
                    Generation::Data::get()->setClusterForCell(cellId, cluster);
                }
                // No Cluster
                else
                {
                    if (auto&& previousCluster = clusterMap[cellId])
                    {
                        if (std::find(modifiedClusters.begin(), modifiedClusters.end(), previousCluster) == modifiedClusters.end())
                            modifiedClusters << previousCluster;
                        previousCluster->removeCells({ cellId }, previousCluster->metaCluster != metaCluster);
                    }
                    Generation::Data::get()->setClusterForCell(cellId, nullptr);
                }
            }
            // No meta cluster or cluster
            else
            {
                if (auto&& previousMetaCluster = terrainMetaClustersMap[cellId])
                {
                    if (std::find(modifiedMetaClusters.begin(), modifiedMetaClusters.end(), previousMetaCluster) == modifiedMetaClusters.end())
                        modifiedMetaClusters << previousMetaCluster;
                    previousMetaCluster->removeCells({ cellId });
                }
                terrainMetaClustersMap[cellId] = nullptr;

                if (auto&& previousCluster = clusterMap[cellId])
                {
                    if (std::find(modifiedClusters.begin(), modifiedClusters.end(), previousCluster) == modifiedClusters.end())
                        modifiedClusters << previousCluster;
                    previousCluster->removeCells({ cellId });
                }
                Generation::Data::get()->setClusterForCell(cellId, nullptr);
            }
        }

        // Remove clusters/meta clusters with no cells
        for (auto&& cluster : modifiedClusters)
            if (cluster->cells.empty())
            {
                cluster->metaCluster->removeCluster(cluster);
                currentClusters.erase(cluster->getGuid());
            }

        for (auto&& metaCluster : modifiedMetaClusters)
            if (metaCluster->getCells().empty())
            {
                Generation::Data::get()->removeMetaCluster(metaCluster->getType(), metaCluster);
                currentMetaClusters.erase(metaCluster->getGuid());
            }

        if (!modifiedMetaClusters.empty() || !modifiedClusters.empty())
            updateClusterMarkers(modifiedMetaClusters, modifiedClusters);
    }

    void StageTools<EGenerationStage::FeaturePlacement>::updateClusterMarkers(const std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>& metaClustersToUpdate, const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& clustersToUpdate)
    {
        std::mutex metaClustersGuard, clustersGuard;
        auto&& dem = Generation::Data::get()->getDEM();

        // notice intentional access to map to create entry for parallel_for
        for(auto&& metaCluster : metaClustersToUpdate)
            if (metaClusterMarkers[metaCluster->getGuid()])
                despawnBatched(metaClusterMarkers[metaCluster->getGuid()]);

        tbb::parallel_for(0, int(metaClustersToUpdate.size()), [&](int idx)
        {
            auto&& metaCluster = metaClustersToUpdate[idx];

            if (metaCluster->getCells().empty())
            {
                std::scoped_lock lock(metaClustersGuard);
                metaClusterMarkers.erase(metaCluster->getGuid());
            }
            else
            {
                auto&& metaPolygon = metaCluster->calculatePolygon(true);
                std::vector<QVector3D> metaPolygon3D(metaPolygon.getPts().size());

                for (int i = 0; i < metaPolygon3D.size(); i++)
                    metaPolygon3D[i] = QVector3D(metaPolygon[i].x, dem->heightData.sample(metaPolygon[i]) + 20, metaPolygon[i].z);

                auto section = spawnBatched(buildLineGeometry(metaPolygon3D, true), metaClusterBorderParams);

                std::scoped_lock lock(metaClustersGuard);
                metaClusterMarkers[metaCluster->getGuid()] = section;
            }
        });

        // notice intentional access to map to create entry for parallel_for
        for (auto&& cluster : clustersToUpdate)
            if (clusterMarkers[cluster->getGuid()])
                despawnBatched(clusterMarkers[cluster->getGuid()]);

        tbb::parallel_for(0, int(clustersToUpdate.size()), [&](int idx)
        {
            auto&& cluster = clustersToUpdate[idx];

            if (cluster->cells.empty())
            {
                std::scoped_lock lock(clustersGuard);
                clusterMarkers.erase(cluster->getGuid());
            }
            else
            {
                auto&& clusterPolygon = cluster->calculatePolygon(true);
                std::vector<QVector3D> metaPolygon3D(clusterPolygon.getPts().size());

                for (int i = 0; i < metaPolygon3D.size(); i++)
                    metaPolygon3D[i] = QVector3D(clusterPolygon[i].x, dem->heightData.sample(clusterPolygon[i]) + 15, clusterPolygon[i].z);

                auto section = spawnBatched(buildLineGeometry(metaPolygon3D, true), clusterBorderParams);

                std::scoped_lock lock(clustersGuard);
                clusterMarkers[cluster->getGuid()] = section;
            }
        });

        Generation::Data::get()->initializeQueuedMarkers();

#if DEBUG_CLUSTER_VALIDATION
        {
            std::unordered_set<int> assignedCells;
            auto&& dem = Generation::Data::get()->getDEM();
            auto&& metaClusters = Generation::Data::get()->getTerrainMetaClusters();
            auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

            std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> accurateMetaClusterMap(terrainMetaClustersMap.size());
            std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> accurateClusterMap(clusterMap.size());
            for (auto&& metaClusterVec : metaClusters)
                for (auto&& metaCluster : metaClusterVec)
                {
                    Q_ASSERT(std::find(terrainMetaClustersMap.begin(), terrainMetaClustersMap.end(), metaCluster) != terrainMetaClustersMap.end());
                    Q_ASSERT(currentMetaClusters.contains(metaCluster->getGuid()));

                    std::unordered_set<int> metaClustersCells;

                    auto&& nonClusterCellsClusters = clusterSelectedCells(metaCluster->selectNonClusterCells());
                    auto&& clusterCells = metaCluster->selectClusterCells();
                    auto combinedClusters = clusterCells;
                    combinedClusters << nonClusterCellsClusters;

                    auto&& clusters = clusterSelectedClusters(combinedClusters);
                    Q_ASSERT(clusters.size() == 1);
                    for (auto&& cluster : metaCluster->getClusters())
                    {
                        auto&& cellClusters = clusterSelectedCells(cluster->cells);
                        Q_ASSERT(cellClusters.size() == 1);
                        Q_ASSERT(currentClusters.contains(cluster->getGuid()));

                        for (auto&& cell : cluster->cells)
                        {
                            Q_ASSERT(terrainMetaClustersMap[cell]);
                            Q_ASSERT(!assignedCells.contains(cell));
                            Q_ASSERT(clusterMap[cell] == cluster);
                            assignedCells.insert(cell);
                            metaClustersCells.insert(cell);

                            accurateMetaClusterMap[cell] = metaCluster;
                            accurateClusterMap[cell] = cluster;
                        }
                    }
                    for (auto&& nonClusterCell : metaCluster->selectNonClusterCells())
                    {
                        Q_ASSERT(terrainMetaClustersMap[nonClusterCell]);
                        Q_ASSERT(!assignedCells.contains(nonClusterCell));
                        Q_ASSERT(!clusterMap[nonClusterCell]);
                        assignedCells.insert(nonClusterCell);
                        metaClustersCells.insert(nonClusterCell);

                        accurateMetaClusterMap[nonClusterCell] = metaCluster;
                    }

                    Q_ASSERT(metaClustersCells == metaCluster->getCells());
                }
            Q_ASSERT(terrainMetaClustersMap == accurateMetaClusterMap);
            Q_ASSERT(clusterMap == accurateClusterMap);

            for (auto&& [guid, marker] : metaClusterMarkers)
            {
                Q_ASSERT(std::find(terrainMetaClustersMap.begin(), terrainMetaClustersMap.end(), currentMetaClusters[marker->getGuid()]) != terrainMetaClustersMap.end());
            }
            for (auto&& metaCluster : terrainMetaClustersMap)
            {
                auto&& c = metaCluster;
                if (metaCluster)
                    Q_ASSERT(metaClusterMarkers.contains(metaCluster->getGuid()));
            }

            for (auto&& [guid, marker] : clusterMarkers)
            {
                Q_ASSERT(std::find(clusterMap.begin(), clusterMap.end(), currentClusters[marker->getGuid()]) != clusterMap.end());
            }
            for (auto&& cluster : clusterMap)
            {
                auto&& c = cluster;
                if (cluster)
                    Q_ASSERT(clusterMarkers.contains(cluster->getGuid()));
            }
        }
#endif
    }

    void StageTools<EGenerationStage::FeaturePlacement>::showClusterBorders(bool forceUpdate /*= false*/)
    {
        // Already present?
        if (std::scoped_lock instanceLock(gClusterBorderMarkerGuard); !forceUpdate && gClusterBorderMarker)
            if (auto&& [batches, guard] = gClusterBorderMarker->getBatches(); !batches.empty())
                return;

        // Clear all previous data
        clearAllBatches<ClusterBorderBatchParams>();
        metaClusterMarkers.clear();
        clusterMarkers.clear();

        auto&& dem = Generation::Data::get()->getDEM();
        auto&& metaClusters = Generation::Data::get()->getTerrainMetaClusters();
        tbb::spin_mutex pushGuard;

        for (auto&& metaClusterVec : metaClusters)
            tbb::parallel_for(0, int(metaClusterVec.size()), [&](int idx)
                {
                    auto&& metaPolygon = metaClusterVec[idx]->calculatePolygon(true);
                    std::vector<QVector3D> metaPolygon3D(metaPolygon.getPts().size());

                    for (int i = 0; i < metaPolygon3D.size(); i++)
                        metaPolygon3D[i] = QVector3D(metaPolygon[i].x, dem->heightData.sample(metaPolygon[i]) + 20, metaPolygon[i].z);

                    auto&& section = spawnBatched(buildLineGeometry(metaPolygon3D, true), metaClusterBorderParams);
                    {
                        std::scoped_lock lock(pushGuard);
                        metaClusterMarkers[metaClusterVec[idx]->getGuid()] = section;
                    }
                });

        for (auto&& metaClusterVec : metaClusters)
            tbb::parallel_for(0, int(metaClusterVec.size()), [&](int idx)
                {
                    for (auto&& cluster : metaClusterVec[idx]->getClusters())
                    {
                        auto&& clusterPolygon = cluster->calculatePolygon(true);
                        std::vector<QVector3D> clusterPolygon3D(clusterPolygon.getPts().size());

                        for (int i = 0; i < clusterPolygon3D.size(); i++)
                            clusterPolygon3D[i] = QVector3D(clusterPolygon[i].x, dem->heightData.sample(clusterPolygon[i]) + 15, clusterPolygon[i].z);

                        auto&& section = spawnBatched(buildLineGeometry(clusterPolygon3D, true), clusterBorderParams);
                        {
                            std::scoped_lock lock(pushGuard);
                            clusterMarkers[cluster->getGuid()] = section;
                        }
                    }
                });


        Generation::Data::get()->initializeQueuedMarkers();
    }
}