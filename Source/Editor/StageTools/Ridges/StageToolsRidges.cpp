#include "stdafx.h"
#include "StageToolsRidges.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/Landmasses/LandmassMarker.h"
#include "Scene/Generation/Stages/Ridges/StageGeneration_Ridges.h"
#include "RidgesSelection.h"
#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Source/Scene/Generation/Common/Markers/LineMarker.h"
#include "Source/Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "../StageTools.h"
#include "../StageObjectNode.h"

#include <ranges>
#include <QApplication>

namespace Design
{
    StageTools<EGenerationStage::Ridges>::StageTools()
        : StageToolsBase()
    {
        setupActions();
    }

    SelectionMgrBase* StageTools<EGenerationStage::Ridges>::getSelectionMgr() const
    {
        return RidgesSelectionMgr::get();
    }

    void StageTools<EGenerationStage::Ridges>::bind()
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
        treeModel.loadRidges();

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, treeView });

        treeView->show();

        // Data events
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(&treeModel, &QRidgesTreeModel::addRidge);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Modified>(&treeModel, &QRidgesTreeModel::updateRidge);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(&treeModel, &QRidgesTreeModel::removeRidge);

        // Selection mgr
        auto* selMgr = RidgesSelectionMgr::get();
        qConnections << connect(selMgr, &SelectionMgrBase::selectionChanged, this, &StageTools<EGenerationStage::Ridges>::onSelectionChanged);

        connect(&ticker, &QTimer::timeout, this, &StageTools<EGenerationStage::Ridges>::editPointsHeight);
        ticker.setInterval(16);

        // Viewport events
        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void StageTools<EGenerationStage::Ridges>::unbind()
    {
        StageToolsBase::unbind();

        bIsRidgeEditing = false;
        bDrawingOperation = false;
        bIsRidgeDrawing = false;

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
    }

    void StageTools<EGenerationStage::Ridges>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();
        genData->saveMarkers<DRidgeMarker>(writer);

        writer << ridgeNodes;
    }

    void StageTools<EGenerationStage::Ridges>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();
        genData->loadMarkers<DRidgeMarker>(reader);

        reader >> ridgeNodes;
    }

    void StageTools<EGenerationStage::Ridges>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::Ridges>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::Ridges>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::Ridges>::modifyNode);
    }

    void StageTools<EGenerationStage::Ridges>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::Ridges>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::Ridges>::clearNodes()
    {
        ridgeNodes.clear();
    }

    void StageTools<EGenerationStage::Ridges>::cleanNodesState()
    {
        std::erase_if(ridgeNodes, [](auto& kv) { return !kv.second->getRidge(); });

        for (auto&& [ridgeGuid, ridgeNode] : ridgeNodes)
        {
            ridgeNode->setCreatedOnCurrentStage(false);
            ridgeNode->clearSnapshot();
        }
    }

    void StageTools<EGenerationStage::Ridges>::updateParentNodes()
    {
        auto&& layoutStageTools = getStageTools<EGenerationStage::Layout>();
        auto&& domainSquareNodes = layoutStageTools->getDomainSquareNodes();

        for (auto&& [square, squareNode] : domainSquareNodes)
            squareNode->clearRidgeNodes();

        for (auto&& [ridgeGuid, ridgeNode] : ridgeNodes)
        {
            if (!ridgeNode->getRidge())
                continue;

            auto&& ridge = ridgeNode->getRidge();

            for (auto&& sq : ridge->getSquares())
                domainSquareNodes.at(sq)->addRidgeNode(ridge);
        }
    }

    void StageTools<EGenerationStage::Ridges>::loadSnapshotData()
    {
        disconnectNodes();

        auto spawnRidgeMarker = [](const QSharedPointer<RidgeNode>& ridgeNode, qint64 ridgeGuid, const QSharedPointer<DRidgeMarker>& parent = nullptr)
        {
            auto&& snapshotRidge = ridgeNode->getSnapshot();
            auto&& newRidge = spawn<DRidgeMarker>(snapshotRidge->points, parent);

            newRidge->setGuid(ridgeGuid);
            newRidge->setName(snapshotRidge->name);
            newRidge->setSourcePointIdx(snapshotRidge->sourcePointIdx);
            newRidge->setSegmentWidth(snapshotRidge->segmentWidth);
            if (snapshotRidge->tablelandType)
                newRidge->setTablelandType(*snapshotRidge->tablelandType);
            newRidge->setRightSlopeFactor(snapshotRidge->slopeVariation.first);
            newRidge->setLeftSlopeFactor(snapshotRidge->slopeVariation.second);
            newRidge->setHeights(snapshotRidge->ridgelineHeight);
            newRidge->setSquares(snapshotRidge->squares);
            if (parent)
                parent->addChild(newRidge);

            ridgeNode->setRidge(newRidge);
        };

        // Add root ridges with children
        for (auto&& [ridgeGuid, ridgeNode] : ridgeNodes)
            if (auto&& snapshotRidge = ridgeNode->getSnapshot(); snapshotRidge && !ridgeNode->getRidge() && snapshotRidge->parentGuid < 0)
            {
                spawnRidgeMarker(ridgeNode, ridgeGuid);

                auto&& treeData = snapshotRidge->treeData;
                auto&& treeParents = snapshotRidge->treeParents;

                for (IndexType i = 1; i < snapshotRidge->treeData.size(); i++)
                {
                    spawnRidgeMarker(ridgeNodes[treeData[i]], treeData[i], ridgeNodes[treeData[treeParents[i]]]->getRidge());
                    ridgeNodes[treeData[i]]->clearSnapshot();
                }

                ridgeNode->clearSnapshot();
            }

        for (auto it = ridgeNodes.begin(); it != ridgeNodes.end();)
        {
            auto&& [ridgeGuid, ridgeNode] = *it;

            if (auto&& snapshotRidge = ridgeNode->getSnapshot())
            {
                // modify
                if (ridgeNode->getRidge())
                {
                    auto&& ridgeToUpdate = ridgeNode->getRidge();
                    ridgeToUpdate->setPoints(snapshotRidge->points);
                    ridgeToUpdate->setName(snapshotRidge->name);
                    ridgeToUpdate->setSourcePointIdx(snapshotRidge->sourcePointIdx);
                    ridgeToUpdate->setSegmentWidth(snapshotRidge->segmentWidth);
                    if (snapshotRidge->tablelandType)
                        ridgeToUpdate->setTablelandType(*snapshotRidge->tablelandType);
                    ridgeToUpdate->setRightSlopeFactor(snapshotRidge->slopeVariation.first);
                    ridgeToUpdate->setLeftSlopeFactor(snapshotRidge->slopeVariation.second);
                    ridgeToUpdate->setHeights(snapshotRidge->ridgelineHeight);
                    ridgeToUpdate->setSquares(snapshotRidge->squares);

                    if (snapshotRidge->parentGuid >= 0)
                    {
                        auto&& parentRidge = ridgeNodes[snapshotRidge->parentGuid]->getRidge();
                        auto&& parentChildren = parentRidge->getChildren();
                        ridgeToUpdate->setParent(parentRidge);
                        if (std::find(parentChildren.begin(), parentChildren.end(), ridgeToUpdate) == parentChildren.end())
                            parentRidge->addChild(ridgeToUpdate);
                    }

                    QOmnigenViewport::updateDrawable(ridgeToUpdate);
                }
                // add (only ridges which root were not removed)
                else
                {
                    spawnRidgeMarker(ridgeNode, ridgeGuid, ridgeNodes[snapshotRidge->parentGuid]->getRidge());
                }

                ridgeNode->clearSnapshot();
            }
            // remove
            else if (ridgeNode->isCreatedOnCurrentStage())
            {
                auto&& ridgeToDelete = ridgeNode->getRidge();

                if (!ridgeToDelete->getParent())
                    Generation::Data::get()->clearSingleExactMarker<DRidgeMarker>(ridgeGuid);
                else
                {
                    emit Editable::aboutToBeDeleted(ridgeToDelete);
                    ridgeToDelete->getParent().lock()->removeChild(ridgeToDelete);
                }

                it = ridgeNodes.erase(it);
                continue;
            }

            it++;
        }

        std::vector<QSharedPointer<DRidgeMarker>> ridgeRoots;
        for (auto&& [ridgeGuid, ridgeNode] : ridgeNodes)
            if (!ridgeNode->getRidge()->getParent())
                ridgeRoots << ridgeNode->getRidge();

        treeModel.clear();
        treeModel.loadRidges(ridgeRoots);
        Generation::Data::get()->initializeQueuedMarkers();
        connectNodes();
    }

    bool StageTools<EGenerationStage::Ridges>::validatePipeline()
    {
        for (auto&& [ridgeGuid, ridgeNode] : ridgeNodes)
        {
            // removed ridge
            if (!ridgeNode->getRidge())
                return false;
            // added ridge
            else if (ridgeNode->isCreatedOnCurrentStage())
                return false;
            // modified ridge
            else if (ridgeNode->getSnapshot())
                return false;
        }

        return true;
    }

    void StageTools<EGenerationStage::Ridges>::updatePipeline()
    {
        auto&& layoutStageTools = getStageTools<EGenerationStage::Layout>();
        auto&& isohypseStageTools = getStageTools<EGenerationStage::ContourLines>();
        auto&& domainSquareNodes = layoutStageTools->getDomainSquareNodes();
        auto&& isohypseNodes = isohypseStageTools->getIsohypseNodes();
        
        std::unordered_set<qint64> affectedRootRidges;
        std::unordered_set<qint64> isohypsesToInvalidate;

        for (auto&& [ridgeGuid, ridgeNode] : ridgeNodes)
        {
            // removed ridge
            if (!ridgeNode->getRidge())
            {
                qint64 rootGuid = ridgeGuid;

                while (true)
                {
                    if (!ridgeNodes[rootGuid]->getRidge())
                    {
                        if (ridgeNodes[rootGuid]->getSnapshot()->parentGuid != -1)
                            rootGuid = ridgeNodes[rootGuid]->getSnapshot()->parentGuid;
                        else
                            break;
                    }
                    else
                    {
                        rootGuid = ridgeNodes[rootGuid]->getRidge()->findRootParent()->getGuid();
                        break;
                    }
                }

                affectedRootRidges += rootGuid;
                auto&& rootRidgeNode = ridgeNodes[rootGuid];

                if (auto&& rootSnapshot = rootRidgeNode->getSnapshot())
                    for (auto&& ridgeGuid : rootSnapshot->treeData)
                        isohypsesToInvalidate += ridgeNodes[ridgeGuid]->getIsohypseNodes();

                if (auto&& rootRidge = rootRidgeNode->getRidge())
                    rootRidge->forEachChild([&](auto& r) { isohypsesToInvalidate += ridgeNodes[r->getGuid()]->getIsohypseNodes(); }, rootRidge);
            }
            // added ridge
            else if (ridgeNode->isCreatedOnCurrentStage())
            {
                qint64 rootGuid = ridgeGuid;

                while (true)
                {
                    if (!ridgeNodes[rootGuid]->getRidge())
                    {
                        if (ridgeNodes[rootGuid]->getSnapshot()->parentGuid != -1)
                            rootGuid = ridgeNodes[rootGuid]->getSnapshot()->parentGuid;
                        else
                            break;
                    }
                    else
                    {
                        rootGuid = ridgeNodes[rootGuid]->getRidge()->findRootParent()->getGuid();
                        break;
                    }
                }

                affectedRootRidges += rootGuid;
                auto&& rootRidgeNode = ridgeNodes[rootGuid];
                auto&& rootRidge = rootRidgeNode->getRidge();

                QSet<GPoint> ridgeTreeSquares;
                rootRidge->forEachChild([&](auto& ridge){ ridgeTreeSquares += ridge->getSquares();}, rootRidge);
                ridgeTreeSquares = GPoint::growMargin(ridgeTreeSquares);

                for (auto&& sq : ridgeTreeSquares)
                    if (domainSquareNodes.contains(sq))
                    {
                        auto&& isohypsesAtSquare = domainSquareNodes.at(sq)->getIsohypseNodes();
                        for (auto&& ih : isohypsesAtSquare)
                            if (auto&& ihlvl = isohypseNodes.at(ih)->getIsohypse()->getLevel(); ihlvl > 1)
                                isohypsesToInvalidate.emplace(ih);
                    }
            }
            // modified ridge
            else if (ridgeNode->getSnapshot())
            {
                if (auto&& rootRidge = ridgeNode->getRidge()->findRootParent(); !isohypsesToInvalidate.contains(rootRidge->getGuid()))
                {
                    affectedRootRidges += rootRidge->getGuid();
                    rootRidge->forEachChild([&](auto& r) { isohypsesToInvalidate += ridgeNodes[r->getGuid()]->getIsohypseNodes(); }, rootRidge);
                }
            }
        }

        // Check against other IH affected by merged modified root ridges
        std::unordered_set<qint64> ihMergeAffectedRootRidges;
        for (auto&& ihGuid : isohypsesToInvalidate)
            if (isohypseNodes.contains(ihGuid) && isohypseNodes.at(ihGuid)->getIsohypse())
                ihMergeAffectedRootRidges += isohypseNodes.at(ihGuid)->getIsohypse()->data.ridgeIds;
        
        for (auto&& ridgeGuid : ihMergeAffectedRootRidges)
            for (auto&& ihGuid : ridgeNodes[ridgeGuid]->getIsohypseNodes())
                if (!isohypsesToInvalidate.contains(ihGuid) && isohypseNodes.contains(ihGuid))
                    if (auto&& ih = isohypseNodes.at(ihGuid)->getIsohypse(); ih && !container_and(ih->data.affectedBy[EIHAffectType::Merge], affectedRootRidges).empty())
                        isohypsesToInvalidate += ihGuid;

        for (auto&& isohypseToInvalidate : isohypsesToInvalidate)
            if (isohypseNodes.contains(isohypseToInvalidate) && isohypseNodes.at(isohypseToInvalidate)->getIsohypse())
            {
                auto isohypse = isohypseNodes.at(isohypseToInvalidate)->getIsohypse();
                isohypseStageTools->removeNode(isohypse);

                auto&& sources = isohypse->data.sources;
                for (auto&& src : sources)
                    if(src.ih != nullptr && src.idx != -1 && src.ihGuid != -1 && src.ih->descendants.size() > 0)
                        src.ih->setDescendant(src.idx, IHSrcInfo());

                despawnBatched(isohypse);
            }

        isohypseStageTools->updatePipeline();
        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::Ridges>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        if (auto&& ridge = object.dynamicCast<DRidgeMarker>(); ridge && !ridgeNodes.contains(ridge->getGuid()))
            ridgeNodes[ridge->getGuid()] = QSharedPointer<RidgeNode>::create(ridge);
    }

    void StageTools<EGenerationStage::Ridges>::removeNode(QSharedPointer<Editable> object)
    {
        if (auto&& ridge = object.dynamicCast<DRidgeMarker>(); ridge && ridgeNodes.contains(ridge->getGuid()))
        {
            if (ridgeNodes[ridge->getGuid()]->isCreatedOnCurrentStage())
                ridgeNodes.erase(ridge->getGuid());
            else
            {
                if (!ridgeNodes[ridge->getGuid()]->getSnapshot())
                    ridgeNodes[ridge->getGuid()]->makeSnapshot();

                ridgeNodes[ridge->getGuid()]->nullifyLandmass();
            }
        }
    }

    void StageTools<EGenerationStage::Ridges>::modifyNode(QSharedPointer<Editable> object)
    {
        if (auto&& ridge = object.dynamicCast<DRidgeMarker>(); ridge && ridgeNodes.contains(ridge->getGuid()) && !ridgeNodes[ridge->getGuid()]->isCreatedOnCurrentStage() && !ridgeNodes[ridge->getGuid()]->getSnapshot())
            ridgeNodes[ridge->getGuid()]->makeSnapshot();
    }

    void StageTools<EGenerationStage::Ridges>::setupActions()
    {
        actions[ERidgeAction::DeleteSelectedRidge] = new QAction(QIcon(), "Delete Ridge", this);
        connect(actions[ERidgeAction::DeleteSelectedRidge], &QAction::triggered, this, [this]()
            {
                std::vector<qint64> guids;
                for (auto&& ridge : RidgesSelectionMgr::get()->getSelection<ERidgesSelection::Ridge>())
                    guids.emplace_back(ridge->getGuid());

                deleteMultipleRidges(guids);
            });
    }

    bool StageTools<EGenerationStage::Ridges>::eventFilter(QObject* obj, QEvent* event)
    {
        if (event->type() == QEvent::Leave)
            clearHighlights();

        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
                if (bIsRidgeEditing)
                {
                    brushWholeMovement = 0.0f;
                    oldBrushPos = OmnigenCameraMgr::get()->findPointInWorld(60, mEvent->x(), mEvent->y());
                }
                else if (bIsRidgeDrawing)
                {
                    startRidge(mEvent);
                }
                else if (bHeightEditing)
                {
                    // Constantly edit ridge height while holding LMB
                    if ((bSculptTool || bFlattenTool || bSmoothTool) && mEvent->button() == Qt::LeftButton)
                    {
                        if (auto&& result = findClosestRidgeline(mEvent); result)
                        {
                            brushOriginPoint = result->second;
                            qint64 ridgeId = result->first;

                            std::vector<QSharedPointer<DRidgeMarker>> ridges;
                            Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
                            auto&& ridgeIter = std::find_if(ridges.begin(), ridges.end(), [ridgeId](auto&& ele) {return ele->getGuid() == ridgeId; });

                            // Save ridgeline points before editing for undo/redo
                            oldRidgesData.emplace_back(result->first, (*ridgeIter)->getControlPoints(), (*ridgeIter)->getSourcePointIdx());
                            ticker.start();
                        }
                    }
                }
            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            // While editing and moving, only brush origin point need to be changed
            if (ticker.isActive())
                if (auto&& result = findClosestRidgeline(mEvent); result)
                    brushOriginPoint = result->second;

            if (bHeightEditing)
            {
                if (auto&& result = findClosestRidgeline(mEvent); result)
                    highlightPointsToEdit(result->first, result->second);
                else
                    clearHighlights();
            }

            if (bIsRidgeEditing && !mEvent->buttons().testFlag(Qt::RightButton))
            {
                drawBrushCircle(mEvent, 24);
                if (bDrawHighlights && !mEvent->buttons().testFlag(Qt::LeftButton))
                    highlightPointsToEdit(mEvent);
                else
                    clearHighlights();
            }
            else if (brushMarker)
            {
                Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
                brushMarker = nullptr;
            }

            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
                if (bIsRidgeEditing)
                {
                    editRidgeByBrush(mEvent);
                }
                else if (bDrawingOperation)
                {
                    if (!drawRidge(mEvent))
                        endRidge();
                }
            }
        }
        else if (mEvent->type() == QEvent::MouseButtonRelease)
        {
            if (mEvent->button() == Qt::LeftButton)
            {
                if (bIsRidgeDrawing)
                    endRidge();
                else if (ridgePreview)
                    clearTempRidge();
                else if ((bIsRidgeEditing || bHeightEditing) && !oldRidgesData.empty())
                {
                    if (ticker.isActive())
                        ticker.stop();

                    finishEditing();
                }
            }
            else if (mEvent->button() == Qt::RightButton)
            {
                if (!QApplication::overrideCursor())
                    RidgesSelectionMgr::get()->rightClick(mEvent);
            }
        }

        return false;
    }

    bool StageTools<EGenerationStage::Ridges>::startRidge(QMouseEvent* event)
    {
        auto point = OmnigenCameraMgr::get()->findPointInWorld(60, event->x(), event->y());

        if (!point)
            return false;

        if (!checkForCurrentTerrain(*point))
            return false;

        newRidgeStart = point;

        bDrawingOperation = true;
        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::drawRidge(QMouseEvent* event)
    {
        auto point = OmnigenCameraMgr::get()->findPointInWorld(60, event->x(), event->y());

        if (!point)
            return false;

        if (!checkForCurrentTerrain(*point))
            return false;

        if (ridgePreview)
        {
            // Finish subridge drawing on intersection with any ridge
            if (auto result = checkParentIntersection(*point))
            {
                ridgePreview->extendMarker((*result).second);
                return endRidge((*result).first);
            }

            auto lastPt = ridgePreview->getControlPoints().back();
            Segment2D ridgeSeg = { *point, {lastPt.x(), lastPt.z()} };

            // Check for any intersections of temporary ridge with self
            auto&& cPts = ridgePreview->getControlPoints();
            for (int i = 0; i < cPts.size() - 1; i++)
            {
                if (ridgeSeg.intersects(Segment2D({ cPts[i].x(), cPts[i].z() }, { cPts[i + 1].x(), cPts[i + 1].z() }), false))
                    return false;
            }

            // Extend the existing marker while drawing
            if (distance(GVector2D(lastPt), *point) > 625)
                ridgePreview->extendMarker(QVector3D(point->x, 60, point->z));
        }
        else if (!newRidgeStart)
        {
            newRidgeStart = point;
        }
        else if (distance(*newRidgeStart, *point) > 625)
        {
            // this happens only once, after mouse movement of at least 650 units after startRidge()
            ridgePreview = Generation::Data::get()->createMarker<DLineMarker, true>(std::vector<QVector3D>({ *newRidgeStart, QVector3D(point->x, 60, point->z) }), QVector4D(1, 1, 1, 1), false, 60);
            bBlockHover = true;
        }

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::endRidge(qint64 parentGuid)
    {
        if (!ridgePreview)
        {
            clearTempRidge();
            return false;
        }

        // Reverse the control points so that potential subridges start from parent
        std::vector<QVector3D> reversedVec(ridgePreview->getControlPoints().size());
        std::ranges::reverse_copy(ridgePreview->getControlPoints(), reversedVec.begin());

        createRidge(reversedVec, parentGuid);
        clearTempRidge();

        return true;
    }

    void StageTools<EGenerationStage::Ridges>::clearTempRidge()
    {
        if (ridgePreview)
        {
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(ridgePreview->getGuid());
            ridgePreview.clear();
            bBlockHover = false;
        }

        bDrawingOperation = false;
        currentDomain.clear();
        newRidgeStart = {};
    }

    std::optional<QPair<qint64, QVector3D>> StageTools<EGenerationStage::Ridges>::checkParentIntersection(const GVector2D& point)
    {
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

        auto lastPt = ridgePreview->getControlPoints().back();
        Segment2D subridgeSeg = { point, {lastPt.x(), lastPt.z()} };

        for (auto&& ridge : ridges)
        {
            auto&& cpts = ridge->getControlPoints();
            for (int i = 0; i < cpts.size() - 1; i++)
            {
                Segment2D ridgeSeg = { { cpts[i].x(), cpts[i].z() }, {cpts[i + 1].x(), cpts[i + 1].z()} };

                if (subridgeSeg.intersects(ridgeSeg, true))
                    return QPair(ridge->getGuid(), cpts[i]);
            }
        }

        return {};
    }

    bool StageTools<EGenerationStage::Ridges>::checkForCurrentTerrain(const GVector2D& point)
    {
        if (auto domain = Generation::Data::get()->getDomainAtSquare(point.toGPoint(), EDomainType::Terrain); domain)
        {
            if (!DLandmassMarker::isInsideLand(point))
                return false;

            if (currentDomain.isNull())
                currentDomain = domain;

            if ((domain == currentDomain))
                return true;
        }

        return false;
    }

    void StageTools<EGenerationStage::Ridges>::updateTreeViewSelection()
    {
        blockSignals(true);
        treeModel.clearSelection();

        for (auto&& ridge : RidgesSelectionMgr::get()->getSelection<ERidgesSelection::Ridge>())
            treeModel.selectRidge(ridge->getGuid());

        blockSignals(false);
    }

    void StageTools<EGenerationStage::Ridges>::onSelectionChanged()
    {
        updateTreeViewSelection();
        QOmnigenViewportSection::repaintAll();
    }

    bool StageTools<EGenerationStage::Ridges>::createRidge(const std::vector<QVector3D>& cPts, qint64 parentGuid)
    {
        std::vector<QVector3D> points;
        qint64 guid = 0;
        qint64 parentId = parentGuid;
        QString name;
        auto genData = Generation::Data::get();
        QSharedPointer<DRidgeMarker> parentMarker = nullptr;
        std::vector<float> pointHeights;

        HISTORY_PUSH(createRidge, {}, {});

        HISTORY_LOAD(parentId);
        if (parentId != 0)
            parentMarker = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(treeModel.getRidgeGuidChain(parentId));

        if (!HISTORY_LOAD4(points, guid, name, pointHeights))
        {
            auto domainData = currentDomain->getData<EDomainType::Terrain>();
            points = cPts;
            pointHeights = getRidgeHeight(cPts, parentMarker);
        }

        auto marker = genData->createMarker<DRidgeMarker>(points, parentMarker);
        marker->setHeights(pointHeights);

        if (!guid)
        {
            guid = marker->getGuid();
            name = marker->getName();
            HISTORY_SAVE4(points, guid, name, pointHeights);
            HISTORY_SAVE(parentId);

            if (parentMarker)
            {
                parentGuid = parentMarker->getGuid();
                HISTORY_SAVE(parentGuid);
            }
        }
        else
        {
            marker->setGuid(guid);
            marker->setName(name);
        }

        // Link subridge with its parent
        if (parentMarker)
            parentMarker->joinRidgeAsSubridge(marker);

        std::optional<ETableLand> tablelandType;
        if (!HISTORY_LOAD(tablelandType))
        {
            if (currentDomain->getData<EDomainType::Terrain>()->landform == ELandform::Tablelands)
            {
                if (parentMarker)
                    tablelandType = parentMarker->getTablelandType();
                else
                    tablelandType = DRidgeMarker::getRandomTablelandType(currentDomain->getData<EDomainType::Terrain>()->tableland);
            }

            HISTORY_SAVE(tablelandType);
        }

        if(tablelandType)
            marker->setTablelandType(*tablelandType);

        genData->initializeQueuedMarkers();

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::createRidge_Undo(const std::vector<QVector3D>& cPts, qint64 parentGuid)
    {
        HISTORY_POP();

        qint64 guid;
        auto genData = Generation::Data::get();

        HISTORY_LOAD(guid);

        auto marker = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(treeModel.getRidgeGuidChain(guid));

        if (!(marker->getParent()))
        {
            Generation::Data::get()->clearSingleExactMarker<DRidgeMarker>(guid);
        }
        // Remove self from parents child list
        else
        {
            auto parentMarker = marker->getParent().lock();
            emit Editable::aboutToBeDeleted(marker);
            parentMarker->removeChild(marker);
        }

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::deleteRidge(qint64 guid)
    {
        qint64 id;
        std::vector<QVector3D> cPts;
        QSharedPointer<DRidgeMarker> marker;
        std::vector<float> pointHeights;

        HISTORY_PUSH(deleteRidge, {});

        if (!HISTORY_LOAD(id))
        {
            id = guid;
            marker = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(treeModel.getRidgeGuidChain(id));
            cPts = marker->getControlPoints();
            auto name = marker->getName();
            pointHeights = marker->getHeights();
            auto tablelandType = marker->getTablelandType();

            HISTORY_SAVE(tablelandType);
            HISTORY_SAVE4(id, cPts, name, pointHeights);
        }
        else
            marker = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(treeModel.getRidgeGuidChain(id));

        // Delete possible children down the chain
        if (!(marker->getChildren().empty()))
        {
            auto children = marker->getChildren();
            int childCount = children.size();
            HISTORY_SAVE(childCount);

            for (auto&& child : children)
                deleteRidge(child->getGuid());
        }

        if (!(marker->getParent()))
        {
            Generation::Data::get()->clearSingleExactMarker<DRidgeMarker>(id);
        }
        // Remove self from parent's child list
        else
        {
            auto parentMarker = marker->getParent().lock();
            parentMarker->removeChild(marker);
            qint64 parentGuid = parentMarker->getGuid();

            HISTORY_SAVE(parentGuid);

            emit Editable::aboutToBeDeleted(marker);
        }

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::deleteRidge_Undo(qint64 guid)
    {
        HISTORY_POP();
        qint64 id;
        qint64 parentGuid;
        std::vector<QVector3D> cPts;
        QString name;
        std::vector<float> pointHeights;
        QSharedPointer<DRidgeMarker> parentMarker = nullptr;
        std::optional<ETableLand> tablelandType;

        if (HISTORY_LOAD(parentGuid))
            parentMarker = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(treeModel.getRidgeGuidChain(parentGuid));

        HISTORY_LOAD4(id, cPts, name, pointHeights);
        auto marker = Generation::Data::get()->createMarker<DRidgeMarker>(cPts, parentMarker);
        marker->setGuid(id);
        marker->setName(name);
        marker->setHeights(pointHeights);

        HISTORY_LOAD(tablelandType);
        if (tablelandType)
            marker->setTablelandType(*tablelandType);

        // Link subridge with its parent
        if (parentMarker)
            parentMarker->joinRidgeAsSubridge(marker);

        Generation::Data::get()->initializeQueuedMarkers();

        // Undo deleted subridges of this ridge
        int childCount;
        if (HISTORY_LOAD(childCount))
            for (int i = 0; i < childCount; i++)
                deleteRidge_Undo({});

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::deleteMultipleRidges(const std::vector<qint64>& guids)
    {
        HISTORY_PUSH(deleteMultipleRidges, {});

        std::vector<qint64> toDeletion;

        if (!HISTORY_LOAD(toDeletion))
        {
            for (auto&& guid : guids)
            {
                auto guidChain = treeModel.getRidgeGuidChain(guid);

                // Check if guidChain does not contain other guids marked for deletion / checks from highest guid in hierarchy, omitting self
                for (int i = guidChain.size() - 1; i > 0; i--)
                {
                    if (auto result = std::find(guids.begin(), guids.end(), guidChain[i]); result != guids.end())
                    {
                        // Check if guid was not already added
                        if (auto r = std::find(toDeletion.begin(), toDeletion.end(), guidChain[i]); r == toDeletion.end())
                            toDeletion.emplace_back(guidChain[i]);

                        goto skip;
                    }
                }

                // Check if guid was not previously added by child ridges
                if (auto r = std::find(toDeletion.begin(), toDeletion.end(), guid); r == toDeletion.end())
                    toDeletion.emplace_back(guid);

            skip:
                continue;
            }

            HISTORY_SAVE(toDeletion);
        }

        for (auto&& guid : toDeletion)
            deleteRidge(guid);

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::deleteMultipleRidges_Undo(const std::vector<qint64>& guids)
    {
        HISTORY_POP();

        std::vector<qint64> toDeletion;
        HISTORY_LOAD(toDeletion);

        for (int i = 0; i < toDeletion.size(); i++)
            deleteRidge_Undo({});

        return true;
    }

    std::vector<float> StageTools<EGenerationStage::Ridges>::getRidgeHeight(const std::vector<QVector3D>& cPts, QSharedPointer<DRidgeMarker> parentRidge)
    {
        std::vector<float> newHeights(cPts.size());
        std::vector<int> peakIndices;

        auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(cPts.front().x(), cPts.front().z()).toGPoint(), EDomainType::Terrain);
        auto&& localMinimumHeight = domain->getData<EDomainType::Terrain>()->minHeight;
        auto&& localMaximumHeight = domain->getData<EDomainType::Terrain>()->maxHeight;

        if (parentRidge)
        {
            auto&& parentCpts = parentRidge->getControlPoints();
            // Find the source point of the new ridge, and assign the height from parent
            for (int i = 0; i < parentCpts.size(); ++i)
            {
                // The height is as of yet unknown, thus the search of source index needs to be done on GVectors
                if (vEq(GVector2D(parentCpts[i]), GVector2D(cPts[0])))
                {
                    newHeights[0] = parentRidge->getHeights()[i];
                    peakIndices.emplace_back(0);
                    break;
                }
            }
        }

        // Tableland ridgelines are by default flat
        if (domain->getData<EDomainType::Terrain>()->landform == ELandform::Tablelands)
        {
            float height = 0.0f;
            if (peakIndices.size() == 1)
                height = newHeights[0];
            else
                height = localMaximumHeight;

            for (int i = 0; i < cPts.size(); ++i)
                newHeights[i] = height;

            return newHeights;
        }

        if (peakIndices.empty())
        {
            std::uniform_int_distribution<> peakChance(1, 2);
            int peakCount = peakChance(Generation::gRandomEngine);
            for (int i = 0; i < peakCount; ++i)
            {
                while (true)
                {
                    auto peakIdxGen = hybrid_int_distribution<int>(0, cPts.size() - 1, 0.75f, 0.5f);
                    int peakIdx = peakIdxGen(Generation::gRandomEngine);
                    if (auto&& it = std::find(peakIndices.begin(), peakIndices.end(), peakIdx); it == peakIndices.end())
                    {
                        newHeights[peakIdx] = localMaximumHeight;
                        peakIndices.emplace_back(peakIdx);
                        break;
                    }
                }
            }
        }

        std::sort(peakIndices.begin(), peakIndices.end());

        Generation::StageGen<EGenerationStage::Ridges>::computeRidgeHeight(&newHeights, cPts, peakIndices, parentRidge);

        return newHeights;
    }

    void StageTools<EGenerationStage::Ridges>::editRidgeByBrush(QMouseEvent* event)
    {
        // Find point on world
        if (auto mPoint = OmnigenCameraMgr::get()->findPointInWorld(60, event->x(), event->y()); mPoint)
        {
            // Check if there is any brush movement
            if (!oldBrushPos)
            {
                oldBrushPos = mPoint;
                return;
            }

            int brushS = brushSize * 1000;
            auto moveDelta = (*mPoint - *oldBrushPos).normalized();

            // Start saving movement info as soon as any edition happens
            if (bBrushStrokes && !oldRidgesData.empty())
                brushWholeMovement += (*mPoint).dist(*oldBrushPos);

            oldBrushPos = mPoint;

            // Create a line according to the brush movement to check against any potential ridges
            auto pointToCheck = *mPoint + GVector2D(brushS * moveDelta.x, brushS * moveDelta.z);
            Segment2D brushSeg(*mPoint, pointToCheck);

            QSharedPointer<DRidgeMarker> ridgeToEdit;
            int peakIdx;

            // Find suitable ridge, and the index of the closest point
            if (auto findRidgeOutput = findClosestRidge(brushSeg, *mPoint); findRidgeOutput)
            {
                ridgeToEdit = (*findRidgeOutput).first;
                peakIdx = (*findRidgeOutput).second;

                auto&& oldPts = ridgeToEdit->getControlPoints();
                std::vector<QVector3D> newPts = oldPts;

                // Find owning domain - this is purposely taken as first point of ridge, as mousePos can potentially be outside of adequate domain
                auto gPoint = GVector2D(oldPts.front().x(), oldPts.front().z()).toGPoint();
                auto&& domain = Generation::Data::get()->getDomainAtSquare(gPoint, EDomainType::Terrain);

                // Gather all subridges along with their source points of edited ridge for further checks
                std::vector<std::pair<QSharedPointer<DRidgeMarker>, int>> subridges;
                if (auto&& children = ridgeToEdit->getChildren(); !children.empty())
                    for (auto&& child : children)
                        subridges.emplace_back(child, child->getSourcePointIdx());

                float moveFactor = 1.0f;
                if (bBrushStrokes)
                {
                    moveFactor = std::clamp((brushS * 4.0f - brushWholeMovement) / (brushS * 4.0f), 0.0f, 1.0f);
                    if (moveFactor == 0)
                        return;
                }

                // First(peak) point to edit
                QVector3D newPeak = oldPts[peakIdx] + QVector3D((moveFactor * brushS * moveDelta.x * (brushStrength / 5.0f)), 0, (moveFactor * brushS * moveDelta.z * (brushStrength / 5.0f)));

                // Check for potential subridges originating from peak point, and try propagating movement
                auto results = subridges | std::views::filter([peakIdx](const auto& ele) {return ele.second == peakIdx; });
                std::unordered_set<qint64> ridgesToSkip;

                // Gather all subridges being moved for validation skips
                // Due to the possibility of multiple subridges originating from one point subridges to skip might be gathered multiple times
                for (auto&& result : results)
                    ridgesToSkip.merge(collectAllSubridgesIds(result.first));

                // Try moving appropriate subridges (if they fail to do so, point edition fails too)
                for (auto&& result : results)
                    if (!propagateToSubridge(result.first, newPeak, ridgesToSkip))
                        return;

                // TODO: while doing redo/undo - load previous subridge points if newPoints validation fails
                if (valdiatePoint(newPts, newPeak, peakIdx, domain, ridgeToEdit, ridgesToSkip))
                    newPts[peakIdx] = newPeak;
                else
                    return;

                // Propagate transformation to all next points
                for (int i = peakIdx; i < oldPts.size(); i++)
                {
                    if (i == peakIdx)
                        continue;

                    // Prevents propagation on far away points (and prevents propagation on looped ridges)
                    auto distToM = (*mPoint).dist({ oldPts[i].x(), oldPts[i].z() });
                    if (distToM > 2 * (brushSize * 1000))
                        break;

                    newPts[i] = getAnotherPoint(newPts, oldPts, i, *mPoint, domain, ridgeToEdit, true);
                }

                // Propagate transformation to all previous points
                for (int i = peakIdx; i >= 0; i--)
                {
                    if (i == peakIdx)
                        continue;

                    // Prevents propagation on far away points (and prevents propagation on looped ridges)
                    auto distToM = (*mPoint).dist({ oldPts[i].x(), oldPts[i].z() });
                    if (distToM > 2 * (brushSize * 1000))
                        break;

                    newPts[i] = getAnotherPoint(newPts, oldPts, i, *mPoint, domain, ridgeToEdit, false);
                }

                // If ridge has parent block first (source) point
                // Transformation will only be forwarded to subridges, never to parent ridges
                if (ridgeToEdit->getParent())
                    newPts[0] = oldPts[0];

                // Save ridge state before editing 
                if (auto result = std::find_if(oldRidgesData.begin(), oldRidgesData.end(), [ridgeToEdit](const auto& oldData) {return std::get<0>(oldData) == ridgeToEdit->getGuid(); }); result == oldRidgesData.end())
                    oldRidgesData.emplace_back(ridgeToEdit->getGuid(), oldPts, ridgeToEdit->getSourcePointIdx());

                // Post movement modifications (point creation/destruction/acute angles merge)
                newPts = modifyRidgePointCount(newPts, ridgeToEdit);
            }
        }
    }

    std::optional<std::pair<QSharedPointer<DRidgeMarker>, int>> StageTools<EGenerationStage::Ridges>::findClosestRidge(const Segment2D& segToCheck, const GVector2D& mPoint)
    {
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        std::pair<QSharedPointer<DRidgeMarker>, int> closestRidge;
        float distance = 50000.0f;

        for (auto&& ridge : ridges)
        {
            auto&& cPts = ridge->getControlPoints();
            for (int i = 0; i < cPts.size() - 1; i++)
            {
                if (segToCheck.intersects(Segment2D({ cPts[i].x(), cPts[i].z() }, { cPts[i + 1].x(), cPts[i + 1].z() }), true))
                {
                    // First point to be edited, and from which movement will be propagated
                    int peakIndex = mPoint.dist({ cPts[i].x(), cPts[i].z() }) < mPoint.dist({ cPts[i + 1].x(), cPts[i + 1].z() }) ? i : i + 1;
                    if (distance > mPoint.dist({ cPts[peakIndex].x(), cPts[peakIndex].z() }))
                    {
                        distance = mPoint.dist({ cPts[peakIndex].x(), cPts[peakIndex].z() });
                        closestRidge = { ridge, peakIndex };
                    }
                }
            }
        }

        if (closestRidge.first)
            return closestRidge;

        return {};
    }

    QVector3D StageTools<EGenerationStage::Ridges>::getAnotherPoint(const std::vector<QVector3D>& newPts, const std::vector<QVector3D>& oldPoints, int pIndex, const GVector2D& mousePos, QSharedPointer<DDomain> domain, QSharedPointer<DRidgeMarker> ownerRidge, bool positiveDirection)
    {
        // Distance factor, the further away from the mouse point, the weaker the point movement
        auto distToM = mousePos.dist({ oldPoints[pIndex].x(), oldPoints[pIndex].z() });
        float factor = (brushSize * 1000) / std::pow(distToM, 1.1);

        float moveFactor = 1.0f;
        if (bBrushStrokes)
            moveFactor = std::clamp((brushSize * 4000.0f - brushWholeMovement) / (brushSize * 4000.0f), 0.0f, 1.0f);

        // Transformation averaged from previous point transformation influenced by vector from mouse point and distance factor
        auto vec = ((newPts[positiveDirection ? pIndex - 1 : pIndex + 1] - oldPoints[positiveDirection ? pIndex - 1 : pIndex + 1] + oldPoints[pIndex] - QVector3D(mousePos.x, 60, mousePos.z)) / 2) * factor * moveFactor * (brushStrength / 5.0f);
        QVector3D newPoint = { oldPoints[pIndex].x() + vec.x(), oldPoints[pIndex].y(), oldPoints[pIndex].z() + vec.z() };

        // Gather all subridges along with their source points of edited ridge for further checks
        std::vector<std::pair<QSharedPointer<DRidgeMarker>, int>> subridges;
        if (auto&& children = ownerRidge->getChildren(); !children.empty())
            for (auto&& child : children)
                subridges.emplace_back(child, child->getSourcePointIdx());

        // Check for potential subridges originating from peak point, and try propagating movement
        auto results = subridges | std::views::filter([pIndex](const auto& ele) {return ele.second == pIndex; });
        std::unordered_set<qint64> ridgesToSkip;
        for (auto&& result : results)
            ridgesToSkip.merge(collectAllSubridgesIds(result.first));

        for (auto&& result : results)
            if (!propagateToSubridge(result.first, newPoint, ridgesToSkip))
                return oldPoints[pIndex];

        // TODO: while doing redo/undo - load previous subridge points if newPoints validation fails
        if (valdiatePoint(newPts, newPoint, pIndex, domain, ownerRidge, ridgesToSkip))
            return newPoint;

        return oldPoints[pIndex];
    }

    bool StageTools<EGenerationStage::Ridges>::propagateToSubridge(QSharedPointer<DRidgeMarker> subridge, const QVector3D& parentPoint, const std::unordered_set<qint64>& ridgesToSkip)
    {
        // Find owning domain - this is purposely taken as first point of ridge, as mousePos can potentially be outside of adequate domain
        auto gPoint = GVector2D(parentPoint.x(), parentPoint.z()).toGPoint();
        auto&& domain = Generation::Data::get()->getDomainAtSquare(gPoint, EDomainType::Terrain);

        std::vector<std::pair<QSharedPointer<DRidgeMarker>, int>> subridges;
        auto&& childPts = subridge->getControlPoints();
        auto newChildPts = childPts;

        // Gather all subridges of current ridge, along with their source points
        if (auto&& children = subridge->getChildren(); !children.empty())
            for (auto&& child : children)
                subridges.emplace_back(child, child->getSourcePointIdx());

        auto vec = parentPoint - childPts.front();

        for (int i = 0; i < childPts.size(); i++)
        {
            if (i == 0)
            {
                // Source point of subridge must be same as parents point at source index / does not require validation as parent did it
                newChildPts[0] = parentPoint;
                continue;
            }

            QVector3D newPoint = { childPts[i].x() + vec.x(), childPts[i].y(), childPts[i].z() + vec.z() };
            newChildPts[i] = newPoint;
        }

        // Validate all new points of subridge
        for (int i = 0; i < newChildPts.size(); i++)
        {
            if (!valdiatePoint(newChildPts, newChildPts[i], i, domain, subridge, ridgesToSkip, true))
                return false;
        }

        // If points were validated, try to move subridges of this ridge
        for (auto&& pair : subridges)
            if (!propagateToSubridge(pair.first, newChildPts[pair.second], ridgesToSkip))
                return false;

        // Save subridge state before editing 
        if (auto result = std::find_if(oldRidgesData.begin(), oldRidgesData.end(), [subridge](const auto& oldData) {return std::get<0>(oldData) == subridge->getGuid(); }); result == oldRidgesData.end())
            oldRidgesData.emplace_back(subridge->getGuid(), childPts, subridge->getSourcePointIdx());

        subridge->moveRidgePoints(newChildPts);

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::valdiatePoint(const std::vector<QVector3D>& newPoints, const QVector3D& pointToCheck, int pointToCheckIndex, QSharedPointer<DDomain> ownerDomain, QSharedPointer<DRidgeMarker> ownerRidge, const std::unordered_set<qint64>& ridgesToSkip, bool skipSelf)
    {
        auto&& perimeter = ownerDomain->getPerimeter();
        std::vector<Segment2D> ridgeSegments;
        float distToCheck = 1000.0f;
        QVector3D vec1 = {}, vec2 = {};

        // Next point segment
        if (pointToCheckIndex < newPoints.size() - 1)
        {
            ridgeSegments.emplace_back(Segment2D({ pointToCheck.x(), pointToCheck.z() }, { newPoints[pointToCheckIndex + 1].x(), newPoints[pointToCheckIndex + 1].z() }));
            vec1 = (pointToCheck - newPoints[pointToCheckIndex + 1]).normalized();
        }

        // Previous point segment
        if (pointToCheckIndex > 0)
        {
            ridgeSegments.emplace_back(Segment2D({ pointToCheck.x(), pointToCheck.z() }, { newPoints[pointToCheckIndex - 1].x(), newPoints[pointToCheckIndex - 1].z() }));
            vec1 = (pointToCheck - newPoints[pointToCheckIndex - 1]).normalized();
        }

        // Checks for intersection of any domain border with any applicable segments
        // Both previous and next segments are checked in order to prevent corner border crossing
        for (auto&& seg : perimeter)
            for (auto&& ridgeSeg : ridgeSegments)
                if (seg.intersects(ridgeSeg, true))
                    return false;

        for (auto&& ridgeSeg : ridgeSegments)
            if (!DLandmassMarker::isInsideLand(ridgeSeg))
                return false;

        // Prevent acute angles in root of subridges and source points due to merge being disabled for them
        if (!vec1.isNull() && !vec2.isNull())
        {
            if ((pointToCheckIndex == 1 && ownerRidge->getParent()) ||
                (ownerRidge->getChildren() | std::views::filter([pointToCheckIndex](const auto& ele) {return ele->getSourcePointIdx() == pointToCheckIndex; })))
                if (angle360(vec1, vec2) > 330 || angle360(vec1, vec2) < 30)
                    return false;
        }

        // Subridges movement can be skip self checks (while editing parent ridge, or higher)
        if (!skipSelf)
        {
            // Check if new point does not intersect with its own ridge
            for (int i = 0; i < newPoints.size() - 1; i++)
            {
                if (i == pointToCheckIndex - 1 || i == pointToCheckIndex)
                    continue;

                Segment2D segToCheck({ newPoints[i].x(), newPoints[i].z() }, { newPoints[i + 1].x(), newPoints[i + 1].z() });
                for (auto&& ridgeSeg : ridgeSegments)
                    if (segToCheck.intersects(ridgeSeg, false))
                        return false;
            }

            // Check if new point is not close enough to other points of self
            for (int i = 0; i < newPoints.size(); i++)
            {
                // Skip on self, or closest neighbours
                if (i == pointToCheckIndex - 2 || i == pointToCheckIndex - 1 ||
                    i == pointToCheckIndex || i == pointToCheckIndex + 1 || i == pointToCheckIndex + 2)
                    continue;

                if (newPoints[i].distanceToPoint(pointToCheck) < distToCheck)
                    return false;
            }
        }

        // Check distance and intersection against all other ridges from same domain
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        for (auto&& ridge : ridges)
        {
            // Skip own ridge
            if (ridge->getGuid() == ownerRidge->getGuid())
                continue;

            // Skip checks against own subridges while moving a whole subridge branch
            if (ridgesToSkip.contains(ridge->getGuid()))
                continue;

            auto&& cpts = ridge->getControlPoints();

            // Check if ridge is in proper domain
            auto point = GVector2D(cpts.front().x(), cpts.front().z()).toGPoint();
            if (Generation::Data::get()->getDomainAtSquare(point, EDomainType::Terrain) != ownerDomain)
                continue;

            // Save parent guid for future checks
            qint64 parentGuid = 0;
            if (ownerRidge->getParent())
                parentGuid = ownerRidge->getParent().lock()->getGuid();

            for (int i = 0; i < cpts.size(); i++)
            {
                // Check for intersection with other ridge
                if (i < cpts.size() - 1)
                {
                    Segment2D segToCheck({ cpts[i].x(), cpts[i].z() }, { cpts[i + 1].x(), cpts[i + 1].z() });
                    for (auto&& ridgeSeg : ridgeSegments)
                        if (segToCheck.intersects(ridgeSeg, false))
                            return false;
                }

                // Skip distance check for first two points of own subridges
                if (i < 2)
                    if (auto children = ownerRidge->getChildren(); !children.empty())
                        if (auto result = std::find_if(children.begin(), children.end(), [ridge](const auto& child) {return child->getGuid() == ridge->getGuid(); }); result != children.end())
                            continue;

                // Skip root points checks against parent ridge
                if ((pointToCheckIndex <= 1) && ridge->getGuid() == parentGuid)
                    continue;

                // Distance check
                if (cpts[i].distanceToPoint(pointToCheck) < distToCheck)
                    return false;
            }
        }

        return true;
    }

    std::vector<QVector3D> StageTools<EGenerationStage::Ridges>::modifyRidgePointCount(const std::vector<QVector3D>& ptsToCheck, QSharedPointer<DRidgeMarker> ridge)
    {
        std::vector<QVector3D> newPts;
        std::vector<float> newHeights;
        auto&& currentHeights = ridge->getHeights();
        int pointsAdded = 0;

        // Save all source points of children
        std::vector<std::pair<QSharedPointer<DRidgeMarker>, int >> srcPts;

        for (auto&& child : ridge->getChildren())
            srcPts.emplace_back(child, child->getSourcePointIdx());

        for (int i = 0; i < ptsToCheck.size(); i++)
        {
            newPts.emplace_back(ptsToCheck[i]);
            newHeights.emplace_back(currentHeights[i]);
            if (i == ptsToCheck.size() - 1)
                break;

            // Prevent any modifications of subridge root point
            if (i < 2 && ridge->getParent())
                continue;

            // If 3 points create an acute angle, merge them into one point (resultant of the three) [must be a minimum of 4 points present overall]
            if (ptsToCheck.size() > 3 && newPts.size() > 1 && i < ptsToCheck.size() - 1)
            {
                auto vec1 = (ptsToCheck[i] - ptsToCheck[i + 1]).normalized();
                auto vec2 = (ptsToCheck[i] - ptsToCheck[i - 1]).normalized();
                float angle = angle360(vec1, vec2);
                if (angle > 310 || angle < 50)
                    if (auto mergPoint = mergePoints(ptsToCheck, i, ridge); mergPoint)
                    {
                        newPts.pop_back();
                        newPts[newPts.size() - 1] = *mergPoint;
                        pointsAdded -= 2;

                        // Height merge
                        newHeights.pop_back();
                        newHeights[newHeights.size() - 1] = (currentHeights[i] + currentHeights[i + 1] + currentHeights[i - 1]) / 3;

                        for (auto&& pair : srcPts)
                            // mergePoints guarantees that no source point can be present on merged points
                            // Move appropriate source points of children
                            if (pair.second > newPts.size() + 2)
                                pair.second -= 2;

                        // Iterate to skip next point
                        i++;
                        continue;
                    }
            }

            // Add new in the midpoint of two sufficiently far away points
            if (auto dist = ptsToCheck[i].distanceToPoint(ptsToCheck[i + 1]); dist > 2000)
            {
                newPts.emplace_back((ptsToCheck[i] + ptsToCheck[i + 1]) / 2);
                newHeights.emplace_back((currentHeights[i] + currentHeights[i + 1]) / 2);
                pointsAdded++;

                // Move appropriate source points of children
                for (auto&& pair : srcPts)
                    if (pair.second >= newPts.size() - 1)
                        pair.second++;
            }
            // Delete point that are too close to other ones [must be minimum of 3 points present overall]
            else if (dist < 300 && newPts.size() > 2)
            {
                for (auto&& pair : srcPts)
                {
                    // Skip source point deletion
                    if (pair.second == newPts.size() - 1)
                        continue;
                }

                newPts.pop_back();
                newHeights.pop_back();
                pointsAdded--;

                for (auto&& pair : srcPts)
                {
                    // Move appropriate source points of children
                    if (pair.second > newPts.size())
                        pair.second--;
                }
            }
        }

        // Set new source points for children
        for (auto&& pair : srcPts)
            pair.first->setSourcePointIdx(pair.second);

        ridge->moveRidgePoints(newPts, pointsAdded);
        ridge->setHeights(newHeights);

        return newPts;
    }

    std::optional<QVector3D> StageTools<EGenerationStage::Ridges>::mergePoints(const std::vector<QVector3D>& ptsToCheck, int mIdx, QSharedPointer<DRidgeMarker> ridge)
    {
        // Save all source points of children
        std::vector<std::pair<QSharedPointer<DRidgeMarker>, int>> srcPts;

        for (auto&& child : ridge->getChildren())
            srcPts.emplace_back(child, child->getSourcePointIdx());

        // Ensure that merged points are not source points to any ridges
        for (auto&& pair : srcPts)
            if (pair.second == mIdx - 1 || pair.second == mIdx || pair.second == mIdx + 1)
                return {};

        auto newPoint = (ptsToCheck[mIdx] + ptsToCheck[mIdx - 1] + ptsToCheck[mIdx + 1]) / 3;
        return newPoint;
    }

    std::unordered_set<qint64> StageTools<EGenerationStage::Ridges>::collectAllSubridgesIds(QSharedPointer<DRidgeMarker> ridge)
    {
        std::unordered_set<qint64> ids = { ridge->getGuid() };

        for (auto&& child : ridge->getChildren())
        {
            ids.emplace(child->getGuid());
            ids.merge(collectAllSubridgesIds(child));
        }

        return ids;
    }

    void StageTools<EGenerationStage::Ridges>::highlightPointsToEdit(QMouseEvent* mEvent)
    {
        if (auto mPoint = OmnigenCameraMgr::get()->findPointInWorld(60, mEvent->x(), mEvent->y()); mPoint)
        {
            // Clear any previous highlights
            clearHighlights();

            int brushS = brushSize * 1000;
            std::vector<QSharedPointer<DRidgeMarker>> ridges;
            Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

            float distance = 50000.0f;
            std::pair<QSharedPointer<DRidgeMarker>, int> closestRidge;

            // Find closest ridge
            for (auto&& ridge : ridges)
            {
                auto cPts = ridge->getControlPoints();

                for (int i = 0; i < cPts.size(); i++)
                {
                    auto distToP = cPts[i].distanceToPoint({ (*mPoint).x, 60, (*mPoint).z });
                    if (distToP < brushS * 3 && distToP < distance)
                    {
                        distance = distToP;
                        closestRidge = { ridge, i };
                    }
                }
            }

            if (closestRidge.first)
            {
                auto cPts = closestRidge.first->getControlPoints();

                // Save initial (peak) highlight
                highlights.emplace_back(Generation::Data::get()->createMarker<DLineMarker, true>(getCirclePoints(cPts[closestRidge.second], 12, 200), QVector4D(0, 0, 1, 1), true, 70.0f)->getGuid());

                auto highlightPoint = [this, &brushS, &cPts, &closestRidge](int i)
                {
                    // Do not propagate past the distance limitation
                    auto distToPeak = cPts[i].distanceToPoint(cPts[closestRidge.second]);
                    if (distToPeak > 2 * brushS)
                        return false;

                    // Same factor as in actual editing
                    float factor = std::clamp(((brushSize * 1000.0f) / std::powf(distToPeak, 1.1f)), 0.0f, 1.0f);

                    highlights.emplace_back(Generation::Data::get()->createMarker<DLineMarker, true>(getCirclePoints(cPts[i], 12, 200), QVector4D(0, 0, 1, factor), true, 70.0f)->getGuid());
                    return true;
                };

                // Propagate highlight to points after peak
                for (int i = closestRidge.second + 1; i < cPts.size(); i++)
                    if (!highlightPoint(i))
                        break;

                // Propagate highlight to points before peak
                for (int i = closestRidge.second - 1; i >= 0; i--)
                    if (!highlightPoint(i))
                        break;
            }
        }
    }

    void StageTools<EGenerationStage::Ridges>::highlightPointsToEdit(qint64 ridgeId, const QVector3D& brushOriginPoint)
    {
        clearHighlights();

        auto&& pointsUnderBrush = findPointsUnderBrush(ridgeId, brushOriginPoint);

        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        auto&& ridgeIter = std::find_if(ridges.begin(), ridges.end(), [ridgeId](auto&& ele) {return ele->getGuid() == ridgeId; });

        int brushRange = brushSize * heightBrushSizeFactor;
        GVector2D originPoint(brushOriginPoint.x(), brushOriginPoint.z());

        auto&& cPts = (*ridgeIter)->getControlPoints();
        for (auto&& idx : pointsUnderBrush)
        {
            // If point is within brush range, draw circular markers around it, fading in color as it gets further away
            if (auto distance = GVector2D(cPts[idx].x(), cPts[idx].z()).dist(originPoint); distance < brushRange)
            {
                float factor = std::clamp(((brushSize * heightBrushSizeFactor) / std::powf(distance, 1.1f)), 0.0f, 0.85f);
                highlights.emplace_back(Generation::Data::get()->createMarker<DLineMarker, true>(getCirclePoints(cPts[idx], 12, 150), QVector4D(0, 0, 1, factor), true, 70.0f)->getGuid());
            }
        }
    }

    std::vector<QVector3D> StageTools<EGenerationStage::Ridges>::getCirclePoints(const QVector3D& circleCenter, int points, int radius)
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

    void StageTools<EGenerationStage::Ridges>::clearHighlights()
    {
        if (!highlights.empty())
        {
            for (auto&& guid : highlights)
                Generation::Data::get()->clearSingleExactMarker<DLineMarker>(guid);

            highlights.clear();
        }
    }

    void StageTools<EGenerationStage::Ridges>::drawBrushCircle(QMouseEvent* mEvent, int circleDetail)
    {
        if (auto mPoint = OmnigenCameraMgr::get()->findPointInWorld(60, mEvent->x(), mEvent->y()); mPoint)
        {
            auto gPoint = GVector2D((*mPoint).x, (*mPoint).z).toGPoint();
            auto&& domain = Generation::Data::get()->getDomainAtSquare(gPoint, EDomainType::Terrain);
            int brushS = brushSize * 1000;

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

    std::optional<std::pair<qint64, QVector3D>> StageTools<EGenerationStage::Ridges>::findClosestRidgeline(QMouseEvent* mEvent)
    {
        auto* camera = OmnigenCameraMgr::get()->getCameraForActiveViewport();
        auto&& cameraPos = camera->getPosition();
        const auto rayVec = camera->makeRayFromCursor(mEvent->pos().x(), mEvent->pos().y());

        std::array<QVector3D, 2> cameraSeg = { cameraPos, cameraPos + rayVec * camera->getViewDistance() };

        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        std::map<float, std::pair<qint64, QVector3D>> allFound;

        // Save all ridge segments within range, and return the closest to the current camera position
        for (auto&& ridge : ridges)
        {
            auto newPoints = ridge->getControlPoints();
            for (int i = 0; i < newPoints.size() - 1; ++i)
            {
                std::array<QVector3D, 2> ridgeSeg = { newPoints[i], newPoints[i + 1] };
                auto [pointOnCamVec, pointOnRidge, dist] = distance(cameraSeg, ridgeSeg);
                if (dist < 200.0f)
                    allFound.emplace(pointOnCamVec.distanceToPoint(cameraPos), std::pair(ridge->getGuid(), pointOnRidge));
            }
        }

        if (!allFound.empty())
            return allFound.begin()->second;

        return {};
    }

    std::vector<int> StageTools<EGenerationStage::Ridges>::findPointsUnderBrush(qint64 ridgeId, const QVector3D& brushCentralPoint)
    {
        int brushRadius = brushSize * heightBrushSizeFactor;

        std::vector<int> pointsUnder;
        GVector2D flatCentralPoint(brushCentralPoint.x(), brushCentralPoint.z());

        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        auto&& ridgeIter = std::find_if(ridges.begin(), ridges.end(), [ridgeId](auto&& ele) {return ele->getGuid() == ridgeId; });

        auto&& cpts = (*ridgeIter)->getControlPoints();
        for (int i = 0; i < cpts.size(); ++i)
        {
            GVector2D flatPoint(cpts[i].x(), cpts[i].z());
            if (flatPoint.dist(flatCentralPoint) < brushRadius)
                pointsUnder.emplace_back(i);
        }

        return pointsUnder;
    }

    void StageTools<EGenerationStage::Ridges>::editPointsHeight()
    {
        if (!brushOriginPoint)
            return;

        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

        qint64 ridgeId = std::get<0>(oldRidgesData.front());
        auto&& pointsUnderBrush = findPointsUnderBrush(ridgeId, *brushOriginPoint);
        if (pointsUnderBrush.empty())
            return;

        auto&& ridgeIter = std::find_if(ridges.begin(), ridges.end(), [ridgeId](const auto& ele) {return ele->getGuid() == ridgeId; });

        if (bSculptTool)
            sculpt(*ridgeIter, pointsUnderBrush);
        else if (bSmoothTool)
            smooth(*ridgeIter, pointsUnderBrush);
        else if (bFlattenTool)
            flatten(*ridgeIter, pointsUnderBrush);

        highlightPointsToEdit(ridgeId, *brushOriginPoint);
    }

    void Design::StageTools<EGenerationStage::Ridges>::sculpt(QSharedPointer<DRidgeMarker> ridge, const std::vector<int>& pointsToEdit)
    {
        float movementDirection = brushStrength * (isKeyDown(VK_SHIFT) ? -30.0f : 30.0f);

        std::unordered_set<int> subridgeSourcePoints;
        auto&& subridges = ridge->getChildren();
        for (auto&& subridge : subridges)
            subridgeSourcePoints.emplace(subridge->getSourcePointIdx());

        auto newPoints = ridge->getControlPoints();

        for (auto&& idx : pointsToEdit)
        {
            if (auto&& distance = GVector2D(newPoints[idx].x(), newPoints[idx].z()).dist(GVector2D(brushOriginPoint->x(), brushOriginPoint->z())); distance <= brushSize * heightBrushSizeFactor)
            {
                // Skip root point edition if editing a subridge
                if (idx == 0 && ridge->getParent())
                    continue;

                newPoints[idx].setY(newPoints[idx].y() + (movementDirection * (1 - (distance / (brushSize * heightBrushSizeFactor)))));
                if (!subridgeSourcePoints.empty() && subridgeSourcePoints.contains(idx))
                {
                    auto&& subridgeIter = subridges.begin();
                    while (true)
                    {
                        // Save subridge for undo/redo
                        subridgeIter = std::find_if(subridgeIter, subridges.end(), [idx](const auto& ele) {return ele->getSourcePointIdx() == idx; });
                        if (subridgeIter == subridges.end())
                            break;

                        auto&& subridge = *subridgeIter;
                        if (auto&& result = std::find_if(oldRidgesData.begin(), oldRidgesData.end(), [subridge](const auto& oldData) {return std::get<0>(oldData) == subridge->getGuid(); }); result == oldRidgesData.end())
                            oldRidgesData.emplace_back((*subridgeIter)->getGuid(), (*subridgeIter)->getControlPoints(), (*subridgeIter)->getSourcePointIdx());

                        // Set new height for subridge root point
                        auto newSubridgePoints = (*subridgeIter)->getControlPoints();
                        newSubridgePoints[0].setY(newSubridgePoints[0].y() + (movementDirection * (1 - (distance / (brushSize * heightBrushSizeFactor)))));
                        (*subridgeIter)->moveRidgePoints(newSubridgePoints);

                        // Save subridge height info
                        auto newSubridgeHeights = (*subridgeIter)->getHeights();
                        newSubridgeHeights[0] = newSubridgePoints[0].y();
                        (*subridgeIter)->setHeights(newSubridgeHeights);
                        subridgeIter++;
                    }
                }
            }
        }

        std::vector<float> newHeights(newPoints.size());
        for (int i = 0; i < newPoints.size(); ++i)
            newHeights[i] = newPoints[i].y();

        ridge->setHeights(newHeights);
        ridge->moveRidgePoints(newPoints);
    }

    void Design::StageTools<EGenerationStage::Ridges>::smooth(QSharedPointer<DRidgeMarker> ridge, const std::vector<int>& pointsToEdit)
    {
        float movementDirection = brushStrength * (isKeyDown(VK_SHIFT) ? -1.0f : 1.0f);

        std::unordered_set<int> subridgeSourcePoints;
        auto&& subridges = ridge->getChildren();
        for (auto&& subridge : subridges)
            subridgeSourcePoints.emplace(subridge->getSourcePointIdx());

        auto newPoints = ridge->getControlPoints();
        std::vector<float> heights;

        for (auto&& idx : pointsToEdit)
            if (auto&& distance = GVector2D(newPoints[idx].x(), newPoints[idx].z()).dist(GVector2D(brushOriginPoint->x(), brushOriginPoint->z())); distance <= brushSize * heightBrushSizeFactor)
                heights.emplace_back(newPoints[idx].y());

        float averageHeight = std::accumulate(heights.begin(), heights.end(), 0.0f) / heights.size();

        for (auto&& idx : pointsToEdit)
        {
            if (auto&& distance = GVector2D(newPoints[idx].x(), newPoints[idx].z()).dist(GVector2D(brushOriginPoint->x(), brushOriginPoint->z())); distance <= brushSize * heightBrushSizeFactor)
            {
                // Skip root point edition if editing a subridge
                if (idx == 0 && ridge->getParent())
                    continue;

                // Edit points to slowly reach the average height
                float movement = (averageHeight - newPoints[idx].y()) * 0.1 * (brushStrength / 20.0f) * (1 - (distance / (brushSize * heightBrushSizeFactor)));
                newPoints[idx].setY(newPoints[idx].y() + (movementDirection * movement));

                if (!subridgeSourcePoints.empty() && subridgeSourcePoints.contains(idx))
                {
                    auto&& subridgeIter = subridges.begin();
                    while (true)
                    {
                        // Save subridge for undo/redo
                        subridgeIter = std::find_if(subridgeIter, subridges.end(), [idx](const auto& ele) {return ele->getSourcePointIdx() == idx; });
                        if (subridgeIter == subridges.end())
                            break;

                        auto&& subridge = *subridgeIter;
                        if (auto&& result = std::find_if(oldRidgesData.begin(), oldRidgesData.end(), [subridge](const auto& oldData) {return std::get<0>(oldData) == subridge->getGuid(); }); result == oldRidgesData.end())
                            oldRidgesData.emplace_back((*subridgeIter)->getGuid(), (*subridgeIter)->getControlPoints(), (*subridgeIter)->getSourcePointIdx());

                        // Set new height for subridge root point
                        auto newSubridgePoints = (*subridgeIter)->getControlPoints();
                        newSubridgePoints[0].setY(newSubridgePoints[0].y() + (movementDirection * movement));
                        (*subridgeIter)->moveRidgePoints(newSubridgePoints);

                        // Save subridge height info
                        auto newSubridgeHeights = (*subridgeIter)->getHeights();
                        newSubridgeHeights[0] = newSubridgePoints[0].y();
                        (*subridgeIter)->setHeights(newSubridgeHeights);
                        subridgeIter++;
                    }
                }
            }
        }

        std::vector<float> newHeights(newPoints.size());
        for (int i = 0; i < newPoints.size(); ++i)
            newHeights[i] = newPoints[i].y();

        ridge->setHeights(newHeights);
        ridge->moveRidgePoints(newPoints);
    }

    void Design::StageTools<EGenerationStage::Ridges>::flatten(QSharedPointer<DRidgeMarker> ridge, const std::vector<int>& pointsToEdit)
    {
        float movementDirection = brushStrength * (isKeyDown(VK_SHIFT) ? -1.0f : 1.0f);

        std::unordered_set<int> subridgeSourcePoints;
        auto&& subridges = ridge->getChildren();
        for (auto&& subridge : subridges)
            subridgeSourcePoints.emplace(subridge->getSourcePointIdx());

        auto newPoints = ridge->getControlPoints();
        std::multimap<float, int> distances;

        for (auto&& idx : pointsToEdit)
            if (auto&& distance = GVector2D(newPoints[idx].x(), newPoints[idx].z()).dist(GVector2D(brushOriginPoint->x(), brushOriginPoint->z())); distance <= brushSize * heightBrushSizeFactor)
                distances.emplace(distance, idx);

        float innerHeight = newPoints[distances.begin()->second].y();

        for (auto&& idx : pointsToEdit)
        {
            if (auto&& distance = GVector2D(newPoints[idx].x(), newPoints[idx].z()).dist(GVector2D(brushOriginPoint->x(), brushOriginPoint->z())); distance <= brushSize * heightBrushSizeFactor)
            {
                // Skip root point edition if editing a subridge
                if (idx == 0 && ridge->getParent())
                    continue;

                // Edit points to slowly reach the central point's height
                float movement = (innerHeight - newPoints[idx].y()) * 0.1 * (brushStrength / 20.0f) * (1 - (distance / (brushSize * heightBrushSizeFactor)));
                newPoints[idx].setY(newPoints[idx].y() + (movementDirection * movement));

                if (!subridgeSourcePoints.empty() && subridgeSourcePoints.contains(idx))
                {
                    auto&& subridgeIter = subridges.begin();
                    while (true)
                    {
                        // Save subridge for undo/redo
                        subridgeIter = std::find_if(subridgeIter, subridges.end(), [idx](const auto& ele) {return ele->getSourcePointIdx() == idx; });
                        if (subridgeIter == subridges.end())
                            break;

                        auto&& subridge = *subridgeIter;
                        if (auto&& result = std::find_if(oldRidgesData.begin(), oldRidgesData.end(), [subridge](const auto& oldData) {return std::get<0>(oldData) == subridge->getGuid(); }); result == oldRidgesData.end())
                            oldRidgesData.emplace_back((*subridgeIter)->getGuid(), (*subridgeIter)->getControlPoints(), (*subridgeIter)->getSourcePointIdx());

                        // Set new height for subridge root point
                        auto newSubridgePoints = (*subridgeIter)->getControlPoints();
                        newSubridgePoints[0].setY(newSubridgePoints[0].y() + (movementDirection * movement));
                        (*subridgeIter)->moveRidgePoints(newSubridgePoints);

                        // Save subridge height info
                        auto newSubridgeHeights = (*subridgeIter)->getHeights();
                        newSubridgeHeights[0] = newSubridgePoints[0].y();
                        (*subridgeIter)->setHeights(newSubridgeHeights);
                        subridgeIter++;
                    }
                }
            }
        }

        std::vector<float> newHeights(newPoints.size());
        for (int i = 0; i < newPoints.size(); ++i)
            newHeights[i] = newPoints[i].y();

        ridge->setHeights(newHeights);
        ridge->moveRidgePoints(newPoints);
    }

    void Design::StageTools<EGenerationStage::Ridges>::changeTo3DRidges()
    {
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        for (auto&& ridge : ridges)
            ridge->showAs3D();
    }

    void Design::StageTools<EGenerationStage::Ridges>::changeTo2DRidges()
    {
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        for (auto&& ridge : ridges)
            ridge->showAs2D();
    }

    bool StageTools<EGenerationStage::Ridges>::finishEditing()
    {
        // quid, points, parent idx
        std::vector<std::tuple<qint64, std::vector<QVector3D>, qint64>> newRidgesData;

        HISTORY_PUSH(finishEditing);

        if (!HISTORY_LOAD2(newRidgesData, oldRidgesData))
        {
            for (auto&& oldData : oldRidgesData)
            {
                auto guidChain = treeModel.getRidgeGuidChain(std::get<0>(oldData));
                auto ridge = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(guidChain);

                newRidgesData.emplace_back(std::get<0>(oldData), ridge->getControlPoints(), ridge->getSourcePointIdx());
            }

            HISTORY_SAVE2(newRidgesData, oldRidgesData);
            oldRidgesData.clear();
        }
        // Redo
        else
        {
            for (auto&& newData : newRidgesData)
            {
                auto guidChain = treeModel.getRidgeGuidChain(std::get<0>(newData));
                auto ridge = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(guidChain);

                if (auto result = std::find_if(oldRidgesData.begin(), oldRidgesData.end(), [ridge](const auto& oldData) {return std::get<0>(oldData) == ridge->getGuid(); }); result != oldRidgesData.end())
                {
                    auto&& points = std::get<1>(newData);
                    ridge->moveRidgePoints(points, std::get<1>(newData).size() - std::get<1>(*result).size());
                    std::vector<float> heights(points.size());
                    for (int i = 0; i < points.size(); ++i)
                        heights[i] = points[i].y();

                    ridge->setHeights(heights);
                    ridge->setSourcePointIdx(std::get<2>(newData));
                }
            }
        }

        oldRidgesData.clear();

        return true;
    }

    bool StageTools<EGenerationStage::Ridges>::finishEditing_Undo()
    {
        HISTORY_POP();

        std::vector<std::tuple<qint64, std::vector<QVector3D>, qint64>> newRidgesData;
        HISTORY_LOAD2(newRidgesData, oldRidgesData);

        for (auto&& data : oldRidgesData)
        {
            auto guidChain = treeModel.getRidgeGuidChain(std::get<0>(data));
            auto ridge = Generation::Data::get()->findChildMarkerByGuidChain<DRidgeMarker>(guidChain);

            if (auto result = std::find_if(newRidgesData.begin(), newRidgesData.end(), [ridge](const auto& newData) {return std::get<0>(newData) == ridge->getGuid(); }); result != newRidgesData.end())
            {
                auto&& points = std::get<1>(data);
                ridge->moveRidgePoints(points, std::get<1>(data).size() - std::get<1>(*result).size());
                ridge->setSourcePointIdx(std::get<2>(data));

                std::vector<float> heights(points.size());
                for (int i = 0; i < points.size(); ++i)
                    heights[i] = points[i].y();

                ridge->setHeights(heights);
            }
        }

        oldRidgesData.clear();

        return true;
    }
}