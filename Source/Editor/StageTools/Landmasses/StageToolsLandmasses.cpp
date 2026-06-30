#include "stdafx.h"
#include "StageToolsLandmasses.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "LandmassSelection.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h" 
#include "Scene/Generation/Stages/Landmasses/LandmassBoundMarker.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"
#include "Scene/Generation/Stages/Landmasses/SeamassMarker.h"
#include "Scene/Generation/Stages/Landmasses/LandmassMarker.h"
#include "Scene/Generation/Stages/Layout/StageGeneration_Layout.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include <Source/Scene/Generation/OmnigenGeneration.h>
#include <Source/Scene/Generation/Stages/Landmasses/ShorelineUtils.h>
#include "../StageTools.h"
#include "../StageObjectNode.h"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

namespace Design
{
    StageTools<EGenerationStage::Landmasses>::StageTools()
        : StageToolsBase()
    {
        setupActions();
    }

    SelectionMgrBase* StageTools<EGenerationStage::Landmasses>::getSelectionMgr() const
    {
        return LandmassSelectionMgr::get();
    }

    void StageTools<EGenerationStage::Landmasses>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* genData = Generation::Data::get();
        auto* outline = omnigen->getOutline();
        auto* properties = omnigen->getProperties();

        // Widget setup
        auto* toolbar = createOutlineToolbar();

        treeView = new OutlineTreeView;
        treeView->setModel(&treeModel);
        treeModel.setTreeView(treeView);
        treeModel.loadLandmasses();

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, treeView });

        treeView->show();

        // Data events
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(&treeModel, &QLandmassTreeModel::addItem);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Modified>(&treeModel, &QLandmassTreeModel::updateItem);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(&treeModel, &QLandmassTreeModel::removeItem);

        // Selection mgr
        auto* selMgr = LandmassSelectionMgr::get();
        qConnections << connect(selMgr, &SelectionMgrBase::selectionChanged, this, &StageTools<EGenerationStage::Landmasses>::onSelectionChanged);

        connect(&tickerForCliffEditing, &QTimer::timeout, this, &StageTools<EGenerationStage::Landmasses>::editCliffsByBrush);
        tickerForCliffEditing.setInterval(16);

        // Viewport events
        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }

        clearSeasideData();
        calculateSeasideData();
    }

    void StageTools<EGenerationStage::Landmasses>::unbind()
    {
        StageToolsBase::unbind();

        isLandmassSpawning = false;
        isLandmassEditing = false;

        // Clear brush size circle
        if (brushMarker)
        {
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
            brushMarker = nullptr;
        }

        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }

        treeModel.clear();

        clearSeasideData();
    }

    void StageTools<EGenerationStage::Landmasses>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();
        genData->saveMarkers<DLandmassMarker>(writer);
        genData->saveMarkers<DSeamassMarker>(writer);
        genData->saveMarkers<DShorelineMarker>(writer);
        genData->saveMarkers<DLandmassBound>(writer);
        writer << genData->getDomainHeightBounds();

        writer << landmassNodes;
        writer << shorelineNodes;
    }

    void StageTools<EGenerationStage::Landmasses>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();
        genData->loadMarkers<DLandmassMarker>(reader);
        genData->loadMarkers<DSeamassMarker>(reader);
        genData->loadMarkers<DShorelineMarker>(reader);
        genData->loadMarkers<DLandmassBound>(reader);

        auto&& landmasses = genData->getMarkers<DLandmassMarker>();
        for (auto&& landmass : landmasses)
        {
            for (auto&& shorelineGuid : landmass->getShorelinesGuids())
                landmass->addShoreline(genData->findMarkerByGuid<DShorelineMarker>(shorelineGuid));

            for (auto&& shorelineGuid : landmass->getInnerSeaShorelinesGuids())
                landmass->addInnerSeaShoreline(genData->findMarkerByGuid<DShorelineMarker>(shorelineGuid));
        }

        auto&& shorelines = genData->getMarkers<DShorelineMarker>();
        for (auto&& shoreline : shorelines)
            shoreline->setLandmass(genData->findMarkerByGuid<DLandmassMarker>(shoreline->getLandmassGuid()));

        std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>> heightBounds;
        reader >> heightBounds;
        genData->setDomainHeightBounds(heightBounds);

        reader >> landmassNodes;
        reader >> shorelineNodes;
    }

    void StageTools<EGenerationStage::Landmasses>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::Landmasses>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::Landmasses>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::Landmasses>::modifyNode);
    }

    void StageTools<EGenerationStage::Landmasses>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::Landmasses>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::Landmasses>::clearNodes()
    {
        landmassNodes.clear();
        shorelineNodes.clear();
    }

    void StageTools<EGenerationStage::Landmasses>::cleanNodesState()
    {
        std::erase_if(shorelineNodes, [](auto& kv) { return !kv.second->getShoreline(); });
        std::erase_if(landmassNodes, [](auto& kv) { return !kv.second->getLandmass(); });

        for (auto&& [shorelineGuid, shorelineNode] : shorelineNodes)
        {
            shorelineNode->setCreatedOnCurrentStage(false);
            shorelineNode->clearSnapshot();
        }
        for (auto&& [landmassGuid, landmassNode] : landmassNodes)
        {
            landmassNode->setCreatedOnCurrentStage(false);
            landmassNode->clearSnapshot();
        }
    }

    void StageTools<EGenerationStage::Landmasses>::updateParentNodes()
    {
        auto&& layoutStageTools = getStageTools<EGenerationStage::Layout>();
        auto&& domainSquareNodes = layoutStageTools->getDomainSquareNodes();

        for (auto&& [square, squareNode] : domainSquareNodes)
        {
            squareNode->clearShorelineNodes();
            squareNode->clearLandmassNodes();
        }

        for (auto&& [landmassGuid, landmassNode] : landmassNodes)
        {
            if (!landmassNode->getLandmass())
                continue;

            auto&& landmass = landmassNode->getLandmass();

            landmass->forEachShoreline([&](auto& s, bool isInner) 
                { 
                    for (auto&& sq : s->getSquares())
                        domainSquareNodes.at(sq)->addLandmassNode(landmass);
                });

            for (auto&& sq : landmass->getSquares())
                domainSquareNodes.at(sq)->addLandmassNode(landmass);
        }

        for (auto&& [shorelineGuid, shorelineNode] : shorelineNodes)
        {
            if (!shorelineNode->getShoreline())
                continue;

            auto&& shoreline = shorelineNode->getShoreline();

            for (auto&& sq : shoreline->getSquares())
                domainSquareNodes.at(sq)->addShorelineNode(shoreline);
        }
    }

    void StageTools<EGenerationStage::Landmasses>::loadSnapshotData()
    {
        disconnectNodes();

        for (auto it = shorelineNodes.begin(); it != shorelineNodes.end();)
        {
            auto&& [shorelineGuid, shorelineNode] = *it;

            if (auto&& snapshotShoreline = shorelineNode->getSnapshot())
            {
                // modify
                if (shorelineNode->getShoreline())
                {
                    auto&& shorelineToUpdate = shorelineNode->getShoreline();
                    shorelineToUpdate->setName(snapshotShoreline->name);
                    shorelineToUpdate->setSegmentWidth(snapshotShoreline->segmentWidth);
                    shorelineToUpdate->setBays(snapshotShoreline->baysRoot);
                    shorelineToUpdate->setPeninsulas(snapshotShoreline->peninsulasRoot);
                    shorelineToUpdate->setSquares(snapshotShoreline->squares);
                    shorelineToUpdate->setPoints(snapshotShoreline->points);
                    shorelineToUpdate->setHeights(snapshotShoreline->shorelineHeights);

                    QOmnigenViewport::updateDrawable(shorelineToUpdate);
                }
                // add
                else
                {
                    auto&& newShoreline = spawn<DShorelineMarker>(snapshotShoreline->points, snapshotShoreline->isCoast);
                    newShoreline->setGuid(shorelineGuid);
                    newShoreline->setName(snapshotShoreline->name);
                    newShoreline->setSegmentWidth(snapshotShoreline->segmentWidth);
                    newShoreline->setBays(snapshotShoreline->baysRoot);
                    newShoreline->setPeninsulas(snapshotShoreline->peninsulasRoot);
                    newShoreline->setSquares(snapshotShoreline->squares);
                    newShoreline->setHeights(snapshotShoreline->shorelineHeights);

                    shorelineNode->setShoreline(newShoreline);
                }

                shorelineNode->clearSnapshot();
            }
            // remove
            else if (shorelineNode->isCreatedOnCurrentStage())
            {
                Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(shorelineGuid);
                it = shorelineNodes.erase(it);
                continue;
            }

            it++;
        }

        for (auto it = landmassNodes.begin(); it != landmassNodes.end();)
        {
            auto&& [landmassGuid, landmassNode] = *it;

            if (auto&& snapshotLandmass = landmassNode->getSnapshot())
            {
                std::vector<QSharedPointer<DShorelineMarker>> shorelines;
                std::vector<QSharedPointer<DShorelineMarker>> innerSeaShorelines;

                for (auto&& shorelineGuid : snapshotLandmass->shorelines)
                    shorelines << shorelineNodes[shorelineGuid]->getShoreline();

                for (auto&& shorelineGuid : snapshotLandmass->innerSeaShorelines)
                    innerSeaShorelines << shorelineNodes[shorelineGuid]->getShoreline();

                // modify
                if (landmassNode->getLandmass())
                {
                    auto&& landmassToUpdate = landmassNode->getLandmass();
                    landmassToUpdate->setName(snapshotLandmass->name);
                    landmassToUpdate->setCoast(snapshotLandmass->coast);
                    landmassToUpdate->setSquares(snapshotLandmass->squares);
                    landmassToUpdate->setShoreline(shorelines);
                    landmassToUpdate->setInnerSeaShoreline(innerSeaShorelines);
                    landmassToUpdate->setPolygons(snapshotLandmass->mainPolygon, snapshotLandmass->cutPolygons);

                    QOmnigenViewport::updateDrawable(landmassToUpdate);
                }
                // add
                else
                {
                    auto&& newLandmass = spawn<DLandmassMarker>(shorelines, innerSeaShorelines);
                    newLandmass->setGuid(landmassGuid);
                    newLandmass->setName(snapshotLandmass->name);

                    landmassNode->setLandmass(newLandmass);
                }

                landmassNode->getLandmass()->forEachShoreline([&](auto& s, bool isInner)
                    {
                        s->setLandmass(landmassNode->getLandmass());
                    });

                landmassNode->clearSnapshot();
            }
            // remove
            else if (landmassNode->isCreatedOnCurrentStage())
            {
                Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmassGuid);
                it = landmassNodes.erase(it);
                continue;
            }

            it++;
        }

        finalizeLandmassChanges(allSeasideData);
        connectNodes();
    }

    bool StageTools<EGenerationStage::Landmasses>::validatePipeline()
    {
        for (auto&& [landmassGuid, landmassNode] : landmassNodes)
        {
            // removed landmass
            if (!landmassNode->getLandmass())
                return false;
            // added landmass
            else if (landmassNode->isCreatedOnCurrentStage())
                return false;
            // modified landmass
            else if (landmassNode->getSnapshot())
                return false;
        }

        return true;
    }

    void StageTools<EGenerationStage::Landmasses>::updatePipeline()
    {
        auto&& layoutStageTools = getStageTools<EGenerationStage::Layout>();
        auto&& ridgeStageTools = getStageTools<EGenerationStage::Ridges>();
        auto&& isohypseStageTools = getStageTools<EGenerationStage::ContourLines>();
        auto&& domainSquareNodes = layoutStageTools->getDomainSquareNodes();
        auto&& ridgeNodes = ridgeStageTools->getRidgeNodes();
        auto&& isohypseNodes = isohypseStageTools->getIsohypseNodes();

        std::unordered_set<qint64> isohypsesToInvalidate;
        std::unordered_set<qint64> ridgeNodesToInvalidate;

        QSet<GPoint> deletedLandmassSquares;
        QSet<GPoint> createdLandmassSquares;
        QSet<GPoint> moddifiedShorelineSquares;

        std::unordered_set<qint64> modifiedShorelines;
        std::unordered_map<GPoint, std::unordered_set<qint64>> moddifiedShorelineMap;
        QSet<GPoint> removedLandmassSquares;
        QSet<GPoint> addedLandmassSquares;

        for (auto&& [landmassGuid, landmassNode] : landmassNodes)
        {
            // removed landmass
            if (!landmassNode->getLandmass())
            {
                removedLandmassSquares += landmassNode->getSnapshot()->squares;
            }
            // added landmass
            else if (landmassNode->isCreatedOnCurrentStage())
            {
                addedLandmassSquares += landmassNode->getLandmass()->getSquares();
            }
            // modified landmass
            else if (landmassNode->getSnapshot())
            {
                removedLandmassSquares += landmassNode->getSnapshot()->squares - landmassNode->getLandmass()->getSquares();
                addedLandmassSquares += landmassNode->getLandmass()->getSquares() - landmassNode->getSnapshot()->squares;
            }
        }

        for (auto&& [shorelineGuid, shorelineNode] : shorelineNodes)
        {
            if (shorelineNode->getShoreline())
                for (auto&& sq : shorelineNode->getShoreline()->getSquares())
                    moddifiedShorelineMap[sq] += shorelineNode->getShoreline()->getGuid();

            if (shorelineNode->getSnapshot() || shorelineNode->isCreatedOnCurrentStage())
            {
                modifiedShorelines += shorelineGuid;

                if (shorelineNode->getShoreline())
                    moddifiedShorelineSquares += shorelineNode->getShoreline()->getSquares();
                if (shorelineNode->getSnapshot())
                    moddifiedShorelineSquares += shorelineNode->getSnapshot()->squares;
            }
        }

        deletedLandmassSquares = removedLandmassSquares - addedLandmassSquares;
        createdLandmassSquares = addedLandmassSquares - removedLandmassSquares;

        std::unordered_set<qint64> isohypsesInsideShorelineSquares;
        for (auto&& sq : moddifiedShorelineSquares)
            isohypsesInsideShorelineSquares += domainSquareNodes.at(sq)->getIsohypseNodes();

        isohypsesToInvalidate += isohypsesInsideShorelineSquares;

        // Check against other IH affected by modified shoreline
        for (auto&& ihGuid : isohypsesInsideShorelineSquares)
            if (isohypseNodes.contains(ihGuid) && isohypseNodes.at(ihGuid)->getIsohypse())
            {
                auto&& isohypse = isohypseNodes.at(ihGuid)->getIsohypse();
            
                std::set<Isohypse*> parentIhs = isohypse->getParentIHs();

                while (true)
                {
                    if (parentIhs.empty())
                        break;

                    auto&& parentIh = *parentIhs.begin();

                    if (!isohypsesToInvalidate.contains(parentIh->getGuid()) && !container_and(parentIh->data.affectedBy[EIHAffectType::Shoreline], modifiedShorelines).empty())
                    {
                        parentIhs.insert(parentIh->getParentIHs().begin(), parentIh->getParentIHs().end());
                        isohypsesToInvalidate += parentIh->getGuid();
                    }
                    
                    parentIhs.erase(parentIh);
                }
            }

        for (auto&& isohypseToInvalidate : isohypsesToInvalidate)
            if (isohypseNodes.contains(isohypseToInvalidate) && isohypseNodes.at(isohypseToInvalidate)->getIsohypse())
            {
                auto isohypse = isohypseNodes.at(isohypseToInvalidate)->getIsohypse();
                isohypseStageTools->removeNode(isohypse);

                auto&& sources = isohypse->data.sources;
                for (auto&& src : sources)
                    if (src.ih != nullptr && src.idx != -1 && src.ihGuid != -1 && src.ih->descendants.size() > 0)
                        src.ih->setDescendant(src.idx, IHSrcInfo());

                despawnBatched(isohypse);
            }

        std::unordered_set<qint64> affectedRidges;
        for (auto&& sq : (deletedLandmassSquares + moddifiedShorelineSquares))
            affectedRidges += domainSquareNodes.at(sq)->getRidgeNodes();

        for (auto&& affectedRidge : affectedRidges)
        {
            if (ridgeNodesToInvalidate.contains(affectedRidge) && ridgeNodes.at(affectedRidge)->getRidge())
                continue;

            auto&& ridge = ridgeNodes.at(affectedRidge)->getRidge();
            auto&& deletedRidgeSquares = container_and(ridge->getSquares(), deletedLandmassSquares);
            auto&& createdRidgeSquares = container_and(ridge->getSquares(), createdLandmassSquares);
            auto&& ridgeOnModifiedShorelineSquares = container_and(ridge->getSquares(), moddifiedShorelineSquares);
            auto&& shorelineOnDeletedSquares = container_and(deletedRidgeSquares, ridgeOnModifiedShorelineSquares);
            auto&& shorelineOnCreatedSquares = container_and(createdRidgeSquares, ridgeOnModifiedShorelineSquares);

            // invalidate if ridge is on deleted square that has no shoreline
            if (!deletedRidgeSquares.empty() && (shorelineOnDeletedSquares.size() != deletedRidgeSquares.size() || shorelineOnDeletedSquares.empty()))
            {
                ridge->forEachChild([&](auto& r) { ridgeNodesToInvalidate.insert(r->getGuid()); }, ridge);
            }
            // check if ridge on shoreline is inside any of shoreline's landmass
            else if (!ridgeOnModifiedShorelineSquares.empty() && (shorelineOnCreatedSquares.size() != createdRidgeSquares.size() || shorelineOnCreatedSquares.empty()))
            {
                std::unordered_set<qint64> landmassGuids;

                for(auto&& sq : ridgeOnModifiedShorelineSquares)
                    if (moddifiedShorelineMap.contains(sq))
                        for (auto&& shorelineGuid : moddifiedShorelineMap[sq])
                        {
                            landmassGuids += shorelineNodes[shorelineGuid]->getShoreline()->getLandmass().lock()->getGuid();
                        }

                if (!std::any_of(landmassGuids.begin(), landmassGuids.end(), [&](auto& lm) { return landmassNodes[lm]->getLandmass()->isInside(ridge->getControlPoints()); }))
                    ridge->forEachChild([&](auto& r) { ridgeNodesToInvalidate.insert(r->getGuid()); }, ridge);
            }
        }

        for (auto&& ridgeNodeToInvalidate : ridgeNodesToInvalidate)
        {
            auto ridge = ridgeNodes.at(ridgeNodeToInvalidate)->getRidge();

            ridgeStageTools->removeNode(ridge);

            if (!ridge->getParent())
                Generation::Data::get()->clearSingleExactMarker<DRidgeMarker>(ridge->getGuid());
            else
            {
                emit Editable::aboutToBeDeleted(ridge);
                ridge->getParent().lock()->removeChild(ridge);
            }
        }

        ridgeStageTools->updatePipeline();
        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::Landmasses>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        if (auto&& landmass = object.dynamicCast<DLandmassMarker>(); landmass && !landmassNodes.contains(landmass->getGuid()))
            landmassNodes[landmass->getGuid()] = QSharedPointer<LandmassNode>::create(landmass);
        else if (auto&& shoreline = object.dynamicCast<DShorelineMarker>(); shoreline && !shorelineNodes.contains(shoreline->getGuid()))
            shorelineNodes[shoreline->getGuid()] = QSharedPointer<ShorelineNode>::create(shoreline);
    }

    void StageTools<EGenerationStage::Landmasses>::removeNode(QSharedPointer<Editable> object)
    {
        if (auto&& landmass = object.dynamicCast<DLandmassMarker>(); landmass && landmassNodes.contains(landmass->getGuid()))
        {
            if (landmassNodes[landmass->getGuid()]->isCreatedOnCurrentStage())
                landmassNodes.erase(landmass->getGuid());
            else
            {
                if (!landmassNodes[landmass->getGuid()]->getSnapshot())
                    landmassNodes[landmass->getGuid()]->makeSnapshot();

                landmassNodes[landmass->getGuid()]->nullifyLandmass();
            }
        }
        else if (auto&& shoreline = object.dynamicCast<DShorelineMarker>(); shoreline && shorelineNodes.contains(shoreline->getGuid()))
        {
            if (shorelineNodes[shoreline->getGuid()]->isCreatedOnCurrentStage())
                shorelineNodes.erase(shoreline->getGuid());
            else
            {
                if (!shorelineNodes[shoreline->getGuid()]->getSnapshot())
                    shorelineNodes[shoreline->getGuid()]->makeSnapshot();

                shorelineNodes[shoreline->getGuid()]->nullifyShoreline();
            }
        }
    }

    void StageTools<EGenerationStage::Landmasses>::modifyNode(QSharedPointer<Editable> object)
    {
        if (auto&& landmass = object.dynamicCast<DLandmassMarker>(); landmass && landmassNodes.contains(landmass->getGuid()) && !landmassNodes[landmass->getGuid()]->isCreatedOnCurrentStage() && !landmassNodes[landmass->getGuid()]->getSnapshot())
            landmassNodes[landmass->getGuid()]->makeSnapshot();
        else if (auto&& shoreline = object.dynamicCast<DShorelineMarker>(); shoreline && shorelineNodes.contains(shoreline->getGuid()) && !shorelineNodes[shoreline->getGuid()]->isCreatedOnCurrentStage() && !shorelineNodes[shoreline->getGuid()]->getSnapshot())
            shorelineNodes[shoreline->getGuid()]->makeSnapshot();
    }

    void StageTools<EGenerationStage::Landmasses>::setupActions()
    {
        actions[ELandmassAction::DeleteSelectedLandmasses] = new QAction(QIcon(), "Delete Landmass", this);
        connect(actions[ELandmassAction::DeleteSelectedLandmasses], &QAction::triggered, this, [this]()
            {
                std::vector<QSharedPointer<DLandmassMarker>> landmasses;
                for (auto&& landmass : LandmassSelectionMgr::get()->getSelection<ELandmassSelection::Landmass>())
                    landmasses << landmass;

                deleteLandmasses(landmasses);
            });

        actions[ELandmassAction::ReshapeShoreline] = new QAction(QIcon(), "Reshape shoreline", this);
        connect(actions[ELandmassAction::ReshapeShoreline], &QAction::triggered, this, [this]()
            {
                std::vector<QSharedPointer<DShorelineMarker>> shorelines;
                for (auto&& shoreline : LandmassSelectionMgr::get()->getSelection<ELandmassSelection::Shoreline>())
                    shorelines << shoreline;

                reshapeShoreline(shorelines);
            });
    }

    bool StageTools<EGenerationStage::Landmasses>::eventFilter(QObject* obj, QEvent* event)
    {
        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
                if (isLandmassSpawning)
                    spawnLandmass(mEvent);
                else if (isLandmassEditing || isCliffEditing)
                {
                    oldBrushPos = OmnigenCameraMgr::get()->findPointInWorld(60, mEvent->x(), mEvent->y());
                    brushPointForCliffEditing = oldBrushPos;
                }

                if (isCliffEditing)
                    tickerForCliffEditing.start();
            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            if (isLandmassEditing && (isLandmassEditAppend || isLandmassEditExtract) || isCliffEditing)
            {
                if (!mEvent->buttons().testFlag(Qt::RightButton))
                {
                    drawBrushCircle(mEvent, 24);
                }
                else if (brushMarker)
                {
                    Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
                    brushMarker = nullptr;
                }

                if (isCliffEditing)
                    brushPointForCliffEditing = OmnigenCameraMgr::get()->findPointInWorld(60, mEvent->x(), mEvent->y());

                if (mEvent->buttons().testFlag(Qt::LeftButton))
                {
                    if (isLandmassEditing)
                        updateLandmassBrushPolygons(mEvent);
                }
            }
            else if (brushMarker)
            {
                Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
                brushMarker = nullptr;
            }
        }
        else if (mEvent->type() == QEvent::MouseButtonRelease)
        {
            if (mEvent->button() == Qt::LeftButton)
            {
                updateLandmassesWithBrushPolygons();

                if (!editLandmassData.empty())
                {
                    finishEditing(editLandmassData);
                    editLandmassData.clear();
                }

                if (tickerForCliffEditing.isActive())
                    tickerForCliffEditing.stop();
            }
            else if (mEvent->button() == Qt::RightButton)
            {
                if (!QApplication::overrideCursor())
                    LandmassSelectionMgr::get()->rightClick(mEvent);
            }
        }

        return false;
    }

    void StageTools<EGenerationStage::Landmasses>::updateTreeViewSelection()
    {
        blockSignals(true);

        treeModel.clearSelection();

        for (auto&& landmass : LandmassSelectionMgr::get()->getSelection<ELandmassSelection::Landmass>())
            treeModel.selectItem(landmass->getGuid());

        for (auto&& shoreline : LandmassSelectionMgr::get()->getSelection<ELandmassSelection::Shoreline>())
            treeModel.selectItem(shoreline->getGuid());

        blockSignals(false);
    }

    void StageTools<EGenerationStage::Landmasses>::onSelectionChanged()
    {
        updateTreeViewSelection();
        QOmnigenViewportSection::repaintAll();
    }

    std::map<float, ShorelineFindResult>  StageTools<EGenerationStage::Landmasses>::findShorelinesInRange(const GVector2D& point, float range)
    {
        auto shorelines = Generation::Data::get()->getMarkers<DShorelineMarker>();
        
        std::map<float, ShorelineFindResult> shorelinesInRange;

        for (auto&& shoreline : shorelines)
        {
            float closestDistance = std::numeric_limits<float>::max();
            int closestIdx = -1;


            for (int idx = 0; idx < shoreline->getControlPoints().size(); idx++)
                if (auto dist = point.dist(shoreline->getControlPoints()[idx]); dist < range && dist < closestDistance)
                {
                    closestDistance = dist;
                    closestIdx = idx;
                }

            if (closestIdx >= 0)
            {
                shorelinesInRange[closestDistance] = { shoreline, closestIdx};
            }
        }

        return shorelinesInRange;
    }

    std::vector<LandmassFindResult> StageTools<EGenerationStage::Landmasses>::findOverlapingLandmasses(const std::vector<std::vector<QVector3D>>& polygons)
    {
        auto landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();
        std::vector<BoundingBox> landmassesBoundingBoxes;
        for (auto&& landmass : landmasses)
            landmassesBoundingBoxes << PolygonUtils::calculateBB(landmass->getMainPolygon());

        std::vector<BoundingBox> polygonsBoundingBoxes;
        for(auto&& polygon : polygons)
            polygonsBoundingBoxes << PolygonUtils::calculateBB(polygon);

        std::vector<LandmassFindResult> landmassesInRange;

        tbb::spin_mutex pushGuard;
        tbb::parallel_for(0, int(landmasses.size()), [&](int i)
            {
                auto&& landmass = landmasses[i];
                auto&& shorelines = landmass->getShorelines();
                auto&& innerShorelines = landmass->getInnerSeaShorelines();

                std::vector<BoundingBox> shorelinesBoundingBoxes;
                for (auto&& shoreline : shorelines)
                    shorelinesBoundingBoxes << PolygonUtils::calculateBB(shoreline->getControlPoints());

                std::vector<BoundingBox> innerShorelinesBoundingBoxes;
                for (auto&& shoreline : innerShorelines)
                    innerShorelinesBoundingBoxes << PolygonUtils::calculateBB(shoreline->getControlPoints());

                bool shouldAddLandmass = false;
                std::vector<QSharedPointer<DShorelineMarker>> shorelineInRange;
                std::vector<QSharedPointer<DShorelineMarker>> innerShorelineInRange;

                for (int j = 0; j < polygons.size(); j++)
                {
                    auto&& polygon = polygons[j];
                    bool addedShoreline = false;

                    if (!polygonsBoundingBoxes[j].overlaps(landmassesBoundingBoxes[i]))
                        continue;

                    tbb::spin_mutex pushGuard2;
                    tbb::parallel_for(0, int(shorelines.size()), [&](int k)
                        {
                            auto&& shoreline = shorelines[k];

                            if (std::find(shorelineInRange.begin(), shorelineInRange.end(), shoreline) != shorelineInRange.end() ||
                                !polygonsBoundingBoxes[j].overlaps(shorelinesBoundingBoxes[k]))
                                return;

                            if (PolygonUtils::containsAny(shoreline->getControlPoints(), polygon))
                            {
                                std::scoped_lock lock(pushGuard2);
                                shorelineInRange << shoreline;
                                addedShoreline = true;
                            }
                        });

                    tbb::parallel_for(0, int(innerShorelines.size()), [&](int k)
                        {
                            auto&& shoreline = innerShorelines[k];

                            if (std::find(innerShorelineInRange.begin(), innerShorelineInRange.end(), shoreline) != innerShorelineInRange.end() ||
                                !polygonsBoundingBoxes[j].overlaps(innerShorelinesBoundingBoxes[k]))
                                return;

                            if (PolygonUtils::containsAny(shoreline->getControlPoints(), polygon))
                            {
                                std::scoped_lock lock(pushGuard2);
                                innerShorelineInRange << shoreline;
                                addedShoreline = true;
                            }
                        });

                    if (!addedShoreline && PolygonUtils::contains(polygon.front(), landmass->getMainPolygon()))
                        shouldAddLandmass = true;
                }

                if (shouldAddLandmass || !shorelineInRange.empty() || !innerShorelineInRange.empty())
                {
                    std::scoped_lock lock(pushGuard);
                    landmassesInRange << LandmassFindResult(landmass, shorelineInRange, innerShorelineInRange);
                }
            });

        return landmassesInRange;
    }

    void StageTools<EGenerationStage::Landmasses>::clearSeasideData()
    {
        allSeasideData.clear();
        seasideSquareMap.clear();
    }

    void StageTools<EGenerationStage::Landmasses>::calculateSeasideData()
    {
        auto&& seasides = Generation::Utils::findSeasideAreas();
        auto&& shorelines = Generation::Data::get()->getMarkers<DShorelineMarker>();

        for (auto&& seaside : seasides)
        {
            auto&& newSeasideData = QSharedPointer<SeasideData>::create();
            newSeasideData->allSeaside = seaside;
            newSeasideData->emptySeaside = seaside;

            for (auto&& shoreline : shorelines)
            {
                if (seaside.contains(*shoreline->getSquares().begin()))
                {
                    newSeasideData->shorelineSeaside += shoreline->getSquares();
                    newSeasideData->emptySeaside -= shoreline->getSquares();

                    for (auto&& sq : shoreline->getSquares())
                        newSeasideData->shorelineSquareMap[sq] = shoreline;

                    auto&& landmass = shoreline->getLandmass().lock();

                    auto&& landArea = container_and(landmass->getSquares(), seaside);
                    newSeasideData->landSeaside += landArea;
                    newSeasideData->emptySeaside -= landArea;

                    for (auto&& sq : landArea)
                        newSeasideData->landmassSquareMap[sq] = landmass;
                }
            }

            allSeasideData << newSeasideData;

            for (auto&& sq : seaside)
                seasideSquareMap[sq] = newSeasideData;
        }
    }

    void StageTools<EGenerationStage::Landmasses>::recalculateSeasideData(const QSharedPointer<SeasideData>& seasideData)
    {
        auto&& shorelines = Generation::Data::get()->getMarkers<DShorelineMarker>();

        auto seaside = seasideData->allSeaside;
        seasideData->clear();

        seasideData->allSeaside = seaside;
        seasideData->emptySeaside = seaside;

        for (auto&& shoreline : shorelines)
        {
            if (seaside.contains(*shoreline->getSquares().begin()))
            {
                seasideData->shorelineSeaside += shoreline->getSquares();
                seasideData->emptySeaside -= shoreline->getSquares();

                for (auto&& sq : shoreline->getSquares())
                    seasideData->shorelineSquareMap[sq] = shoreline;

                auto&& landmass = shoreline->getLandmass().lock();

                auto&& landArea = container_and(landmass->getSquares(), seaside);
                seasideData->landSeaside += landArea;
                seasideData->emptySeaside -= landArea;

                for (auto&& sq : landArea)
                    seasideData->landmassSquareMap[sq] = landmass;
            }
        }
    }

    void StageTools<EGenerationStage::Landmasses>::finalizeLandmassChanges(const std::vector<QSharedPointer<SeasideData>>& seasideDatas, bool respawnSeamass /*= true*/)
    {
        Generation::Data::get()->initializeQueuedMarkers();
        for (auto&& seasideData : seasideDatas)
            recalculateSeasideData(seasideData);

        if (respawnSeamass)
        {
            auto&& landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();
            Generation::Data::get()->clearExactMarkers<DSeamassMarker>();
            DSeamassMarker::generateSeamassMarkers(landmasses);
            Generation::Data::get()->initializeQueuedMarkers();
        }
    }

    void StageTools<EGenerationStage::Landmasses>::reshapeShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& shorelines)
    {
        TODO("reshapeShoreline: need more work to make sure it work with shorelines which share squares");

        std::unordered_map<qint64, DLandmassMarkerData> changeData;

        QSet<QSharedPointer<DLandmassMarker>> reshapedLandmasses;
        for (auto&& shoreline : shorelines)
        {
            auto&& landmass = shoreline->getLandmass().lock();
            auto&& waterDomain = Generation::Data::get()->getDomainAtSquare(*shoreline->getSquares().begin(), EDomainType::Water);
            auto&& domainData = waterDomain->getData<EDomainType::Water>();

            DShorelineMarker::IslandParameters params;
            params.coverage = PIslandsRatioCoverage[domainData->landCoverage];
            params.smallIslandsQuantity = PIslandsWeightQuanity[domainData->amountOfSmallIslands];
            params.mediumIslandsQuantity = PIslandsWeightQuanity[domainData->amountOfMediumIslands];
            params.largeIslandsQuantity = PIslandsWeightQuanity[domainData->amountOfLargeIslands];
            params.shorelineComplexity = PShorelineComplexity[domainData->shorelineComplexity];
            auto&& shorelinePath = DShorelineMarker::generateshorelinePath(landmass->getSquares(), shoreline->getSquares(), params, landmass->isCoast());

            saveLandmassData(&changeData, ELandmassEditType::Change, landmass);
            shoreline->setPoints(shorelinePath, !landmass->isCoast());
            QOmnigenViewport::updateDrawable(shoreline);
            reshapedLandmasses += landmass;
        }

        for (auto&& landmass : reshapedLandmasses)
        {
            landmass->recalculateLandmassPolygons();
            QOmnigenViewport::updateDrawable(landmass);
        }

        finishEditing(changeData);
    }

    void StageTools<EGenerationStage::Landmasses>::deleteLandmasses(const std::vector<QSharedPointer<DLandmassMarker>>& landmasses)
    {
        std::unordered_map<qint64, DLandmassMarkerData> changeData;

        for (auto&& landmass : landmasses)
        {
            saveLandmassData(&changeData, ELandmassEditType::Delete, landmass);
            Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
            landmass->forEachShoreline([&](auto& s, bool isInner) { Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(s->getGuid()); });

            auto&& initLandmasses = spawnInitialLandmasses(landmass);
            for(auto&& initLandmass : initLandmasses)
                saveLandmassData(&changeData, ELandmassEditType::Create, initLandmass);
        }

        finishEditing(changeData);
    }

    std::vector<QSharedPointer<DLandmassMarker>> StageTools<EGenerationStage::Landmasses>::spawnInitialLandmasses(const QSharedPointer<DLandmassMarker>& landmass)
    {
        QSet<GPoint> terrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
        QSet<GPoint> seaSquares = Generation::Data::get()->getAllSquares<EDomainType::Water>();

        QSet<GPoint> landSquares = container_and(terrainSquares - seaSquares, landmass->getSquares());
        QSet<GPoint> seasideSquares = container_and(terrainSquares, seaSquares);

        if (landSquares.empty())
            return {};

        auto landPolygons = PolygonUtils::calculatePolygonsFromGridSquares(landSquares);
        auto seasidePolygons = PolygonUtils::calculatePolygonsFromGridSquares(seasideSquares);

        for (auto&& landPolygon : landPolygons)
        {
            auto inflatedLand = PolygonUtils::inflatePolygon(landPolygon, 1.0f).front();
            auto shorelinePolygons = PolygonUtils::intersectPolygons(inflatedLand, seasidePolygons);
            shorelinePolygons << landPolygon;
            landPolygon = PolygonUtils::mergePolygons(shorelinePolygons).front();
        }
        landPolygons = PolygonUtils::mergePolygons(landPolygons);

        std::vector<QSharedPointer<DLandmassMarker>> initLandmasses;

        for (auto&& landPolygon : landPolygons)
        {
            auto&& [newShorelines, isCoast] = ShorelineUtils::findShorelinesAlongLandmass(landPolygon, seasidePolygons);
            ShorelineUtils::reduceDistanceBetweenPoints(&newShorelines, 250.0f, isCoast);

            std::vector<QSharedPointer<DShorelineMarker>> shorelineMarkers;
            for (auto&& shoreline : newShorelines)
                shorelineMarkers << spawn<DShorelineMarker>(shoreline, !isCoast);

            auto&& initLandmass = spawn<DLandmassMarker>(shorelineMarkers, std::vector<QSharedPointer<DShorelineMarker>>());
            initLandmass->forEachShoreline([&](auto& s, bool isInner) { s->setLandmass(initLandmass); });

            initLandmasses << initLandmass;
        }

        return initLandmasses;
    }

    void StageTools<EGenerationStage::Landmasses>::spawnLandmass(QMouseEvent* event)
    {
        auto sq = OmnigenCameraMgr::get()->findGridPoint(event->x(), event->y());

        if (!sq || !seasideSquareMap.contains(*sq))
            return;

        auto&& seasideData = seasideSquareMap[*sq];

        auto illegalInsidePoints = seasideData->shorelineSeaside;

        for (auto&& sq : seasideData->shorelineSeaside)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    illegalInsidePoints += GPoint(sq.x + x, sq.z + z);

        auto selectableSquares = seasideData->emptySeaside - illegalInsidePoints;

        if (!selectableSquares.contains(*sq))
            return;

        std::unordered_map<qint64, DLandmassMarkerData> changeData;

        auto&& spawnSizeRange = PShorelineSpawnSizeRange[spawnSize];
        int size = std::uniform_int_distribution<int>(spawnSizeRange.first, spawnSizeRange.second)(Generation::gRandomEngine);
        auto&& landmass = DShorelineMarker::generateLandmassAtSquare(seasideData->emptySeaside, illegalInsidePoints, *sq, spawnComplexity, size);
        saveLandmassData(&changeData, ELandmassEditType::Create, landmass);

        finishEditing(changeData);
    }

    void StageTools<EGenerationStage::Landmasses>::editCliffsByBrush()
    {
        if (!brushPointForCliffEditing)
            return;
        auto point = *brushPointForCliffEditing;

        int brushS = brushSize * cliffBrushSizeFactor;

        if (auto foundResults = findShorelinesInRange(point, brushS); !foundResults.empty())
        {
            auto&& [shoreline, idx] = foundResults.begin()->second;
            auto&& pts = shoreline->getControlPoints();
            auto&& cPts = asCircular(pts);

            float strength = cliffStrength * cliffStrengthFactor * (isKeyDown(VK_SHIFT) ? -1.0f : 1.0f);
            auto&& idxs = findIndexesToEdit(shoreline, idx, point, brushS);
            auto&& baseHeight = calculateBaseHeight(shoreline, idxs, strength);

            std::vector<float> newHeights;

            if (isCliffEditIncrement)
                newHeights = calculateCliffHeightsAfterIncrement(shoreline, idxs, baseHeight, strength);
            else if (isCliffEditFlattening)
                newHeights = calculateCliffHeightsAfterFlattening(shoreline, idxs, baseHeight, strength);

            if (!newHeights.empty())
                shoreline->setHeights(newHeights);
        }
    }

    std::vector<float> StageTools<EGenerationStage::Landmasses>::calculateCliffHeightsAfterIncrement(const QSharedPointer<DShorelineMarker>& shoreline, const std::set<int>& idxs, float baseHeight, float increment)
    {
        auto&& pts = shoreline->getControlPoints();

        std::vector<float> newHeights;
        for (auto&& pt : pts)
            newHeights << pt.y();

        bool changed = false;
        for (int i = 0; i < pts.size(); i++)
            if (idxs.contains(i))
            {
                float heightDiff = pts[i].y() - baseHeight;

                auto&& sq = ((GVector2D)pts[std::clamp(i, 1, (int)pts.size() - 2)]).toGPoint();
                auto&& domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
                auto&& terrainData = domain->getData<EDomainType::Terrain>();

                float newHeight = newHeights[i] + increment;
                // try to flatten while using increment
                if (isCliffEditIncrementFlattening)
                {
                    if ((increment >= 0 && heightDiff > increment) || increment <= 0 && heightDiff < increment)
                        continue;

                    if (std::abs(heightDiff) <= std::abs(increment))
                        newHeight = baseHeight + increment;
                    else
                        newHeight = newHeights[i] + increment * 2.0f;
                }

                newHeight = std::max(1.0f, newHeight);
                newHeight = std::min(newHeight, terrainData->maxHeight * 0.95f);

                changed = true;
                newHeights[i] = newHeight;
            }

        return (changed ? newHeights : std::vector<float>{});
    }

    std::vector<float> StageTools<EGenerationStage::Landmasses>::calculateCliffHeightsAfterFlattening(const QSharedPointer<DShorelineMarker>& shoreline, const std::set<int>& idxs, float baseHeight, float strength)
    {
        auto&& pts = shoreline->getControlPoints();

        std::vector<float> newHeights;
        for (auto&& pt : pts)
            newHeights << pt.y();

        bool changed = false;
        for (int i = 0; i < pts.size(); i++)
            if (idxs.contains(i))
            {
                float heightDiff = pts[i].y() - baseHeight;

                auto&& sq = ((GVector2D)pts[std::clamp(i, 1, (int)pts.size() - 2)]).toGPoint();
                auto&& domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
                auto&& terrainData = domain->getData<EDomainType::Terrain>();

                float newHeight = baseHeight;
                if (std::abs(heightDiff) > std::abs(strength))
                    newHeight = newHeights[i] + (std::abs(strength) * std::sgn(-heightDiff));
                
                newHeight = std::max(1.0f, newHeight);
                newHeight = std::min(newHeight, terrainData->maxHeight * 0.95f);

                changed = true;
                newHeights[i] = newHeight;
            }

        return (changed ? newHeights : std::vector<float>{});
    }

    float StageTools<EGenerationStage::Landmasses>::calculateBaseHeight(const QSharedPointer<DShorelineMarker>& shoreline, const std::set<int> affectedIndexes, float increment)
    {
        auto&& pts = shoreline->getControlPoints();

        std::vector<std::tuple<float, int>> shorelineHeights;
        for (auto&& i : affectedIndexes)
        {
            auto oldHeight = std::find_if(shorelineHeights.begin(), shorelineHeights.end(), [&](auto& t) { return std::abs(std::get<float>(t) - pts[i].y()) < std::abs(increment) * 2.0f; });

            if (oldHeight != shorelineHeights.end())
            {
                auto&& [height, count] = *oldHeight;
                count++;
            }
            else
                shorelineHeights.push_back({ pts[i].y(), 1 });
        }

        return std::get<float>(*std::max_element(shorelineHeights.begin(), shorelineHeights.end(), [](auto& t1, auto& t2) { return std::get<int>(t1) < std::get<int>(t2); }));
    }

    std::set<int> StageTools<EGenerationStage::Landmasses>::findIndexesToEdit(const QSharedPointer<DShorelineMarker>& shoreline, int closestIdx, const GVector2D& brushPoint, float brushSize)
    {
        std::set<int> idxs{ closestIdx };

        auto&& pts = shoreline->getControlPoints();
        auto&& cPts = asCircular(pts);

        auto findProperIdxs = [&](int dir)
        {
            std::vector<int> potentialIdxs;
            float distFromLastIdx = 0;
            int next_idx = closestIdx;
            while (true)
            {
                int cur_idx = next_idx;
                next_idx = cPts.findIdx(next_idx, dir);

                if (idxs.contains(next_idx) || distFromLastIdx > 3000.0f)
                    break;

                if (brushPoint.dist(pts[next_idx]) <= brushSize)
                {
                    idxs.insert(next_idx);

                    if (distFromLastIdx > 0.0f)
                    {
                        idxs.insert(potentialIdxs.begin(), potentialIdxs.end());
                        distFromLastIdx = 0.0f;
                        potentialIdxs.clear();
                    }
                }
                else
                {
                    potentialIdxs << next_idx;
                    distFromLastIdx += pts[cur_idx].distanceToPoint(pts[next_idx]);
                }
            }
        };
        findProperIdxs(1);
        findProperIdxs(-1);

        return idxs;
    }

    void StageTools<EGenerationStage::Landmasses>::updateLandmassBrushPolygons(QMouseEvent* event)
    {
        auto point = OmnigenCameraMgr::get()->findPointInWorld(60, event->x(), event->y());

        if (!point)
            return;

        // Check if there is any brush movement
        if (!oldBrushPos)
        {
            oldBrushPos = point;
            return;
        }

        QSet<GPoint> allSeasides;
        for (auto&& seasideData : allSeasideData)
            allSeasides += seasideData->allSeaside;
        auto&& allSeasidesPolygons = PolygonUtils::calculatePolygonsFromGridSquares(allSeasides);

        int brushS = brushSize * brushSizeFactor;
        auto brushPts = getCirclePoints(*point, 12, brushS);
        auto squarifiedBrush = squarifyPath(brushPts, 250.f);
        storedBrushPolygons << squarifiedBrush;
        storedBrushPolygons = PolygonUtils::intersectPolygons(storedBrushPolygons, allSeasidesPolygons);
        storedBrushPolygons = PolygonUtils::mergePolygons(storedBrushPolygons);

        for (auto it = storedBrushPolygons.begin(); it != storedBrushPolygons.end(); it++)
            for (auto it2 = storedBrushPolygons.begin(); it2 != storedBrushPolygons.end(); it2++)
                if (it != it2 && PolygonUtils::contains(it->front(), *it2))
                {
                    *it = {};
                    break;
                }
        storedBrushPolygons.erase(std::remove_if(storedBrushPolygons.begin(), storedBrushPolygons.end(), [](auto& p) { return p.empty(); }), storedBrushPolygons.end());

        for (auto&& marker : storedBrushPolygonsMarkers)
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(marker->getGuid());
        storedBrushPolygonsMarkers.clear();
        for (auto&& brushPolygon : storedBrushPolygons)
            storedBrushPolygonsMarkers << spawn<DLineMarker, true>(brushPolygon, QVector4D(0.2, 1, 0.2, 1.0), true, 200);
    }


    void StageTools<EGenerationStage::Landmasses>::updateLandmassesWithBrushPolygons()
    {
        if (storedBrushPolygons.empty())
            return;

        auto&& brushPolygons = storedBrushPolygons;

        // Edit existing landmass, search range slightly boosted for close brush to shoreline distance cases
        if (auto&& results = findOverlapingLandmasses(brushPolygons); !results.empty())
        {
            std::vector<QSharedPointer<DLandmassMarker>> landmassesToEdit;
            std::vector<QSharedPointer<DShorelineMarker>> shorelinesToEdit;
            std::vector<QSharedPointer<DShorelineMarker>> innerShorelinesToEdit;

            for (auto&& [landmass, shorelines, innerShorelines] : results)
            {
                landmassesToEdit << landmass;
                shorelinesToEdit << shorelines;
                innerShorelinesToEdit << innerShorelines;
            }

            // Shorelines of affected landmass that shall not be changed
            std::vector<QSharedPointer<DShorelineMarker>> shorelinesToReapply;
            std::vector<QSharedPointer<DShorelineMarker>> innerShorelinesToReapply;

            for(auto&& landmassToEdit : landmassesToEdit)
                landmassToEdit->forEachShoreline([&](auto& s, bool inner)
                    {
                        if (!inner)
                        {
                            if (std::find(shorelinesToEdit.begin(), shorelinesToEdit.end(), s) == shorelinesToEdit.end())
                                shorelinesToReapply << s;
                        }
                        else if (std::find(innerShorelinesToEdit.begin(), innerShorelinesToEdit.end(), s) == innerShorelinesToEdit.end())
                            innerShorelinesToReapply << s;
                    });

            // Find all seasides within edited landmasses range
            std::vector<QSharedPointer<SeasideData>> relevantSeasideData;

            for (auto&& landmassToEdit : landmassesToEdit)
            {
                auto&& seasideSquaresSets = ShorelineUtils::splitSeparateSet(landmassToEdit->findSeasideDomainSquares());
                for (auto&& seasideSquares : seasideSquaresSets)
                    if (auto&& seasideData = seasideSquareMap[*seasideSquares.begin()]; std::find(relevantSeasideData.begin(), relevantSeasideData.end(), seasideData) == relevantSeasideData.end())
                        relevantSeasideData << seasideData;

                landmassToEdit->forEachShoreline([&](auto& s, bool inner)
                    {
                        if (auto&& seasideData = seasideSquareMap[*s->getSquares().begin()]; std::find(relevantSeasideData.begin(), relevantSeasideData.end(), seasideData) == relevantSeasideData.end())
                            relevantSeasideData << seasideData;
                    });
            }

            QSet<GPoint> avalaibleSeaside;
            for (auto&& seasideData : relevantSeasideData)
                avalaibleSeaside += seasideData->allSeaside;
            auto allSeasidePolygons = PolygonUtils::calculatePolygonsFromGridSquares(avalaibleSeaside);

            float minShorelinePointsDistance = 0;
            for (auto&& landmassToEdit : landmassesToEdit)
                landmassToEdit->forEachShoreline([&](auto& s, bool inner) { minShorelinePointsDistance = std::max(minShorelinePointsDistance, s->getSegmentWidth()); });
            minShorelinePointsDistance = std::clamp(minShorelinePointsDistance, minMaxShorelinePointDist.first, minMaxShorelinePointDist.second);

            // Get landmasses polygons after using brush + eleminate too small landmasses
            auto newLandmasses = useBrushOnLandmasses(landmassesToEdit, innerShorelinesToEdit, brushPolygons, avalaibleSeaside);
            newLandmasses.erase(std::remove_if(newLandmasses.begin(), newLandmasses.end(), [](auto& lm) { return !lm.isMandatory && lm.area < 500000; }), newLandmasses.end());
            auto landmassMatches = matchLandmasses(landmassesToEdit, newLandmasses);

            std::vector<QSharedPointer<DLandmassMarker>> notMatchedLandmasses = landmassesToEdit;

            // Delete all found landmasses
            if (newLandmasses.empty())
            {
                for (auto&& landmass : landmassesToEdit)
                {
                    saveEditLandmassData(ELandmassEditType::Delete, landmass);
                    landmass->forEachShoreline([&](auto& s, bool inner)
                        {
                            Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(s->getGuid());
                        });

                    Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
                }
            }
            // Delete not matched landmasses
            else
            {
                for (int i = 0; i < landmassMatches.size(); i++)
                    if (landmassMatches[i] >= 0)
                        notMatchedLandmasses.erase(std::find(notMatchedLandmasses.begin(), notMatchedLandmasses.end(), landmassesToEdit[landmassMatches[i]]));

                for (auto&& landmass : notMatchedLandmasses)
                {
                    saveEditLandmassData(ELandmassEditType::Delete, landmass);
                    landmass->forEachShoreline([](auto& s, bool inner) { s->setLandmass(nullptr); });
                    Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
                }
            }
            
            // Not found shorelines, to be deleted later
            std::vector<QSharedPointer<DShorelineMarker>> unattachedShorelines;
            unattachedShorelines << shorelinesToReapply << innerShorelinesToReapply;
            for (auto&& landmass : notMatchedLandmasses)
                landmass->forEachShoreline([&](auto& s, bool isInner) 
                    {
                        if (std::find(unattachedShorelines.begin(), unattachedShorelines.end(), s) == unattachedShorelines.end())
                            unattachedShorelines << s;
                    });

            std::set<QSharedPointer<DShorelineMarker>> reattachedShorelines;

            tbb::spin_mutex attachedShorelinesGuard;
            tbb::parallel_for(0, int(newLandmasses.size()), [&](int landmass_idx)
                {
                    auto&& newLandmass = newLandmasses[landmass_idx];

                    std::vector<QSharedPointer<DShorelineMarker>> landmassUnattachedShorelines;
                    std::set<QSharedPointer<DShorelineMarker>> landmassReattachedShorelines;

                    std::vector<std::vector<QVector3D>> newInnerShorelines;
                    for (auto&& [landmassInnerPolygon, area] : newLandmass.innerPolygons)
                        if (area > 500000)
                            newInnerShorelines << landmassInnerPolygon;

                    // gather and apply changes for existing inner shorelines
                    ShorelineUtils::reduceDistanceBetweenPoints(&newInnerShorelines, minShorelinePointsDistance, false, 1000.0f);
                    auto [newInnerShorelineMarkers, notFoundInnerShorelineMarkers] = applyNewShorelines(innerShorelinesToEdit, innerShorelinesToReapply, &newInnerShorelines, false, newLandmass.mainPolygon, avalaibleSeaside);

                    for (auto&& newShorelineMarker : newInnerShorelineMarkers)
                        landmassReattachedShorelines.insert(newShorelineMarker);

                    for (auto&& notFoundShorelineMarker : notFoundInnerShorelineMarkers)
                        if (std::find(innerShorelinesToReapply.begin(), innerShorelinesToReapply.end(), notFoundShorelineMarker) == innerShorelinesToReapply.end())
                            landmassUnattachedShorelines << notFoundShorelineMarker;

                    // recognize shoreline paths in new landmass polygon
                    auto [newShorelines, isCoast] = ShorelineUtils::findShorelinesAlongLandmass(newLandmass.mainPolygon, allSeasidePolygons);
                    ShorelineUtils::reduceDistanceBetweenPoints(&newShorelines, minShorelinePointsDistance, isCoast, 1000.0f);

                    // Edit landmassToEdit
                    if (landmassMatches[landmass_idx] >= 0)
                    {
                        auto&& landmassToEdit = landmassesToEdit[landmassMatches[landmass_idx]];
                        saveEditLandmassData(ELandmassEditType::Change, landmassToEdit);

                        // gather and apply changes for existing shorelines
                        auto [newShorelineMarkers, notFoundShorelineMarkers] = applyNewShorelines(shorelinesToEdit, shorelinesToReapply, &newShorelines, isCoast, newLandmass.mainPolygon, avalaibleSeaside);

                        for (auto&& newShorelineMarker : newShorelineMarkers)
                            landmassReattachedShorelines.insert(newShorelineMarker);

                        for (auto&& notFoundShorelineMarker : notFoundShorelineMarkers)
                            if (std::find(shorelinesToReapply.begin(), shorelinesToReapply.end(), notFoundShorelineMarker) == shorelinesToReapply.end())
                                landmassUnattachedShorelines << notFoundShorelineMarker;

                        // Some of new shorelines might duplicate shorelines To Reapply
                        for (auto&& shorelineToReapply : shorelinesToReapply)
                        {
                            std::set<GVector2D> shorelineToReapplyPointsSet(shorelineToReapply->getControlPoints().begin(), shorelineToReapply->getControlPoints().end());
                            for (auto it = newShorelines.begin(); it != newShorelines.end(); it++)
                                if (areComparable(shorelineToReapplyPointsSet, *it, 5))
                                {
                                    newShorelines.erase(it);
                                    break;
                                }
                        }

                        for (auto&& newShoreline : newShorelines)
                            newShorelineMarkers << spawn<DShorelineMarker>(newShoreline, !isCoast);

                        for (auto&& newShoreline : newInnerShorelines)
                            newInnerShorelineMarkers << spawn<DShorelineMarker>(newShoreline, false);

                        for (auto&& newShorelineMarker : newShorelineMarkers)
                        {
                            landmassToEdit->addShoreline(newShorelineMarker);
                            newShorelineMarker->setLandmass(landmassToEdit);
                        }

                        for (auto&& newShorelineMarker : newInnerShorelineMarkers)
                        {
                            landmassToEdit->addInnerSeaShoreline(newShorelineMarker);
                            newShorelineMarker->setLandmass(landmassToEdit);
                        }

                        for (auto&& oldShorelines : notFoundShorelineMarkers)
                            landmassToEdit->removeShoreline(oldShorelines);

                        for (auto&& oldShorelines : notFoundInnerShorelineMarkers)
                            landmassToEdit->removeShoreline(oldShorelines);

                        if (isLandmassEditExtract)
                            deleteInnerSeasInsideInnerSeas(landmassToEdit);

                        if (isLandmassEditAppend)
                        {
                            landmassToEdit->recalculateLandmassPolygons(false);
                            deleteLandmassesInsideLandmass(landmassToEdit);
                        }

                        landmassToEdit->recalculateLandmassPolygons();
                        QOmnigenViewport::updateDrawable(landmassToEdit);
                        landmassToEdit->setLocked(true);
                    }
                    // New landmass
                    else
                    {
                        // treat all shorelines as new
                        std::vector<QSharedPointer<DShorelineMarker>> newShorelineMarkers;

                        for (auto&& newShoreline : newShorelines)
                            newShorelineMarkers << spawn<DShorelineMarker>(newShoreline, !isCoast);

                        for (auto&& newShoreline : newInnerShorelines)
                            newInnerShorelineMarkers << spawn<DShorelineMarker>(newShoreline, false);

                        auto&& newLandmass = spawn<DLandmassMarker>(newShorelineMarkers, newInnerShorelineMarkers);
                        newLandmass->setLocked(true);
                        saveEditLandmassData(ELandmassEditType::Create, newLandmass);

                        for (auto&& newShorelineMarker : newShorelineMarkers)
                            newShorelineMarker->setLandmass(newLandmass);

                        for (auto&& newShorelineMarker : newInnerShorelineMarkers)
                            newShorelineMarker->setLandmass(newLandmass);
                    }

                    {
                        std::scoped_lock lock(attachedShorelinesGuard);
                        unattachedShorelines << landmassUnattachedShorelines;
                        reattachedShorelines.insert(landmassReattachedShorelines.begin(), landmassReattachedShorelines.end());
                    }
                });

            for (auto&& unattachedShoreline : unattachedShorelines)
                if (!reattachedShorelines.contains(unattachedShoreline))
                {
                    if (auto&& landmass = unattachedShoreline->getLandmass().lock(); landmass)
                        landmass->removeShoreline(unattachedShoreline);
                    Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(unattachedShoreline->getGuid());
                }
        }
        else if (isLandmassEditAppend)
        // Spawn new landmass
        {
            std::vector<QSharedPointer<SeasideData>> seasideDatas;
            for(auto&& brushPoly : brushPolygons)
                for(auto&& pt : brushPoly)
                    if (auto&& seasideData = seasideSquareMap[((GVector2D)pt).toGPoint()]; seasideData && std::find(seasideDatas.begin(), seasideDatas.end(), seasideData) == seasideDatas.end())
                        seasideDatas << seasideData;

            QSet<GPoint> avaliableSeaside;
            for (auto&& seasideData : seasideDatas)
                avaliableSeaside += seasideData->allSeaside;
            auto allSeasidePolygons = PolygonUtils::calculatePolygonsFromGridSquares(avaliableSeaside);

            auto [brushPolygonsWithoutBorder, _] = cutPolygonByBorders(brushPolygons, {}, avaliableSeaside);
                
            for (auto&& polygon : brushPolygonsWithoutBorder)
            {
                auto [newShorelines, isCoast] = ShorelineUtils::findShorelinesAlongLandmass(polygon, allSeasidePolygons);

                std::vector<QSharedPointer<DShorelineMarker>> newShorelineMarkers;
                std::vector<QSharedPointer<DShorelineMarker>> newInnerSeaShorelineMarkers;

                for(auto&& newShoreline : newShorelines)
                    newShorelineMarkers << spawn<DShorelineMarker>(newShoreline, !isCoast);

                auto&& newLandmass = spawn<DLandmassMarker>(newShorelineMarkers, newInnerSeaShorelineMarkers);
                newLandmass->setLocked(true);

                saveEditLandmassData(ELandmassEditType::Create, newLandmass);

                for (auto&& newShorelineMarker : newShorelineMarkers)
                    newShorelineMarker->setLandmass(newLandmass);

                for (auto&& newShorelineMarker : newInnerSeaShorelineMarkers)
                    newShorelineMarker->setLandmass(newLandmass);
            }
        }

        storedBrushPolygons.clear();
        for (auto&& marker : storedBrushPolygonsMarkers)
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(marker->getGuid());
        storedBrushPolygonsMarkers.clear();
    }

    std::pair<std::vector<QSharedPointer<DShorelineMarker>>, std::vector<QSharedPointer<DShorelineMarker>>> StageTools<EGenerationStage::Landmasses>::applyNewShorelines(const std::vector<QSharedPointer<DShorelineMarker>>& shorelinesToEdit, const std::vector<QSharedPointer<DShorelineMarker>>& shorelinesToReapply, std::vector<std::vector<QVector3D>>* newShorelines, bool isCoast, const std::vector<QVector3D>& landmass, const QSet<GPoint>& relevantSeaside)
    {
        std::vector<QSharedPointer<DShorelineMarker>> newShorelineMarkers;
        std::vector<QSharedPointer<DShorelineMarker>> notFoundShorelineMarkers;

        for (auto&& reapplyShoreline : shorelinesToReapply)
            if (PolygonUtils::contains(reapplyShoreline->getControlPoints().front(), landmass))
                newShorelineMarkers << reapplyShoreline;
            else
                notFoundShorelineMarkers << reapplyShoreline;

        std::vector<QSharedPointer<DShorelineMarker>> oldInnerShorelines;
        for (auto&& shoreline : shorelinesToEdit)
            if (relevantSeaside.contains(*shoreline->getSquares().begin()))
                oldInnerShorelines << shoreline;

        for (auto&& oldShoreline : oldInnerShorelines)
        {
            std::set<GVector2D> oldShorelinePointsSet(oldShoreline->getControlPoints().begin(), oldShoreline->getControlPoints().end());
            auto newInnerShoreline_it = std::find_if(newShorelines->begin(), newShorelines->end(), [&](auto& s) { return areComparable(oldShorelinePointsSet, s, 5); });

            if (newInnerShoreline_it != newShorelines->end())
            {
                if (auto&& landmass = oldShoreline->getLandmass().lock(); landmass)
                    saveEditLandmassData(ELandmassEditType::Change, landmass);
                oldShoreline->setPoints(*newInnerShoreline_it, !isCoast);
                QOmnigenViewport::updateDrawable(oldShoreline);
                newShorelineMarkers << oldShoreline;
                newShorelines->erase(newInnerShoreline_it);
            }
            else
            {
                notFoundShorelineMarkers << oldShoreline;
            }
        }

        return { newShorelineMarkers, notFoundShorelineMarkers };
    }

    void StageTools<EGenerationStage::Landmasses>::deleteLandmassesInsideLandmass(const QSharedPointer<DLandmassMarker>& landmassToEdit)
    {
        auto landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();

        for (auto&& landmass : landmasses)
            if (landmass != landmassToEdit)
                if (auto&& pt = landmass->getMainPolygon().front(); PolygonUtils::contains(pt, landmassToEdit->getMainPolygon()))
                {
                    if (!landmass->findTerrainDomainSquares().empty())
                    {
                        auto&& innerSeas = landmassToEdit->getInnerSeaShorelines();
                        auto&& innerSeaToDelete = std::find_if(innerSeas.begin(), innerSeas.end(), [&](auto& s) { return PolygonUtils::contains(pt, s->getControlPoints()); });

                        if (innerSeaToDelete != innerSeas.end())
                        {
                            auto&& innerSeaShoreline = *innerSeaToDelete;
                            saveEditLandmassData(ELandmassEditType::Change, landmassToEdit);
                            landmassToEdit->removeShoreline(innerSeaShoreline);
                            Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(innerSeaShoreline->getGuid());
                            Generation::Data::get()->removeMarkerFromInit(innerSeaShoreline);
                        }
                    }

                    saveEditLandmassData(ELandmassEditType::Delete, landmass);
                    landmass->forEachShoreline([&](auto& s, bool inner)
                        {
                            Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(s->getGuid());
                        });

                    Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
                }
    }

    void StageTools<EGenerationStage::Landmasses>::deleteInnerSeasInsideInnerSeas(const QSharedPointer<DLandmassMarker>& landmassToEdit)
    {
        auto innerSeas = landmassToEdit->getInnerSeaShorelines();

        for (auto&& innerSea1 : innerSeas)
            for (auto&& innerSea2 : innerSeas)
                if (innerSea1 != innerSea2)
                    if (PolygonUtils::contains(innerSea2->getControlPoints().front(), innerSea1->getControlPoints()))
                    {
                        landmassToEdit->removeShoreline(innerSea2);
                        // Marker can be initialized or not yet
                        Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(innerSea2->getGuid());
                        Generation::Data::get()->removeMarkerFromInit(innerSea2);
                    }
    }

    std::vector<int> StageTools<EGenerationStage::Landmasses>::matchLandmasses(const std::vector<QSharedPointer<DLandmassMarker>>& oldLandmasses, const std::vector<AfterEditLandmass>& newLandmasses)
    {
        std::vector<int> idxs(newLandmasses.size());

        std::vector<std::vector<QVector3D>> newLandmassPolygons;
        for (auto&& newLandmass : newLandmasses)
            newLandmassPolygons << newLandmass.mainPolygon;

        std::vector<std::vector<float>> landmassCoverages(oldLandmasses.size());

        for (int i = 0; i < oldLandmasses.size(); i++)
        {
            auto&& landmass = oldLandmasses[i];
            auto&& coverages = PolygonUtils::coverage(landmass->getMainPolygon(), newLandmassPolygons);

            landmassCoverages[i] = coverages;
        }

        std::vector<bool> usedIdxs(oldLandmasses.size());
        for (int i = 0; i < newLandmasses.size(); i++)
        {
            int bestOldLandmassIdx = -1;
            float bestCovarage = 0;

            for (int j = 0; j < oldLandmasses.size(); j++)
                if (!usedIdxs[j] && landmassCoverages[j][i] > bestCovarage)
                {
                    bestCovarage = landmassCoverages[j][i];
                    bestOldLandmassIdx = j;
                }

            if (bestCovarage > 0.1f)
            {
                usedIdxs[bestOldLandmassIdx] = true;
                idxs[i] = bestOldLandmassIdx;
            }
            else
                idxs[i] = -1;
        }

        return idxs;
    }

    std::vector<AfterEditLandmass> StageTools<EGenerationStage::Landmasses>::useBrushOnLandmasses(const std::vector<QSharedPointer<DLandmassMarker>>& landmasses, const std::vector<QSharedPointer<DShorelineMarker>>& innerShorelinesInRange, const std::vector<std::vector<QVector3D>>& brushPolygons, const QSet<GPoint>& avaliableSeaside)
    {
        QSet<GPoint> insideTerrainSquares;
        for (auto&& landmass : landmasses)
            insideTerrainSquares += landmass->findTerrainDomainSquares();

        auto [brushPolygonsWithoutBorder, insideLandPolygons] = cutPolygonByBorders(brushPolygons, insideTerrainSquares, avaliableSeaside);

        std::vector<std::vector<QVector3D>> innerShorelinesPolygons;
        for (auto&& shoreline : innerShorelinesInRange)
            innerShorelinesPolygons << shoreline->getControlPoints();

        std::vector<std::vector<QVector3D>> landmassPolygonsAfterBrush;
        std::vector<std::vector<QVector3D>> innerShorelinePolygonsAfterBrush;

        std::vector<std::vector<QVector3D>> mainPolygons;
        for (auto&& landmass : landmasses)
            mainPolygons << landmass->getMainPolygon();

        if (isLandmassEditAppend)
        {
            for (auto&& landmass : landmasses)
                brushPolygonsWithoutBorder.push_back(landmass->getMainPolygon());
            landmassPolygonsAfterBrush = PolygonUtils::mergePolygons(brushPolygonsWithoutBorder);

            if (!innerShorelinesPolygons.empty())
            {
                brushPolygonsWithoutBorder.erase(brushPolygonsWithoutBorder.end() - 1);
                innerShorelinePolygonsAfterBrush = PolygonUtils::cutPolygons(innerShorelinesPolygons, brushPolygonsWithoutBorder);
            }
        }
        else if (isLandmassEditExtract)
        {
            std::vector<std::vector<QVector3D>> mainPolygons;
            for (auto&& landmass : landmasses)
                mainPolygons << landmass->getMainPolygon();

            for(auto&& brushPolygon : brushPolygonsWithoutBorder)
                std::reverse(brushPolygon.begin(), brushPolygon.end());
            for (auto&& innerPolygon : innerShorelinesPolygons)
                if (!PolygonUtils::isCW(innerPolygon))
                    std::reverse(innerPolygon.begin(), innerPolygon.end());

            brushPolygonsWithoutBorder << innerShorelinesPolygons;
            landmassPolygonsAfterBrush = PolygonUtils::cutPolygons(mainPolygons, brushPolygonsWithoutBorder, true);
            landmassPolygonsAfterBrush << insideLandPolygons;
        }

        if (landmassPolygonsAfterBrush.empty())
            return {};

        auto&& terrainSquaresSets = ShorelineUtils::splitSeparateSet(insideTerrainSquares);

        std::vector<AfterEditLandmass> newLandmasses;

        // Treat all polygon results as new landmass
        for (auto&& polygon : landmassPolygonsAfterBrush)
        {
            AfterEditLandmass newLandmass;
            newLandmass.mainPolygon = polygon;
            newLandmass.area = PolygonUtils::calculateArea(polygon);
            // if contains square terrain
            newLandmass.isMandatory = std::any_of(terrainSquaresSets.begin(), terrainSquaresSets.end(), [&](auto& a) { return PolygonUtils::contains(a.begin()->midPoint(), polygon); });

            newLandmasses << newLandmass;
        }

        // Treat new landmasses which are inside other landmasses as inner seas
        for (auto it = newLandmasses.begin(); it != newLandmasses.end(); it++)
            for (auto it2 = newLandmasses.begin(); it2 != newLandmasses.end(); it2++)
                if (it != it2 && PolygonUtils::contains(it->mainPolygon.front(), it2->mainPolygon))
                {
                    if (!std::any_of(terrainSquaresSets.begin(), terrainSquaresSets.end(), [&](auto& a) { return PolygonUtils::contains(a.begin()->midPoint(), it->mainPolygon); }))
                        it2->innerPolygons.push_back({ it->mainPolygon, it->area });
                    it->mainPolygon = {};
                    break;
                }
        newLandmasses.erase(std::remove_if(newLandmasses.begin(), newLandmasses.end(), [](auto& lm) { return lm.mainPolygon.empty(); }), newLandmasses.end());

        // Assign inner sea polygons calculated from brush to landmasses
        for (auto&& polygon : innerShorelinePolygonsAfterBrush)
            for(auto&& newLandmass : newLandmasses)
                // check if landmass contains sea + if sea does not contains terrain square
                if (PolygonUtils::contains(polygon.front(), newLandmass.mainPolygon) &&
                    !std::any_of(terrainSquaresSets.begin(), terrainSquaresSets.end(), [&](auto& a) { return PolygonUtils::contains(a.begin()->midPoint(), polygon); }))
                {
                    std::reverse(polygon.begin(), polygon.end());
                    newLandmass.innerPolygons.push_back({polygon, PolygonUtils::calculateArea(polygon)});
                    break;
                }

        return newLandmasses;
    }

    std::tuple<std::vector<std::vector<QVector3D>>, std::vector<std::vector<QVector3D>>> StageTools<EGenerationStage::Landmasses>::cutPolygonByBorders(const std::vector<std::vector<QVector3D>>& polgonsyToCut, const QSet<GPoint>& ownLand, const QSet<GPoint>& avaliableSeaside)
    {
        QSet<GPoint> endOfMapBorder;
        QSet<GPoint> terrainBorder;
        QSet<GPoint> waterBorder;

        // Border near seaside should be inflated as it is implied that other landmass is there
        for (auto&& sq : avaliableSeaside)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    if (auto borderSq = GPoint(sq.x + x, sq.z + z); !avaliableSeaside.contains(borderSq))
                    {
                        if (Generation::Data::get()->getDomainAtSquare(borderSq, EDomainType::Terrain))
                        {
                            if (ownLand.contains(borderSq))
                                terrainBorder += borderSq;
                        }
                        else if (Generation::Data::get()->getDomainAtSquare(borderSq, EDomainType::Water))
                            waterBorder += borderSq;
                        else
                            endOfMapBorder += borderSq;
                    }

        // Splitting to avoid situation where border creats polygon around avaliable seaside
        auto addSplitBorder = [](const QSet<GPoint>& border)
        {
            static const GVector2D splitVec(0, 1);
            auto&& [border1, border2] = ShorelineUtils::splitSetByVector(border, splitVec);
            auto polygonBorder = PolygonUtils::calculatePolygonsFromGridSquares(border1);
            polygonBorder << PolygonUtils::calculatePolygonsFromGridSquares(border2);

            return polygonBorder;
        };

        auto borderPolygons = addSplitBorder(endOfMapBorder);

        auto waterBorderPolygons = addSplitBorder(waterBorder);
        for (auto&& poly : waterBorderPolygons)
            borderPolygons << PolygonUtils::inflatePolygon(poly, 1.0f).front();

        auto terrainBorderPolygons = PolygonUtils::calculatePolygonsFromGridSquares(terrainBorder);
        for (auto&& poly : terrainBorderPolygons)
            borderPolygons << PolygonUtils::inflatePolygon(poly, 1.0f).front();

        std::vector<std::vector<QVector3D>> brushPolygons = PolygonUtils::cutPolygons(polgonsyToCut, borderPolygons);
        std::vector<std::vector<QVector3D>> landInsideBrushPolygons;

        for (auto it = brushPolygons.begin(); it != brushPolygons.end(); it++)
            for (auto it2 = brushPolygons.begin(); it2 != brushPolygons.end(); it2++)
                if (it != it2 && PolygonUtils::containsAll(*it, *it2))
                {
                    landInsideBrushPolygons << *it;
                    *it = {};
                    break;
                }

        brushPolygons.erase(std::remove_if(brushPolygons.begin(), brushPolygons.end(), [&](auto& poly) { return poly.empty(); }), brushPolygons.end());

        // Remove if outside of avaliable seaside
        brushPolygons.erase(std::remove_if(brushPolygons.begin(), brushPolygons.end(), [&](auto& poly) {
            return std::none_of(poly.begin(), poly.end(), [&](auto&& pt) { return avaliableSeaside.contains(((GVector2D)pt).toGPoint()); });
            }), brushPolygons.end());

        return { brushPolygons, landInsideBrushPolygons };
    }

    void StageTools<EGenerationStage::Landmasses>::saveEditLandmassData(ELandmassEditType editType, const QSharedPointer<DLandmassMarker>& marker)
    {
        saveLandmassData(&editLandmassData, editType, marker);
    }

    void StageTools<EGenerationStage::Landmasses>::saveLandmassData(std::unordered_map<qint64, DLandmassMarkerData>* editData, ELandmassEditType editType, const QSharedPointer<DLandmassMarker>& marker)
    {
        if ((*editData).contains(marker->getGuid()))
        {
            auto&& [currentEditType, name,  shorelines, innerShorelines] = (*editData)[marker->getGuid()];

            if (editType == ELandmassEditType::Delete)
            {
                if (currentEditType == ELandmassEditType::Create)
                    (*editData).erase(marker->getGuid());
                else
                    currentEditType = editType;
            }
        }
        else
        {
            if (editType == ELandmassEditType::Create)
                (*editData)[marker->getGuid()] = { editType, {}, {}, {} };
            else
            {
                DShorelineMarkerData shorelineMarkerData;
                DShorelineMarkerData shorelineInnerMarkerData;

                marker->forEachShoreline([&](auto& s, bool isInner)
                    {
                        if (!isInner)
                            shorelineMarkerData.push_back({ s->getGuid(), s->getName(), s->getControlPoints(), s->isLoop() });
                        else
                            shorelineInnerMarkerData.push_back({ s->getGuid(), s->getName(), s->getControlPoints(), s->isLoop() });
                    });

                (*editData)[marker->getGuid()] = { editType, marker->getName(), shorelineMarkerData, shorelineInnerMarkerData };
            }
        }
    }

    bool StageTools<EGenerationStage::Landmasses>::areComparable(const std::set<GVector2D>& setOfpath1, const std::vector<QVector3D>& path2, int degreeOfComparison)
    {
        int num = 0;
        for (auto&& pt : path2)
            if (setOfpath1.contains(pt) && (++num) >= degreeOfComparison)
                return true;

        return false;
    }

    std::vector<QSharedPointer<SeasideData>> StageTools<EGenerationStage::Landmasses>::findEditedSeasideDatas(const std::unordered_map<qint64, DLandmassMarkerData>& landmassData)
    {
        std::vector<QSharedPointer<SeasideData>> seasideDatas;

        for (auto&& [landmassId, landmassData] : landmassData)
            if (auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(landmassId); landmass)
                landmass->forEachShoreline([&](auto& s, bool inner)
                    {
                        if (auto&& seasideData = seasideSquareMap.at(*s->getSquares().begin()); std::find(seasideDatas.begin(), seasideDatas.end(), seasideData) == seasideDatas.end())
                            seasideDatas << seasideData;
                    });
            else
            {
                auto&& [editType, name, shorelineData, innerShorelineData] = landmassData;

                for (auto&& [id, shorelineName, pts, isLoop] : shorelineData)
                    if (auto&& seasideData = seasideSquareMap.at(((GVector2D)pts[pts.size() * 0.5f]).toGPoint()); std::find(seasideDatas.begin(), seasideDatas.end(), seasideData) == seasideDatas.end())
                        seasideDatas << seasideData;

                for (auto&& [id, shorelineName, pts, isLoop] : innerShorelineData)
                    if (auto&& seasideData = seasideSquareMap.at(((GVector2D)pts[pts.size() * 0.5f]).toGPoint()); std::find(seasideDatas.begin(), seasideDatas.end(), seasideData) == seasideDatas.end())
                        seasideDatas << seasideData;
            }

        return seasideDatas;
    }

    bool StageTools<EGenerationStage::Landmasses>::finishEditing(const std::unordered_map<qint64, DLandmassMarkerData>& landmassChanges, bool requiresFinalize /*= true*/)
    {
        HISTORY_PUSH(finishEditing, {}, true);

        std::unordered_map<qint64, DLandmassMarkerData> oldLandmassData;
        std::unordered_map<qint64, DLandmassMarkerData> newLandmassData;

        if (!HISTORY_LOAD2(newLandmassData, oldLandmassData))
        {
            oldLandmassData = landmassChanges;

            for (auto&& [landmassId, data] : oldLandmassData)
            {
                auto&& [editType, name, shorelines, innerShorelines] = data;

                if (editType == ELandmassEditType::Delete)
                    newLandmassData[landmassId] = { editType, {}, {}, {} };
                else
                {
                    auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(landmassId);

                    DShorelineMarkerData shorelineMarkerData;
                    DShorelineMarkerData shorelineInnerMarkerData;

                    landmass->forEachShoreline([&](auto& s, bool isInner)
                        {
                            if (!isInner)
                                shorelineMarkerData.push_back({ s->getGuid(), s->getName(), s->getControlPoints(), s->isLoop() });
                            else
                                shorelineInnerMarkerData.push_back({ s->getGuid(), s->getName(), s->getControlPoints(), s->isLoop() });
                        });

                    newLandmassData[landmassId] = { editType, landmass->getName(), shorelineMarkerData , shorelineInnerMarkerData };
                }
            }

            HISTORY_SAVE2(newLandmassData, oldLandmassData);
        }
        // Redo
        else
        {
            std::set<qint64> allLeftoverShorelines;
            for (auto&& [landmassId, landmassData] : newLandmassData)
                if (std::get<ELandmassEditType>(landmassData) == ELandmassEditType::Change)
                {
                    auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(landmassId);
                    landmass->forEachShoreline([&](auto& s, bool isInner) { allLeftoverShorelines.insert(s->getGuid()); });
                }

            for (auto&& [landmassId, landmassData] : newLandmassData)
            {
                auto editType = std::get<ELandmassEditType>(landmassData);

                if (editType == ELandmassEditType::Delete)
                    finishEditing_Delete(landmassId);
                else if (editType == ELandmassEditType::Create)
                    finishEditing_Create(landmassId, landmassData, &allLeftoverShorelines);
                else if (editType == ELandmassEditType::Change)
                    finishEditing_Change(landmassId, landmassData, &allLeftoverShorelines);
            }

            for (auto&& shorelineId : allLeftoverShorelines)
                Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(shorelineId);

            requiresFinalize = true;
        }

        if (requiresFinalize)
        {
            std::vector<QSharedPointer<SeasideData>> seasideDatas = findEditedSeasideDatas(oldLandmassData);
            for (auto&& seasideData : findEditedSeasideDatas(newLandmassData))
                if (std::find(seasideDatas.begin(), seasideDatas.end(), seasideData) == seasideDatas.end())
                    seasideDatas << seasideData;

            finalizeLandmassChanges(seasideDatas);

            blockSignals(true);
            treeModel.clear();
            treeModel.loadLandmasses();
            blockSignals(false);
        }

        return true;
    }

    bool StageTools<EGenerationStage::Landmasses>::finishEditing_Undo(const std::unordered_map<qint64, DLandmassMarkerData>& landmassChanges, bool requiresFinalize /*= true*/)
    {
        HISTORY_POP();

        std::unordered_map<qint64, DLandmassMarkerData> oldLandmassData;
        std::unordered_map<qint64, DLandmassMarkerData> newLandmassData;
        HISTORY_LOAD2(newLandmassData, oldLandmassData);

        std::set<qint64> allLeftoverShorelines;
        for (auto&& [landmassId, landmassData] : oldLandmassData)
            if (std::get<ELandmassEditType>(landmassData) == ELandmassEditType::Change)
            {
                auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(landmassId);
                landmass->forEachShoreline([&](auto& s, bool isInner) { allLeftoverShorelines.insert(s->getGuid()); });
            }

        for (auto&& [landmassId, landmassData] : oldLandmassData)
        {
            auto editType = std::get<ELandmassEditType>(landmassData);

            if (editType == ELandmassEditType::Delete)
                finishEditing_Create(landmassId, landmassData, &allLeftoverShorelines);
            else if (editType == ELandmassEditType::Create)
                finishEditing_Delete(landmassId);
            else if (editType == ELandmassEditType::Change)
                finishEditing_Change(landmassId, landmassData, &allLeftoverShorelines);
        }

        for (auto&& shorelineId : allLeftoverShorelines)
            Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(shorelineId);

        std::vector<QSharedPointer<SeasideData>> seasideDatas = findEditedSeasideDatas(oldLandmassData);
        for (auto&& seasideData : findEditedSeasideDatas(newLandmassData))
            if (std::find(seasideDatas.begin(), seasideDatas.end(), seasideData) == seasideDatas.end())
                seasideDatas << seasideData;

        finalizeLandmassChanges(seasideDatas);

        blockSignals(true);
        treeModel.clear();
        treeModel.loadLandmasses();
        blockSignals(false);

        return true;
    }

    void StageTools<EGenerationStage::Landmasses>::finishEditing_Delete(qint64 landmassId)
    {
        auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(landmassId);

        landmass->forEachShoreline([&](auto& s, bool inner)
            {
                Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(s->getGuid());
            });
        Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
    }

    void StageTools<EGenerationStage::Landmasses>::finishEditing_Create(qint64 landmassId, const DLandmassMarkerData& landmassData, std::set<qint64>* allLeftoverShorelines)
    {
        auto&& [editType, name, shorelines, innerShorelines] = landmassData;

        std::vector<QSharedPointer<DShorelineMarker>> shorelineMarkers;
        std::vector<QSharedPointer<DShorelineMarker>> innerShorelineMarkers;

        for (auto&& [shorelineId, shorelineName, shorelinePts, shorelineIsLoop] : shorelines)
            if (auto&& shorelineMarker = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(shorelineId); shorelineMarker)
            {
                shorelineMarker->setPoints(shorelinePts, shorelineIsLoop);
                QOmnigenViewport::updateDrawable(shorelineMarker);
                allLeftoverShorelines->erase(shorelineId);
                shorelineMarkers << shorelineMarker;
            }
            else
            {
                shorelineMarker = spawn<DShorelineMarker>(shorelinePts, shorelineIsLoop);
                shorelineMarker->setGuid(shorelineId);
                shorelineMarker->setName(shorelineName);
                shorelineMarkers << shorelineMarker;
            }

        for (auto&& [shorelineId, shorelineName, shorelinePts, shorelineIsLoop] : innerShorelines)
            if (auto&& shorelineMarker = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(shorelineId); shorelineMarker)
            {
                shorelineMarker->setPoints(shorelinePts, shorelineIsLoop);
                QOmnigenViewport::updateDrawable(shorelineMarker);
                allLeftoverShorelines->erase(shorelineId);
                innerShorelineMarkers << shorelineMarker;
            }
            else
            {
                shorelineMarker = spawn<DShorelineMarker>(shorelinePts, shorelineIsLoop);
                shorelineMarker->setGuid(shorelineId);
                shorelineMarker->setName(shorelineName);
                innerShorelineMarkers << shorelineMarker;
            }

        auto&& landmass = spawn<DLandmassMarker>(shorelineMarkers, innerShorelineMarkers);
        landmass->setLocked(true);
        landmass->setGuid(landmassId);
        landmass->setName(name);
        landmass->forEachShoreline([&](auto& s, bool isInner) 
            {
                if (auto&& oldLandmass = s->getLandmass().lock(); oldLandmass)
                    oldLandmass->removeShoreline(s);

                s->setLandmass(landmass); 
            });
    }

    void StageTools<EGenerationStage::Landmasses>::finishEditing_Change(qint64 landmassId, const DLandmassMarkerData& landmassData, std::set<qint64>* allLeftoverShorelines)
    {
        auto&& [editType, name, shorelines, innerShorelines] = landmassData;

        auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(landmassId);

        std::vector<QSharedPointer<DShorelineMarker>> leftoutShorelines;
        landmass->forEachShoreline([&](auto& s, bool isInner) { leftoutShorelines << s; });

        for (auto&& [shorelineId, shorelineName, shorelinePts, shorelineIsLoop] : shorelines)
        {
            auto&& shorelineMarker = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(shorelineId);

            if (shorelineMarker)
            {
                shorelineMarker->setPoints(shorelinePts, shorelineIsLoop);
                QOmnigenViewport::updateDrawable(shorelineMarker);
                leftoutShorelines.erase(std::remove(leftoutShorelines.begin(), leftoutShorelines.end(), shorelineMarker), leftoutShorelines.end());
                allLeftoverShorelines->erase(shorelineId);
            }
            else
            {
                shorelineMarker = spawn<DShorelineMarker>(shorelinePts, shorelineIsLoop);
                shorelineMarker->setGuid(shorelineId);
                shorelineMarker->setName(shorelineName);
            }

            if (landmass->addShoreline(shorelineMarker))
            {
                if (auto&& oldLandmass = shorelineMarker->getLandmass().lock(); oldLandmass)
                    oldLandmass->removeShoreline(shorelineMarker);

                shorelineMarker->setLandmass(landmass);
            }
        }

        for (auto&& [shorelineId, shorelineName, shorelinePts, shorelineIsLoop] : innerShorelines)
        {
            auto&& shorelineMarker = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(shorelineId);

            if (shorelineMarker)
            {
                shorelineMarker->setPoints(shorelinePts, shorelineIsLoop);
                QOmnigenViewport::updateDrawable(shorelineMarker);
                leftoutShorelines.erase(std::remove(leftoutShorelines.begin(), leftoutShorelines.end(), shorelineMarker), leftoutShorelines.end());
                allLeftoverShorelines->erase(shorelineId);
            }
            else
            {
                shorelineMarker = spawn<DShorelineMarker>(shorelinePts, shorelineIsLoop);
                shorelineMarker->setGuid(shorelineId);
                shorelineMarker->setName(shorelineName);
            }

            if (landmass->addInnerSeaShoreline(shorelineMarker))
            {
                if (auto&& oldLandmass = shorelineMarker->getLandmass().lock(); oldLandmass)
                    oldLandmass->removeShoreline(shorelineMarker);

                shorelineMarker->setLandmass(landmass);
            }
        }

        for (auto&& shorelineMarker : leftoutShorelines)
            landmass->removeShoreline(shorelineMarker);

        landmass->recalculateLandmassPolygons();
        QOmnigenViewport::updateDrawable(landmass);
    }

    std::vector<QVector3D> StageTools<EGenerationStage::Landmasses>::squarifyPath(const std::vector<QVector3D>& path, float squareWidth)
    {
        QSet<std::pair<int, int>> squares;
        std::vector<std::pair<int, int>> squarePath;

        auto cPath = asCircular(path);
        for (int i = 0; i < path.size() - 1; i++)
        {
            auto&& p1 = path[i];
            auto&& p2 = path[i + 1];
            auto dist = p1.distanceToPoint(p2);
            auto discretPoints = dist / squareWidth;
            auto dir = (p2 - p1).normalized();

            auto sqPoint = p1;
            for (int j = 0; j <= discretPoints; j++)
            {
                std::pair<int, int> sq(sqPoint.x() / squareWidth, sqPoint.z() / squareWidth);
                if (!squares.contains(sq))
                    squarePath << sq;
                squares += sq;

                sqPoint = sqPoint + dir * squareWidth;
            }
        }

        std::vector<std::pair<int, int>> squarePathNoDiagonal;

        auto cSquarePath = asCircular(squarePath);
        for (int i = 0; i < squarePath.size(); i++)
        {
            auto&& currentSq = squarePath[i];
            auto&& nextSq = squarePath[cSquarePath.findIdx(i, 1)];

            squarePathNoDiagonal << currentSq;

            auto xDiff = nextSq.first - currentSq.first;
            auto zDiff = nextSq.second - currentSq.second;

            // diagonal
            if (xDiff != 0 && zDiff != 0)
            {
                if (auto sq = std::pair<int, int>(currentSq.first + xDiff, currentSq.second); !squares.contains(sq))
                    squarePathNoDiagonal << sq;
                else if (auto sq = std::pair<int, int>(currentSq.first, currentSq.second + zDiff); !squares.contains(sq))
                    squarePathNoDiagonal << sq;
            }
        }

        std::vector<QVector3D> squarifiedPath;

        for (auto&& sq : squarePathNoDiagonal)
            squarifiedPath << QVector3D(sq.first * squareWidth + squareWidth * 0.5f, 0, sq.second * squareWidth + squareWidth * 0.5f);

        return squarifiedPath;
    }

    void StageTools<EGenerationStage::Landmasses>::drawBrushCircle(QMouseEvent* mEvent, int circleDetail)
    {
        if (auto mPoint = OmnigenCameraMgr::get()->findPointInWorld(60, mEvent->x(), mEvent->y()); mPoint)
        {
            auto gPoint = GVector2D((*mPoint).x, (*mPoint).z).toGPoint();
            auto&& domain = Generation::Data::get()->getDomainAtSquare(gPoint, EDomainType::Terrain);
            int brushS = brushSize * (isCliffEditing ? cliffBrushSizeFactor : brushSizeFactor);

            std::vector<QVector3D> circlePoints;

            for (int i = 0; i <= circleDetail; i++)
            {
                auto angle = ((std::numbers::pi * 2) / circleDetail) * i;

                QVector3D newPoint = { (*mPoint).x + (brushS * std::cosf(angle)), 60.0f, (*mPoint).z + (brushS * std::sinf(angle)) };
                circlePoints.emplace_back(newPoint);
            }

            if (!brushMarker)
                brushMarker = Generation::Data::get()->createMarker<DLineMarker, true>(circlePoints, QVector4D(0.2, 1, 0.2, 0.6), true);
            else
                brushMarker->movePoints(circlePoints);
        }
    }

    std::vector<QVector3D> StageTools<EGenerationStage::Landmasses>::getCirclePoints(const QVector3D& circleCenter, int points, int radius)
    {
        std::vector<QVector3D> circlePoints;
        for (int i = 0; i <= points; i++)
        {
            auto angle = ((std::numbers::pi * 2) / points) * i;
            QVector3D newPoint = { circleCenter.x() + (radius * std::cosf(angle)), circleCenter.y() + 50.0f, circleCenter.z() + (radius * std::sinf(angle)) };
            circlePoints.emplace_back(newPoint);
        }

        return circlePoints;
    }

    void Design::StageTools<EGenerationStage::Landmasses>::changeTo3DShorelines()
    {
        auto&& shorelines = Generation::Data::get()->getMarkers<DShorelineMarker>();
        for (auto&& shoreline : shorelines)
            shoreline->showAs3D();
    }

    void Design::StageTools<EGenerationStage::Landmasses>::changeTo2DShorelines()
    {
        auto&& shorelines = Generation::Data::get()->getMarkers<DShorelineMarker>();
        for (auto&& shoreline : shorelines)
            shoreline->showAs2D();
    }

    void SeasideData::clear()
    {
        allSeaside.clear();
        emptySeaside.clear();
        landSeaside.clear();
        shorelineSeaside.clear();
        landmassSquareMap.clear();
        shorelineSquareMap.clear();
    }
}