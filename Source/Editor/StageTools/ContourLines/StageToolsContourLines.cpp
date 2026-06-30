#include "stdafx.h"
#include "StageToolsContourLines.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "../StageTools.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include <Source/Scene/Generation/Stages/ContourLines/ContourLines.h>

#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

namespace Design
{
    StageTools<EGenerationStage::ContourLines>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::ContourLines>::getSelectionMgr() const
    {
        return StageToolsBase::getSelectionMgr();
    }

    void StageTools<EGenerationStage::ContourLines>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* genData = Generation::Data::get();
        auto* outline = omnigen->getOutline();
        auto* properties = omnigen->getProperties();

        // Widget setup
        auto* toolbar = createOutlineToolbar();
        QTreeView* dummyOutline = new OutlineTreeView;

        outline->applyTreeStyle(dummyOutline);
        outline->fillSection({ toolbar, dummyOutline });
    }

    void StageTools<EGenerationStage::ContourLines>::unbind()
    {
        StageToolsBase::unbind();
    }

    void StageTools<EGenerationStage::ContourLines>::save(OmniBin<std::ios::out>& writer) const
    {
        writer << gBatchingMarkerInstance<IsohypseBatchParams>;
        writer << isohypseNodes;
        writer << peakPoints;
    }

    void StageTools<EGenerationStage::ContourLines>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& batchedIHs = gBatchingMarkerInstance<IsohypseBatchParams>;
        reader >> batchedIHs;

        auto&& ihBounds = Generation::Data::get()->getMarkers<DIsohypseBound>();

        // Load IH pointers
        auto&& [batches, batchesGuard] = batchedIHs->getBatches();

        std::scoped_lock batchesLock(batchesGuard);
        for (auto&& [params, batch] : batches)
        {
            for (auto&& [offset, ih] : batch.sections)
            {
                std::vector<QSharedPointer<Isohypse>> sourceMarkers;

                auto assignSourceIh = [&](Isohypse** ih, qint64 guid)
                {
                    if (guid == -1)
                        return;

                    auto it = std::find_if(sourceMarkers.begin(), sourceMarkers.end(), [&](auto&& marker) {return marker->getGuid() == guid; });
                    if (it != sourceMarkers.end())
                    {
                        *ih = it->get();
                    }
                    else
                    {
                        if (auto marker = batchedIHs->findSectionByGuid(guid))
                        {
                            sourceMarkers << marker;
                            *ih = marker.get();
                        }
                    }
                };

                for (qint64 parentGuid : ih->getParentGuids())
                    ih->addParent(batchedIHs->findSectionByGuid(parentGuid).get());

                for (auto&& ihs : ih->getSources())
                    assignSourceIh(&const_cast<IHSrcInfo&>(ihs).ih, ihs.ihGuid);

                for (auto&& ihsVec : ih->getPreflow())
                    for (auto&& ihs : ihsVec)
                        assignSourceIh(&const_cast<IHSrcInfoMulti&>(ihs).ih, ihs.ihGuid);

                if (!ih->getDescendants().empty())
                {
                    QSharedPointer<Isohypse> descendingMarker;
                    for (auto&& ihs : ih->getDescendants())
                        if (ihs.ihGuid != -1)
                        {
                            descendingMarker = batchedIHs->findSectionByGuid(ihs.ihGuid);
                            break;
                        }

                    for (auto&& ihs : ih->getDescendants())
                        const_cast<IHSrcInfo&>(ihs).ih = descendingMarker.get();
                }
                
                // cannot use findMarkerByGuid as DIsohypseBound is base class
                for (auto&& [guid, genStage] : ih->data.boundsGuid)
                {
                    auto it = std::find_if(ihBounds.begin(), ihBounds.end(), [guid](auto&& ihB) { return ihB->getGuid() == guid; });
                    Q_ASSERT(it != ihBounds.end());

                    ih->data.bounds[guid] = std::pair(*it, genStage);
                }
            }
        }

        // Show
        Generation::Data::get()->addMarker(batchedIHs);

        reader >> isohypseNodes;
        reader >> peakPoints;
    }


    void StageTools<EGenerationStage::ContourLines>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::ContourLines>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::ContourLines>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::ContourLines>::modifyNode);
    }

    void StageTools<EGenerationStage::ContourLines>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::ContourLines>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::ContourLines>::clearNodes()
    {
        isohypseNodes.clear();
    }

    void StageTools<EGenerationStage::ContourLines>::cleanNodesState()
    {
        std::erase_if(isohypseNodes, [](auto& kv) { return !kv.second->getIsohypse(); });

        for (auto&& [isohypseGuid, isohypseNode] : isohypseNodes)
        {
            isohypseNode->setCreatedOnCurrentStage(false);
            isohypseNode->clearSnapshot();
        }
    }

    void StageTools<EGenerationStage::ContourLines>::updateParentNodes()
    {
        auto&& layoutStageTools = getStageTools<EGenerationStage::Layout>();
        auto&& ridgeStageTools = getStageTools<EGenerationStage::Ridges>();
        auto&& domainSquareNodes = layoutStageTools->getDomainSquareNodes();
        auto&& ridgeNodes = ridgeStageTools->getRidgeNodes();

        for (auto&& [square, squareNode] : domainSquareNodes)
            squareNode->clearIsohypseNodes();

        for (auto&& [guid, ridgeNode] : ridgeNodes)
            ridgeNode->clearIsohypseNodes();

        for (auto&& [isohypseGuid, isohypseNode] : isohypseNodes)
        {
            if (!isohypseNode->getIsohypse())
                continue;

            auto&& isohypse = isohypseNode->getIsohypse();

            for (auto&& ridgeGuid : isohypse->data.ridgeIds)
                ridgeNodes.at(ridgeGuid)->addIsohypseNode(isohypse);

            QSet<GPoint> squares = PolygonUtils::calculateGridSquaresOfPolygon(std::vector<QVector3D>{ isohypse->getVertices().begin(), isohypse->getVertices().end()});
            squares = container_and(squares, Generation::Data::get()->getAllSquares());

            for(auto&& sq : squares)
                domainSquareNodes.at(sq)->addIsohypseNode(isohypse);
        }
    }

    void StageTools<EGenerationStage::ContourLines>::loadSnapshotData()
    {
        disconnectNodes();

        auto&& ihBounds = Generation::Data::get()->getMarkers<DIsohypseBound>();

        auto updateIHProtoData = [ihBounds](IHProtoData* protoData, const IHProtoSnapshotData& snapshotData)
        {
            // sources are updated after adding/removing of IHs
            protoData->boundsReached = snapshotData.boundsReached;
            protoData->slopeFactorForPeakApplied = snapshotData.slopeFactorForPeakApplied;
            protoData->originIdx = snapshotData.originIdx;
            protoData->lowestRidgeTier = snapshotData.lowestRidgeTier;
            protoData->height = snapshotData.height;
            protoData->mergeDistanceMult = snapshotData.mergeDistanceMult;
            protoData->heightAtHalfOfDistanceToBase = snapshotData.heightAtHalfOfDistanceToBase;
            protoData->distanceToBase = snapshotData.distanceToBase;
            protoData->groupingFactor = snapshotData.groupingFactor;
            protoData->usedDomainId = snapshotData.usedDomainId;
            protoData->modifiedBy = snapshotData.modifiedBy;
            for (auto&& [guid, genStage] : snapshotData.bounds)
            {
                auto it = std::find_if(ihBounds.begin(), ihBounds.end(), [guid](auto&& ihB) { return ihB->getGuid() == guid; });
                Q_ASSERT(it != ihBounds.end());

                protoData->bounds[guid] = std::pair(*it, genStage);
            }
            protoData->mergeIhlevels = snapshotData.mergeIhlevels;
            protoData->affectedBy = snapshotData.affectedBy;
            protoData->mergedDomains = snapshotData.mergedDomains;
            protoData->ridgeIds = snapshotData.ridgeIds;
            protoData->ridgelineSources = snapshotData.ridgelineSources;
            protoData->mergeThreshold = snapshotData.mergeThreshold;
            protoData->currentDropLvl = snapshotData.currentDropLvl;
            protoData->desiredDropLvl = snapshotData.desiredDropLvl;
            protoData->tablelandType = snapshotData.tablelandType;
        };

        for (auto it = isohypseNodes.begin(); it != isohypseNodes.end();)
        {
            auto&& [isohypseGuid, isohypseNode] = *it;

            if (auto&& snapshot = isohypseNode->getSnapshot())
            {
                // removed isohypse
                if (!isohypseNode->getIsohypse())
                {
                    auto newIsohypse = spawnBatched(std::move(buildLineGeometry(snapshot->points, true)), snapshot->batchParams, isohypseGuid);
                    newIsohypse->level = snapshot->level;
                    updateIHProtoData(&newIsohypse->data, snapshot->data);

                    isohypseNode->setIsohypse(newIsohypse);
                }
                // modified isohypse
                else
                {
                    auto&& isohypseToUpdate = isohypseNode->getIsohypse();
                    isohypseToUpdate->level = snapshot->level;
                    updateIHProtoData(&isohypseToUpdate->data, snapshot->data);

                    isohypseToUpdate->setGeometry(std::move(buildLineGeometry(snapshot->points, true)));
                }
            }
            // added isohypse
            else if (isohypseNode->isCreatedOnCurrentStage())
            {
                despawnBatched(isohypseNode->getIsohypse());
                it = isohypseNodes.erase(it);
                continue;
            }

            it++;
        }

        // update isohypse relations data
        for (auto&& [isohypseGuid, isohypseNode] : isohypseNodes)
            if (auto&& snapshot = isohypseNode->getSnapshot())
            {
                auto&& isohypse = isohypseNode->getIsohypse();

                for (auto&& parentGuid : snapshot->parentGuids)
                    isohypse->addParent(isohypseNodes[parentGuid]->getIsohypse().data());

                for (auto&& [sourceGuid, idx] : snapshot->data.sources)
                    isohypse->data.sources.push_back(IHSrcInfo{.ih = (sourceGuid != -1 ? isohypseNodes[sourceGuid]->getIsohypse().data() : nullptr), .idx = idx});

                for (auto&& [desceGuid, idx] : snapshot->descendants)
                    isohypse->descendants.push_back(IHSrcInfo{ .ih = (desceGuid != -1 ? isohypseNodes[desceGuid]->getIsohypse().data() : nullptr), .idx = idx });

                for (auto&& preflow : snapshot->preflow)
                {
                    std::vector<IHSrcInfoMulti> srcInfos;

                    for (auto&& [preflowGuid, indices] : preflow)
                    {
                        IHSrcInfoMulti srcInfo;
                        srcInfo.ih = (preflowGuid != -1 ? isohypseNodes[preflowGuid]->getIsohypse().data() : nullptr);
                        srcInfo.indices = indices;

                        srcInfos.push_back(srcInfo);
                    }

                    isohypse->preflow.push_back(srcInfos);
                }

                isohypseNode->clearSnapshot();
            }

        connectNodes();
    }

    bool StageTools<EGenerationStage::ContourLines>::validatePipeline()
    {
        for (auto&& [isohypseGuid, isohypseNode] : isohypseNodes)
        {
            // removed isohypse
            if (!isohypseNode->getIsohypse())
                return false;
            // added isohypse
            else if (isohypseNode->isCreatedOnCurrentStage())
                return false;
            // modified isohypse
            else if (isohypseNode->getSnapshot())
                return false;
        }

        return true;
    }

    std::unordered_map<GPoint, std::vector<QVector3D>> findAffectedPolygonsPerSquare(const QSet<GPoint>& partialyAffectedSquares, const std::unordered_set<qint64>& affectedIHs)
    {
        auto&& ihQtree = gBatchingMarkerInstance<IsohypseBatchParams>->getQuadTree();
        auto findLowestAffectedIHs = [&](const GPoint& sq)
        {
            std::unordered_set<Isohypse*> lowestIHs;

            static const float r = GRID_SEGMENT_WIDTH * M_SQRT2;
            auto&& p = sq.midPoint();
            auto nodes = ihQtree.map_all_nearest(p.x, p.z, r);

            std::unordered_map<qint64, std::unordered_set<Isohypse*>> ihPerRidge;

            for (auto&& [dist, result] : nodes)
                if (auto&& ih = result->data.section; !affectedIHs.contains(ih->getGuid()))
                    for (auto&& ridge : ih->data.ridgeIds)
                        ihPerRidge[ridge] += ih;

            // bigger level == lower ih
            for (auto&& [_, ihs] : ihPerRidge)
                lowestIHs += *std::max_element(ihs.begin(), ihs.end(), [](auto&& ih1, auto&& ih2) { return ih1->level < ih2->level; });

            return lowestIHs;
        };


        std::unordered_map<GPoint, std::vector<QVector3D>> affectedPolygonPerSquare;

        for (auto&& sq : partialyAffectedSquares)
        {
            std::vector<QVector3D> affectedPolygon{
                {sq.x * GRID_SEGMENT_WIDTH, 0, sq.z * GRID_SEGMENT_WIDTH},
                {sq.x * GRID_SEGMENT_WIDTH, 0, (sq.z + 1) * GRID_SEGMENT_WIDTH},
                {(sq.x + 1) * GRID_SEGMENT_WIDTH, 0, (sq.z + 1) * GRID_SEGMENT_WIDTH},
                {(sq.x + 1) * GRID_SEGMENT_WIDTH, 0, sq.z * GRID_SEGMENT_WIDTH} };

            std::vector<std::vector<QVector3D>> ihPolygons;

            auto&& affectedIHs = findLowestAffectedIHs(sq);
            for (auto&& ih : affectedIHs)
                ihPolygons << ih->getPoints();

            if (auto result = PolygonUtils::cutPolygon(affectedPolygon, ihPolygons); !result.empty())
                affectedPolygonPerSquare[sq] = result.front();
        }

        return affectedPolygonPerSquare;
    }

    void StageTools<EGenerationStage::ContourLines>::updatePipeline()
    {
        auto&& terrainModelStageTools = getStageTools<EGenerationStage::TerrainModel>();
        auto&& demPointNodes = terrainModelStageTools->getDEMPointNodes();
        auto&& instance = gBatchingMarkerInstance<IsohypseBatchParams>;
        auto&& dem = Generation::Data::get()->getDEM();

        if (instance)
        {
            connectNodes();
            ignoreModifiedNodes = true;
            ContourLines::regenerateStack();
            ContourLines::generate();
            ignoreModifiedNodes = false;
            disconnectNodes();
        }

        std::unordered_set<qint64> affectedIHs;
        QSet<GPoint> partialyAffectedSquares;
        QSet<GPoint> affectedSquares;
        QSet<GPoint> unaffectedSquares;

        QSet<GPoint> ihSquares;

        for (auto&& [isohypseGuid, isohypseNode] : isohypseNodes)
        {
            auto&& vertices = isohypseNode->getIsohypse() ? isohypseNode->getIsohypse()->getPoints() : isohypseNode->getSnapshot()->points;

            if (isohypseNode->getSnapshot() || isohypseNode->isCreatedOnCurrentStage())
            {
                affectedIHs += isohypseGuid;
                ihSquares += PolygonUtils::calculateGridSquaresOfPolygon(vertices, true);
                affectedSquares += PolygonUtils::calculateGridSquaresOfPolygon(vertices);
            }
            else
                unaffectedSquares += PolygonUtils::calculateGridSquaresOfPolygon(vertices);
        }
        affectedSquares -= unaffectedSquares;
        partialyAffectedSquares = container_and(unaffectedSquares, ihSquares);
        unaffectedSquares -= partialyAffectedSquares;
        partialyAffectedSquares -= unaffectedSquares;


        // ensure only valid squares are considered
        auto&& allSquares = Generation::Data::get()->getAllSquares();
        partialyAffectedSquares = container_and(partialyAffectedSquares, allSquares);
        affectedSquares = container_and(affectedSquares, allSquares);

        if (dem)
        {
            // Find all modified dem points
            std::unordered_set<GVector2D> modifiedDEMPoints;

            for (auto&& sq : affectedSquares)
                dem->forEachPoint(sq, [&](const GVector2D& pt)
                    {
                        modifiedDEMPoints += pt;
                    });

            auto&& affectedPolygonPerSquare = findAffectedPolygonsPerSquare(partialyAffectedSquares, affectedIHs);

            tbb::spin_mutex pushGuard;
            std::vector<GPoint> partialyAffectedSquaresVec(partialyAffectedSquares.begin(), partialyAffectedSquares.end());
            tbb::parallel_for(0, int(partialyAffectedSquaresVec.size()), [&](int i)
                {
                    auto&& sq = partialyAffectedSquaresVec[i];
                    if (!affectedPolygonPerSquare.contains(sq))
                        return;

                    auto&& affectedPoly = affectedPolygonPerSquare[sq];

                    std::unordered_set<GVector2D> modifiedDEMPointsPerSuqare;
                    dem->forEachPoint(sq, [&](const GVector2D& pt)
                        {
                            if (PolygonUtils::contains(pt, affectedPoly))
                                modifiedDEMPointsPerSuqare += pt;
                        });

                    {
                        std::scoped_lock lock(pushGuard);
                        modifiedDEMPoints += modifiedDEMPointsPerSuqare;
                    }
                });

            auto&& updateInfo = QSharedPointer<Generation::DEMUpdateInfo>::create();
            updateInfo->points = std::vector<GVector2D>(modifiedDEMPoints.begin(), modifiedDEMPoints.end());
            Generation::Data::get()->getDEM()->loadFromIHs(updateInfo->points);
            terrainModelStageTools->modifyNode(updateInfo);

            dem->heightData.update();
            dem->verticalDisplacementXCoords.update();
        }

        terrainModelStageTools->updatePipeline();
        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::ContourLines>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        if (auto&& isohypse = object.dynamicCast<Isohypse>(); isohypse && !isohypseNodes.contains(isohypse->getGuid()))
            isohypseNodes[isohypse->getGuid()] = QSharedPointer<IsohypseNode>::create(isohypse);
    }

    void StageTools<EGenerationStage::ContourLines>::removeNode(QSharedPointer<Editable> object)
    {
        if (auto&& isohypse = object.dynamicCast<Isohypse>(); isohypse && isohypseNodes.contains(isohypse->getGuid()))
        {
            if (isohypseNodes[isohypse->getGuid()]->isCreatedOnCurrentStage())
                isohypseNodes.erase(isohypse->getGuid());
            else
            {
                if (!isohypseNodes[isohypse->getGuid()]->getSnapshot())
                    isohypseNodes[isohypse->getGuid()]->makeSnapshot();

                isohypseNodes[isohypse->getGuid()]->nullifyIsohypse();
            }
        }
    }

    void StageTools<EGenerationStage::ContourLines>::modifyNode(QSharedPointer<Editable> object)
    {
        if (ignoreModifiedNodes)
            return;

        if (auto&& isohypse = object.dynamicCast<Isohypse>(); isohypse && isohypseNodes.contains(isohypse->getGuid()) && !isohypseNodes[isohypse->getGuid()]->isCreatedOnCurrentStage() && !isohypseNodes[isohypse->getGuid()]->getSnapshot())
            isohypseNodes[isohypse->getGuid()]->makeSnapshot();
    }

    void StageTools<EGenerationStage::ContourLines>::setPeakPoints(const std::vector<QVector3D>& peaks)
    {
        peakPoints = peaks; 
    }
}