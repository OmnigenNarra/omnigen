#include "stdafx.h"
#include "ContourLines.h"
#include "Omnigen.h"
#include "../Ridges/RidgeMarker.h"
#include "../Landmasses/ShorelineMarker.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/CircularVectorView.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Utils/OmnigenDirectCompute.h"
#include "Utils/Polygon.h"
#include "IsohypseBatchingMarker.h"
#include "Editor/StageTools/StageTools.h"

#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

struct OverlapOutput
{
    int h1pts[2] = { -1, -1 };
    int h2pts[2] = { -1, -1 };
};

// Sync with IHMergeRecognition.hlsl : MAX_OVERLAP_WIDTH !!!
#define MAX_OVERLAP_WIDTH 64

#define DEBUG_PREFLOW 0
#define DEBUG_LOCAL_MIN 0
#define DEBUG_IH_MERGES 0
#define DEBUG_HEIGHT_BOUNDS 0
#define DEBUG_IH_HEIGHT_GRAPH 0
#define DEBUG_IH_RIDGE_FOLLOW 0
#define DEBUG_IH_FIX 0
#define DEBUG_2D_VIEW 0

#if DEBUG_HEIGHT_BOUNDS
std::vector<QPair<Segment2D, float>> debugDistances;
tbb::spin_mutex debugDistancesGuard;
#endif

void ContourLines::prepareStack()
{
    auto ridgeRoots = Generation::Data::get()->getMarkers<DRidgeMarker>();
    Q_ASSERT(!ridgeRoots.empty());

    auto heightBounds = Generation::Data::get()->getDomainHeightBounds();

    // Add ridges to heightbounds
    std::vector<QSharedPointer<DRidgeMarker>> ridges;
    Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

    for (auto&& ridge : ridges)
    {
        auto&& cPts = ridge->getControlPoints();
        auto&& heights = ridge->getHeights();
        auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(cPts[0]).toGPoint(), EDomainType::Terrain);

        for (int i = 0; i < cPts.size() - 1; ++i)
        {
            int height = (heights[i] + heights[i + 1]) / 2;
            heightBounds[domain->getGuid()][Generation::HeightBoundOrigin::Ridge][ridge->getGuid()][height].emplace_back(Segment2D(cPts[i], cPts[i + 1]));
        }
    }

    Generation::Data::get()->setDomainHeightBounds(heightBounds);
    heightBoundsqtree = computeHeightBoundsQtree(heightBounds);

#if DEBUG_LOCAL_MIN
    for (auto&& [ignore, domain] : Generation::Data::get()->getAllDomains())
    {
        float minHeight = domain->getData<EDomainType::Terrain>()->minHeight;

        auto height = QVector3D(0, minHeight, 0);
        for (auto&& segment : domain->getPerimeter())
            Generation::Data::get()->createMarker<DLineMarker>(std::vector<QVector3D>{height + segment.first, height + segment.second}, QVector4D(0.f, 0.f, 1, 1));
    }
#endif

    auto primeIhs = generateLevels0And1(ridgeRoots, heightBounds);

    for (auto&& it = primeIhs.begin(); it != primeIhs.end(); ++it)
    {
        auto&& cpts = it->pts;
        for (int i = 0; i < cpts.size(); ++i)
        {
            int j = i == cpts.size() - 1 ? 0 : i + 1;
            auto&& domainId = it->usedDomainId;
            auto&& ihGuid = it->ptr->getGuid();
            auto&& height = it.key();

            heightBounds[domainId][Generation::HeightBoundOrigin::Isohypse][ihGuid][height].emplace_back(Segment2D(cpts[i], cpts[j]));
        }
    }

    Generation::Data::get()->setDomainHeightBounds(heightBounds);
    heightBoundsqtree = computeHeightBoundsQtree(heightBounds);

    ihStack.clear();
    for (auto&& ihInfo : primeIhs)
        ihStack.push(ihInfo);
}

void ContourLines::regenerateStack()
{
    auto&& ihMarkers = Generation::Data::get()->getIsohypseMarkersByLevel();
    auto heightBounds = Generation::Data::get()->getDomainHeightBounds();
    ihStack.clear();
    std::vector<QSharedPointer<Isohypse>> lastIsohypses;
    std::unordered_set<qint64> sourceRidges;

    // Find final isohypses (with no descendants) and save root ridges used
    for (int i = ihMarkers.size() - 1; i >= 0; --i)
    {
        for(auto&& marker : ihMarkers[i])
        {
            std::unordered_set<qint64> ridges = marker->data.ridgeIds;
            sourceRidges.merge(ridges);
            if (marker->level == 0)
                continue;

            auto&& checkDescendants = [&]() 
            {
                if (auto&& desc = marker->getDescendants(); desc.size() > 1)
                {
                    for (auto&& d : desc)
                        if (d.ih != nullptr && d.idx != -1 && d.ih->descendants.size() > 0)
                            return false;
                }

                return true;
            };

            if(checkDescendants())
                lastIsohypses.emplace_back(marker);
        }
    }

    // Create IHProtoData for final isohypses
    for (auto&& isohypse : lastIsohypses)
    {
        auto&& isohypseData = isohypse->data;

        Q_ASSERT(isohypse->getLevel() > 0);

        IHProtoData data;
        data.originIdx = isohypseData.originIdx;
        data.lowestRidgeTier = isohypseData.lowestRidgeTier;
        data.height = isohypseData.height;
        data.mergeDistanceMult = isohypseData.mergeDistanceMult;
        data.heightAtHalfOfDistanceToBase = isohypseData.heightAtHalfOfDistanceToBase;
        data.distanceToBase = isohypseData.distanceToBase;
        data.groupingFactor = isohypseData.groupingFactor;
        data.usedDomainId = isohypseData.usedDomainId;
        data.ptr = isohypse;
        auto&& verticesSpan = isohypse->getVertices();
        data.pts = std::vector<QVector3D>(verticesSpan.begin(), verticesSpan.end());
        data.sources = isohypseData.sources;

        std::vector<float> increments(data.pts.size());
        for (int i = 0; i < increments.size(); ++i)
        {
            auto&& verticesSpan = data.sources[i].ih->getVertices();
            auto&& sourcePoints = std::vector<QVector3D>(verticesSpan.begin(), verticesSpan.end());
            increments[i] = sourcePoints[data.sources[i].idx].distanceToPoint(data.pts[i]);
        }

        data.increments = increments;
        data.modifiedBy = isohypseData.modifiedBy;
        data.bounds = isohypseData.bounds;
        data.boundsGuid = isohypseData.boundsGuid;
        std::vector<float> heightDeltas(isohypse->getLevel(), fixedHeightIncrement);
        data.heightDeltas = heightDeltas;
        data.mergeIhlevels = isohypseData.mergeIhlevels;
        data.affectedBy = isohypseData.affectedBy;
        data.mergedDomains = isohypseData.mergedDomains;
        data.parentIhs = isohypseData.parentIhs;
        data.ridgeIds = isohypseData.ridgeIds;
        data.ridgelineSources = isohypseData.ridgelineSources;
        data.mergeThreshold = isohypseData.mergeThreshold;
        data.currentDropLvl = isohypseData.currentDropLvl;
        data.desiredDropLvl = isohypseData.desiredDropLvl;
        data.tablelandType = isohypseData.tablelandType;
        data.regenerated = true;

        ihStack.push(data);
    }

    // Clear the height bounds of any leftover ridges or isohypses
    for (auto&& [domain, boundMap] : heightBounds)
    {
        heightBounds[domain].erase(Generation::HeightBoundOrigin::Ridge);
        heightBounds[domain].erase(Generation::HeightBoundOrigin::Isohypse);
    }

    // Add ridges to heighbounds
    std::vector<QSharedPointer<DRidgeMarker>> ridges;
    Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

    for (auto&& ridge : ridges)
    {
        auto&& cPts = ridge->getControlPoints();
        auto&& heights = ridge->getHeights();
        auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(cPts[0]).toGPoint(), EDomainType::Terrain);

        for (int i = 0; i < cPts.size() - 1; ++i)
        {
            int height = (heights[i] + heights[i + 1]) / 2;
            heightBounds[domain->getGuid()][Generation::HeightBoundOrigin::Ridge][ridge->getGuid()][height].emplace_back(Segment2D(cPts[i], cPts[i + 1]));
        }
    }

    // Generate lvl 0 and 1 for any new ridgelines
    auto&& rootRidges = Generation::Data::get()->getMarkers<DRidgeMarker>();
    std::vector<QSharedPointer<DRidgeMarker>> rootRidgesToGenerate;
    for (auto&& ridge : rootRidges)
        if (!sourceRidges.contains(ridge->getGuid()))
            rootRidgesToGenerate.emplace_back(ridge);

    heightBoundsqtree = computeHeightBoundsQtree(heightBounds);
    Generation::Data::get()->setDomainHeightBounds(heightBounds);

    if (!rootRidgesToGenerate.empty())
    {
        auto&& newProtoData = generateLevels0And1(rootRidgesToGenerate, heightBounds);
        for (auto&& ihInfo : newProtoData)
            ihStack.push(ihInfo);
    }

    for (auto&& data : ihStack)
    {
        // Add the isohypse as a heightBound
        for (int i = 0; i < data.pts.size(); ++i)
        {
            int j = i == data.pts.size() - 1 ? 0 : i + 1;
            heightBounds[data.usedDomainId][Generation::HeightBoundOrigin::Isohypse][data.ptr->getGuid()][data.height].emplace_back(Segment2D(data.pts[i], data.pts[j]));
        }
    }

    heightBoundsqtree = computeHeightBoundsQtree(heightBounds);
    Generation::Data::get()->setDomainHeightBounds(heightBounds);
}

bool ContourLines::generate()
{
    OmniProfile("Isohypses");
    auto&& heightBounds = Generation::Data::get()->getDomainHeightBounds();

    int levelIdx = 1;
    while (true)
    {
        auto [growing, mergingOnly] = chooseIhsToGrow();
        QMultiMap<float, IHProtoData> newIhs = generateNextIHs(growing, &mergingOnly, heightBounds, ++levelIdx);

        if (newIhs.isEmpty())
            break;

        for (auto&& ihInfo : newIhs)
            ihStack.push(ihInfo);
    };

    Generation::Data::get()->initializeQueuedMarkers();

    computePreflow();
    ihMerges.clear();
    gBatchingMarkerInstance<IsohypseBatchParams>->resetQuadTree();
    return true;
}

std::map<quint64, float> ContourLines::findLocalMinPerDomain(const std::vector<QSharedPointer<DRidgeMarker>>& ridgeRoots)
{
    auto&& heightBounds = Generation::Data::get()->getDomainHeightBounds();

    std::map<quint64, float> localMinPerDomain;

    for (auto&& [ignore, domain] : Generation::Data::get()->getAllDomains())
    {
        auto id = domain->getGuid();   
        auto maxHeight = domain->getData<EDomainType::Terrain>()->maxHeight;
        auto estimatedLocalMin = maxHeight * 0.3f;

        if (heightBounds.contains(id))
        {
            int minHeightBound = std::numeric_limits<int>::max();
            for (auto&& [_, boundsPerType] : heightBounds.at(id))
                for (auto&& [_, bounds] : boundsPerType)
                    minHeightBound = std::min(minHeightBound, bounds.begin()->first);
            localMinPerDomain[id] = maxHeight >= minHeightBound ? minHeightBound : estimatedLocalMin;
        }
        else
            localMinPerDomain[id] = estimatedLocalMin;
    }

    return localMinPerDomain;
}

QMultiMap<float, IHProtoData> ContourLines::generateLevels0And1(const std::vector<QSharedPointer<DRidgeMarker>>& ridgeRoots, const std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>& heightBounds)
{
    auto isohypseBounds = Generation::Data::get()->getMarkers<DIsohypseBound>();
    std::vector<IHProtoData> level0data;
    int idx = -1;

    // Get level 0: Ridges
    for (auto&& ridgeRoot : ridgeRoots)
    {
        GPoint sq = GVector2D(ridgeRoot->getControlPoints().front()).toGPoint();
        auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);

        ELandform landform = domain->getData<EDomainType::Terrain>()->landform;
        auto guid = ridgeRoot->getGuid();

        int treeTier = 0;
        std::vector<QSharedPointer<DRidgeMarker>> children = ridgeRoot->getChildren();
        std::unordered_map<int, std::vector<QSharedPointer<DRidgeMarker>>> ridgeTree;
        ridgeTree[treeTier] = { ridgeRoot };

        // Gather the root ridge and all its subridges into a ridgeline tree
        while (true)
        {
            std::vector<QSharedPointer<DRidgeMarker>> nextChildren;
            for (auto&& r : ridgeTree[treeTier])
                if (auto&& newChildren = r->getChildren(); !newChildren.empty())
                    nextChildren.insert(nextChildren.end(), std::make_move_iterator(newChildren.begin()), std::make_move_iterator(newChildren.end()));

            treeTier++;

            if (!nextChildren.empty())
                ridgeTree.emplace(treeTier, nextChildren);
            else
                break;
        }

        std::set<std::vector<QVector3D>> peakMap;

        for (int tier = 0; tier < ridgeTree.size(); ++tier)
        {
            for(auto&& ridge : ridgeTree[tier])
            {
                auto&& peaks = findPeaks(ridge, peakMap);

                for(auto&& [peakHeight, peakPts] : peaks)
                {
                    peakMap.emplace(peakPts);

                    IHProtoData data;
                    data.originIdx = ++idx;
                    data.pts = peakPts;
                    data.increments.resize(peakPts.size());
                    data.sources.resize(peakPts.size());
                    data.modifiedBy.resize(peakPts.size(), { 1.0f , true});
                    data.boundsReached = false;
                    data.height = (int(peakHeight) / int(fixedHeightIncrement)) * fixedHeightIncrement;
                    data.ridgeIds = { ridgeRoot->getGuid() };
                    data.lowestRidgeTier = tier;
                    data.usedDomainId = domain->getGuid();
                    data.mergedDomains = { domain->getGuid() };
                    data.tablelandType = ridge->getTablelandType();

                    // Pair isohypse markers with their bounds
                    for (auto&& bound : isohypseBounds)
                    {
                        auto&& boundPoints = bound->getControlPoints();
                        if (isRidgeInsideBound(peakPts, boundPoints))
                        {
                            // At this point, each isohypse can only have one bound
                            data.bounds[bound->getGuid()] = { bound, EIHGenerationStage::Init };
                            break;
                        }
                    }

                    level0data << data;
                }
            }
        }
    }

#if DEBUG_IH_HEIGHT_GRAPH
    QVector3D h(0, 1000, 0);
    for (int node = 0; node < heightGraphNodes.size(); node++)
        for (auto&& [edgeNode, edgeData] : heightGraphNodes[node].edges)
            Generation::Data::get()->createMarker<DLineMarker>(std::vector<QVector3D>{heightGraphNodes[node].pos + h, heightGraphNodes[edgeNode].pos + h}, QVector4D(0, 0, 1, 1));
#endif

    std::vector<QVector3D> peakCentralPoints;

    for (int i = 0; i < level0data.size(); i++)
    {
        auto landformVariation = (*Generation::Data::get()->findDomainByGuid(level0data[i].usedDomainId))->getData<EDomainType::Terrain>()->landformVariation;
        auto [distanceToBase, heightAtHalf] = Landform::calculateCurveDefiningIsohypseParameters(level0data[i].height, landformVariation);

        level0data[i].distanceToBase = distanceToBase;
        level0data[i].heightAtHalfOfDistanceToBase = heightAtHalf;
        level0data[i].mergeIhlevels = {};

        auto [ignore, newMergeDistanceMult] = Landform::computeNextIHParams(level0data[i]);
        level0data[i].heightDeltas = { };
        level0data[i].mergeDistanceMult = newMergeDistanceMult;

        level0data[i].ptr = spawnBatchedIH(level0data[i], 0);
        Q_ASSERT(level0data[i].ptr);

        auto& allPoints = level0data[i].pts;
        QVector3D peakCenter = (std::accumulate(allPoints.begin(), allPoints.end(), QVector3D(0, 0, 0))) / allPoints.size();
        peakCenter.setY(level0data[i].height);
        peakCentralPoints.push_back(peakCenter);
    }

    auto&& clTools = getStageTools<EGenerationStage::ContourLines>();
    clTools->setPeakPoints(peakCentralPoints);

    // Generate level 1
    static std::vector<IHProtoData> dummyOther;
    Q_ASSERT(!level0data.empty());

    return generateNextIHs(level0data, &dummyOther, heightBounds, 1);
}

std::unordered_multimap<float, std::vector<QVector3D>> ContourLines::findPeaks(const QSharedPointer<DRidgeMarker>& ridge, const std::set<std::vector<QVector3D>>& peakMap)
{
    std::unordered_multimap<float, std::vector<std::pair<QVector3D, IndexType/*old idx*/>>> peaks;
    auto&& heights = ridge->getHeights();
    auto&& allPoints = ridge->getControlPoints();
    std::unordered_set<GVector2D> claimedPoints;
    float minimumDistance = baseIncrement * 4;
    float peakMargin = 0.01f;

    auto&& ridgeChildren = ridge->getChildren();
    std::unordered_multimap<int, QSharedPointer<DRidgeMarker>> subridgeSourcePoints;
    for (auto&& child : ridgeChildren)
        subridgeSourcePoints.emplace(child->getSourcePointIdx(), child);

    std::multimap<float, int> heightMap;
    for (int i = 0; i < heights.size(); ++i)
        heightMap.emplace(heights[i], i);

    std::unordered_set<int> subridgeRoot;
    if (ridge->getParent())
    {
        subridgeRoot.emplace(0);
        for (int i = 1; i < allPoints.size(); ++i)
        {
            if (GVector2D(allPoints[i]).dist(GVector2D(allPoints[0])) < minimumDistance)
                subridgeRoot.emplace(i);
            else
                break;
        }
    }

    for (auto&& it = heightMap.rbegin(); it != heightMap.rend(); ++it)
    {
        int peakIdx = it->second;
        bool peakIsFlat = true;
        std::unordered_set<GVector2D> newClaims;
        std::vector<QVector3D> potentialPointsToMerge;

        // Disallow peaks at subridge root
        if (subridgeRoot.contains(peakIdx))
            continue;

        // Skip peak point if it is already claimed by another peak
        if (claimedPoints.contains(GVector2D(allPoints[peakIdx])))
            continue;

        // Guarantee that peaks are highest local points, and have minimum space
        if (peakIdx != 0)
            if (heights[peakIdx - 1] > it->first || claimedPoints.contains(GVector2D(allPoints[peakIdx])) || subridgeRoot.contains(peakIdx - 1))
                continue;

        if (peakIdx != heights.size() - 1)
            if (heights[peakIdx + 1] > it->first || claimedPoints.contains(GVector2D(allPoints[peakIdx])))
                continue;

        float peakHeight = it->first;
        std::vector<std::pair<QVector3D, IndexType/*old idx*/>> peakPoints = {std::pair<QVector3D, IndexType>(QVector3D(allPoints[peakIdx].x(), 0.0f, allPoints[peakIdx].z()), it->second)};
        newClaims.emplace(GVector2D(allPoints[peakIdx]));

        auto processPeak = [&]() 
        {
            // Check forward if points can be added to peakPoints, or close peaks merged
            for (int i = peakIdx + 1; i < allPoints.size(); ++i)
            {
                if (heights[i] < peakHeight)
                    peakIsFlat = false;

                if (!potentialPointsToMerge.empty() && GVector2D(peakPoints.back().first).dist(GVector2D(allPoints[i])) >= minimumDistance)
                {
                    potentialPointsToMerge.clear();
                    break;
                }
                // Merge Peaks
                else if (claimedPoints.contains(GVector2D(allPoints[i])))
                {
                    // this prevents a situation where a flat 'peak' would go all the way to another peaks foot and merge with it
                    if (peakIsFlat)
                        return false;

                    // Insert all points between peaks
                    if (!potentialPointsToMerge.empty())
                    {
                        std::vector<QVector3D> tempPoints(potentialPointsToMerge.size());
                        for (int j = 0; j < potentialPointsToMerge.size(); ++j)
                        {
                            newClaims.emplace(potentialPointsToMerge[j].x(), potentialPointsToMerge[j].z());
                            tempPoints[j] = QVector3D(potentialPointsToMerge[j].x(), 0.0f, potentialPointsToMerge[j].z());
                        }
                        potentialPointsToMerge = tempPoints;
                    }

                    for (auto&& point : potentialPointsToMerge)
                        peakPoints.emplace_back(point, i);

                    auto&& peakToMerge = std::find_if(peaks.begin(), peaks.end(), [&](const auto& peakData) {return QVector3D(allPoints[i].x(), 0.0f, allPoints[i].z()) == peakData.second.front().first; });
                    Q_ASSERT(peakToMerge != peaks.end());

                    peakPoints.insert(peakPoints.end(), peakToMerge->second.begin(), peakToMerge->second.end());
                    peakHeight = (peakHeight + peakToMerge->first) / 2;

                    for (auto&& [pt, unused] : peakPoints)
                        newClaims.emplace(pt.x(), pt.z());

                    peaks.erase(peakToMerge);
                    break;
                }

                // Check if other points are close enough to the peak margin to add them to peak points
                if (potentialPointsToMerge.empty())
                {
                    if (i == peakIdx + 1 || fCmp(heights[i], peakHeight, peakMargin) == std::strong_ordering::equal)
                    {
                        peakPoints.emplace_back(QVector3D(allPoints[i].x(), 0.0f, allPoints[i].z()), i);
                        newClaims.emplace(GVector2D(allPoints[i]));
                        continue;
                    }
                    // if a point is higher than current peak, invalidate this peak
                    else if (heights[i] > peakHeight)
                    {
                        return false;
                    }
                }

                potentialPointsToMerge.emplace_back(allPoints[i]);
            }

            peakIsFlat = true;

            // Check backward if points can be added to peakPoints, or close peaks merged
            for (int i = peakIdx - 1; i >= 0; --i)
            {
                if (heights[i] < peakHeight)
                    peakIsFlat = false;

                // Disallow peaks too close to subridge root
                if (subridgeRoot.contains(i))
                {
                    potentialPointsToMerge.clear();
                    break;
                }

                if (!potentialPointsToMerge.empty() && GVector2D(peakPoints.front().first).dist(GVector2D(allPoints[i])) >= minimumDistance)
                {
                    potentialPointsToMerge.clear();
                    break;
                }
                // Merge Peaks
                else if (claimedPoints.contains(GVector2D(allPoints[i])))
                {
                    // this prevents a situation where a flat 'peak' would go all the way to another peaks foot and merge with it
                    if (peakIsFlat)
                        return false;

                    // Insert all points between peaks
                    if (!potentialPointsToMerge.empty())
                    {
                        std::vector<QVector3D> tempPoints(potentialPointsToMerge.size());
                        for (int j = 0; j < potentialPointsToMerge.size(); ++j)
                        {
                            newClaims.emplace(GVector2D(potentialPointsToMerge[j]));
                            tempPoints[j] = QVector3D(potentialPointsToMerge[j].x(), 0.0f, potentialPointsToMerge[j].z());
                        }
                        potentialPointsToMerge = tempPoints;
                    }

                    for (auto&& [point, unused] : peakPoints)
                        potentialPointsToMerge.emplace_back(point);

                    auto&& peakToMerge = std::find_if(peaks.begin(), peaks.end(), [&](const auto& peakData) {return QVector3D(allPoints[i].x(), 0.0f, allPoints[i].z()) == peakData.second.back().first; });
                    Q_ASSERT(peakToMerge != peaks.end());

                    peakPoints = peakToMerge->second;
                    for (auto&& point : potentialPointsToMerge)
                        peakPoints.emplace_back(point, i);

                    peakHeight = (peakHeight + peakToMerge->first) / 2;

                    for (auto&& [pt, unused] : peakPoints)
                        newClaims.emplace(GVector2D(pt));

                    peaks.erase(peakToMerge);
                    break;
                }

                // Check if other points are close enough to the peak margin to add them to peak points
                if (potentialPointsToMerge.empty())
                {
                    if (i == peakIdx - 1 || fCmp(heights[i], peakHeight, peakMargin) == std::strong_ordering::equal)
                    {
                        std::vector<std::pair<QVector3D, IndexType>> tempVec = {std::pair<QVector3D, IndexType>(QVector3D(allPoints[i].x(), 0.0f, allPoints[i].z()), i)};
                        tempVec.insert(tempVec.end(), std::make_move_iterator(peakPoints.begin()), std::make_move_iterator(peakPoints.end()));
                        peakPoints = tempVec;
                        newClaims.emplace(GVector2D(allPoints[i]));
                        continue;
                    }
                    else if (heights[i] > peakHeight)
                    {
                        return false;
                    }
                }

                potentialPointsToMerge.emplace_back(allPoints[i]);
            }

            // Distance check against all other peaks
            for (auto&& [point, unused] : peakPoints)
            {
                GVector2D pointToCheck(point);
                for (auto&& existingPeak : peakMap)
                    for (auto&& p : existingPeak)
                        if (GVector2D(p).dist(pointToCheck) <= minimumDistance * 2)
                            return false;
            }

            return true;
        };

        if(processPeak())
        {
            claimedPoints.merge(newClaims);
            newClaims.clear();
            peaks.emplace(peakHeight, peakPoints);
        }

        potentialPointsToMerge.clear();
        // Clear claimed points from any leftover points of invalidated peaks
        for(auto&& claim : newClaims)
            claimedPoints.erase(claim);

        int allPeakPoints = 0;
        for (auto&& peak : peaks)
            allPeakPoints += peak.second.size();

        Q_ASSERT(claimedPoints.size() == allPeakPoints);
    }

    std::unordered_multimap<float, std::vector<QVector3D>> finalPeaks;

    // To current peak points add a reversed duplicate of points to create a loop (without the first and last point)
    // Additionally check each point for subridges, and add any suitable points withing peak margin to the final peak shape
    for (auto&& peakData : peaks)
    {
        Q_ASSERT(peakData.second.size() > 1);
        // For proper isohypse generation an additional point is added if the peak consists of only 2 points
        if (peakData.second.size() == 2)
        {
            std::vector<std::pair<QVector3D, IndexType>> newPoints(3);
            newPoints[0] = peakData.second[0];
            newPoints[1] = std::pair<QVector3D, IndexType>(QVector3D((peakData.second[0].first + peakData.second[1].first) / 2), -1);
            newPoints[2] = peakData.second[1];
            peakData.second = newPoints;
        }

        auto&& peakPoints = peakData.second;
        std::vector<QVector3D> finalPeakPoints;

        // Skip the first point, the point and any subridges there will be added on the reverse iteration
        for (int idx = 1; idx < peakPoints.size(); ++idx)
        {
            finalPeakPoints.emplace_back(peakPoints[idx].first);
            if (subridgeSourcePoints.contains(peakPoints[idx].second))
            {
                auto&& subridges = subridgeSourcePoints.equal_range(peakPoints[idx].second);
                std::map<float /*angle*/, QSharedPointer<DRidgeMarker>> subridgeBranchesMap;
                auto prevVector = (GVector2D(peakPoints[idx - 1].first) - GVector2D(peakPoints[idx].first)).normalized();

                for (auto&& it = subridges.first; it != subridges.second; ++it)
                {
                    float minAngle = 0.0f;
                    if (idx < peakPoints.size() - 1)
                    {
                        auto nextVector = (GVector2D(peakPoints[idx + 1].first) - GVector2D(peakPoints[idx].first)).normalized();
                        minAngle = angle360(prevVector, nextVector);
                    }

                    auto&& subridgeCpts = it->second->getControlPoints();
                    auto subridgeVector = (GVector2D(subridgeCpts[1]) - GVector2D(peakPoints[idx].first)).normalized();
                    if (float angle = angle360(prevVector, subridgeVector); angle > minAngle)
                        subridgeBranchesMap.emplace(angle, it->second);
                }

                for (auto&& it = subridgeBranchesMap.rbegin(); it != subridgeBranchesMap.rend(); ++it)
                {
                    auto&& newPts = checkSubridgeForAdditionalPeakPoints(it->second, peakData.first, peakMargin);
                    finalPeakPoints.insert(finalPeakPoints.end(), std::make_move_iterator(newPts.begin()), std::make_move_iterator(newPts.end()));
                }
            }
        }

        // Reversed
        auto reversedPoints = peakPoints;
        std::ranges::reverse(reversedPoints);

        // Similarly, first point is skipped, since it was added in the previous iteration
        for (int idx = 1; idx < reversedPoints.size(); ++idx)
        {
            finalPeakPoints.emplace_back(reversedPoints[idx].first);
            if (subridgeSourcePoints.contains(reversedPoints[idx].second))
            {
                auto&& subridges = subridgeSourcePoints.equal_range(reversedPoints[idx].second);
                std::map<float /*angle*/, QSharedPointer<DRidgeMarker>> subridgeBranchesMap;
                auto prevVector = (GVector2D(reversedPoints[idx - 1].first) - GVector2D(reversedPoints[idx].first)).normalized();

                for (auto&& it = subridges.first; it != subridges.second; ++it)
                {
                    float minAngle = 0.0f;
                    if (idx < reversedPoints.size() - 1)
                    {
                        auto nextVector = (GVector2D(reversedPoints[idx + 1].first) - GVector2D(reversedPoints[idx].first)).normalized();
                        minAngle = angle360(prevVector, nextVector);
                    }

                    auto&& subridgeCpts = it->second->getControlPoints();
                    auto subridgeVector = (GVector2D(subridgeCpts[1]) - GVector2D(reversedPoints[idx].first)).normalized();
                    if (float angle = angle360(prevVector, subridgeVector); angle > minAngle)
                        subridgeBranchesMap.emplace(angle, it->second);
                }

                for (auto&& it = subridgeBranchesMap.rbegin(); it != subridgeBranchesMap.rend(); ++it)
                {
                    auto&& newPts = checkSubridgeForAdditionalPeakPoints(it->second, peakData.first, peakMargin);
                    finalPeakPoints.insert(finalPeakPoints.end(), std::make_move_iterator(newPts.begin()), std::make_move_iterator(newPts.end()));
                }
            }
        }

        finalPeaks.emplace(peakData.first, finalPeakPoints);
    }

    return finalPeaks;
}

std::vector<QVector3D> ContourLines::checkSubridgeForAdditionalPeakPoints(const QSharedPointer<DRidgeMarker>& subridge, const float peakHeight, const float peakMargin)
{
    std::vector<QVector3D> points;
    auto&& subridgeHeights = subridge->getHeights();
    auto&& subridgePoints = subridge->getControlPoints();
    auto&& subridgeChildren = subridge->getChildren();
    std::unordered_multimap<int, QSharedPointer<DRidgeMarker>> childrenSourcePoints;
    for (auto&& child : subridgeChildren)
        childrenSourcePoints.emplace(child->getSourcePointIdx(), child);

    // The idx is saved outside the loop, so that it is known from where to start the reverse iteration (as not all subridge points are suitable to add)
    int idx = 1;
    // Skip source points
    for (int i = idx; i < subridgePoints.size(); ++i)
    {
        idx = i;
        if (subridgeHeights[idx] > peakHeight)
            goto reverse;

        if (fCmp(subridgeHeights[idx], peakHeight, peakMargin) == std::strong_ordering::equal)
        {
            points.emplace_back(QVector3D(subridgePoints[idx].x(), 0.0f, subridgePoints[idx].z()));

            if (childrenSourcePoints.contains(idx))
            {
                auto&& ridges = childrenSourcePoints.equal_range(idx);
                std::map<float /*angle*/, QSharedPointer<DRidgeMarker>> subridgeBranchesMap;
                auto prevVector = (GVector2D(subridgePoints[idx - 1]) - GVector2D(subridgePoints[idx])).normalized();
                float minAngle = 0.0f;
                // Check if this is the last point that will be added, if so, all subridges originating from here should be added immediately
                // Otherwise only right ones should be added, and left ones while going back
                if (idx < subridgePoints.size() - 1 && fCmp(subridgeHeights[idx + 1], peakHeight, peakMargin) == std::strong_ordering::equal)
                {
                    auto nextVector = (GVector2D(subridgePoints[idx + 1]) - GVector2D(subridgePoints[idx])).normalized();
                    minAngle = angle360(prevVector, nextVector);
                }

                for (auto&& it = ridges.first; it != ridges.second; ++it)
                {
                    auto&& subridgeCpts = it->second->getControlPoints();
                    auto subridgeVector = (GVector2D(subridgeCpts[1]) - GVector2D(subridgePoints[idx])).normalized();
                    if (float angle = angle360(prevVector, subridgeVector); angle > minAngle)
                        subridgeBranchesMap.emplace(angle, it->second);
                }

                for (auto branchIt = subridgeBranchesMap.rbegin(); branchIt != subridgeBranchesMap.rend(); ++branchIt)
                {
                    auto newPoints = checkSubridgeForAdditionalPeakPoints(branchIt->second, peakHeight, peakMargin);
                    points.insert(points.end(), std::make_move_iterator(newPoints.begin()), std::make_move_iterator(newPoints.end()));
                }
            }
        }
        else
            goto reverse;
    }

reverse:
    for (int i = idx; i >= 0; --i)
    {
        points.emplace_back(QVector3D(subridgePoints[i].x(), 0.0f, subridgePoints[i].z()));
        if (i != 0 && i != subridgePoints.size() - 1 && childrenSourcePoints.contains(i))
        {
            auto&& ridges = childrenSourcePoints.equal_range(i);
            std::map<float /*angle*/, QSharedPointer<DRidgeMarker>> subridgeBranchesMap;
            // Prev vector in the reversed iteration is the idx+1 point and vice versa
            auto prevVector = (GVector2D(subridgePoints[i + 1]) - GVector2D(subridgePoints[i])).normalized();
            float minAngle = 0.0f;

            if (i > 0)
            {
                auto nextVector = (GVector2D(subridgePoints[i - 1]) - GVector2D(subridgePoints[i])).normalized();
                minAngle = angle360(prevVector, nextVector);
            }

            for (auto&& it = ridges.first; it != ridges.second; ++it)
            {
                auto&& subridgeCpts = it->second->getControlPoints();
                auto subridgeVector = (GVector2D(subridgeCpts[1]) - GVector2D(subridgePoints[i])).normalized();
                if (float angle = angle360(prevVector, subridgeVector); angle > minAngle)
                    subridgeBranchesMap.emplace(angle, it->second);
            }

            for (auto branchIt = subridgeBranchesMap.rbegin(); branchIt != subridgeBranchesMap.rend(); ++branchIt)
            {
                auto newPoints = checkSubridgeForAdditionalPeakPoints(branchIt->second, peakHeight, peakMargin);
                points.insert(points.end(), std::make_move_iterator(newPoints.begin()), std::make_move_iterator(newPoints.end()));
            }
        }
    }

    return points;
}

QMultiMap<float, IHProtoData> ContourLines::generateNextIHs(const std::vector<IHProtoData>& sourceIhs, std::vector<IHProtoData>* otherIhs, const std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>& heightBounds, int level)
{
    OmniProfile("Hull generation");
    std::vector<IHProtoData> nextIHs;

    std::vector<bool> modifiedByRidgeline;

    // Generate isohypses for each source.
    for (auto&& sourceIhData : sourceIhs)
    {
        if (sourceIhData.boundsReached)
            continue;

        if (sourceIhData.pts.size() < 3)
            continue;

        nextIHs.push_back({});
        auto&& newData = nextIHs.back();
        Q_ASSERT(sourceIhData.ptr);
        newData.parentIhs = { sourceIhData.ptr.get() };

        // Check if influenced by a single landform
        QSet<ELandform> landforms = { (*Generation::Data::get()->findDomainByGuid(sourceIhData.usedDomainId))->getData<EDomainType::Terrain>()->landform };
        for (auto&& p : sourceIhData.pts)
        {
            GPoint sq = GVector2D(p).toGPoint();
            auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
            if (domain && !sourceIhData.mergedDomains.contains(domain->getGuid()))
                landforms += domain->getData<EDomainType::Terrain>()->landform;
        }

        auto&& newHull = newData.pts;
        auto&& newSources = newData.sources;
        auto&& newIncrements = newData.increments;
        newData.originIdx = sourceIhData.originIdx;
        newData.distanceToBase = sourceIhData.distanceToBase;
        newData.heightAtHalfOfDistanceToBase = sourceIhData.heightAtHalfOfDistanceToBase;
        newData.mergeIhlevels = sourceIhData.mergeIhlevels;
        newData.bounds = sourceIhData.bounds;
        newData.heightDeltas = sourceIhData.heightDeltas;
        newData.mergeThreshold = sourceIhData.mergeThreshold;
        newData.ridgeIds = sourceIhData.ridgeIds;
        newData.lowestRidgeTier = sourceIhData.lowestRidgeTier;
        newData.usedDomainId = sourceIhData.usedDomainId;
        newData.mergedDomains = sourceIhData.mergedDomains;
        newData.affectedBy = sourceIhData.affectedBy;
        newData.tablelandType = sourceIhData.tablelandType;
        newData.modifiedBy = sourceIhData.modifiedBy;
        newData.slopeFactorForPeakApplied = sourceIhData.slopeFactorForPeakApplied;
        newData.groupingFactor = sourceIhData.groupingFactor;
        newData.currentDropLvl = sourceIhData.currentDropLvl;
        newData.desiredDropLvl = sourceIhData.desiredDropLvl;

        float slopeAngle = -1.0;
        int randomIncrement = 1;
        std::optional<ELandformVariations> landformVariation;

        // Landform-based generation
        if (landforms.size() == 1)
        {
            auto [newDelta, newIncrementMult] = Landform::computeNextIHParams(sourceIhData);
            newData.heightDeltas << fixedHeightIncrement;
            newData.mergeDistanceMult = 1.0f;

            if (auto domain = Generation::Data::get()->findDomainByGuid(sourceIhData.usedDomainId); domain)
            {
                auto&& domainData = (*domain)->getData<EDomainType::Terrain>();
                landformVariation = domainData->landformVariation;
                slopeAngle = domainData->ridgeGenParams.slopeAngle;
                randomIncrement = domainData->landformInstanceParams->randomizedIncrement;
                if (newData.currentDropLvl)
                {
                    int randomizationStartStep = *newData.desiredDropLvl - (*newData.desiredDropLvl * PTablelandTypes[*landformVariation][*newData.tablelandType].randomizationStart.getRandomValue());
                    randomIncrement = newData.currentDropLvl < randomizationStartStep ? 0 : 1;
                }
            }
        }
        else // Transition area
        {
            float baseHeight = std::accumulate(sourceIhData.heightDeltas.begin(), sourceIhData.heightDeltas.end(), sourceIhData.height);
            newData.heightDeltas << fixedHeightIncrement;
            newData.mergeDistanceMult = 1.0f;
            // TODO: take the average of slopes from the transition area?
            slopeAngle = 45.0f;
        }

        // TODO: add limits to height or make it so last ih is stretched to cover all terrain
        newData.height = sourceIhData.height - newData.heightDeltas.back();

        auto&& oldHull = sourceIhData.pts;
        auto oldIhc = asCircular(oldHull);

        newHull = oldHull;
        newSources.resize(oldHull.size());
        newIncrements.resize(oldHull.size());
        std::vector<float> projectedIncrements(oldHull.size());
        std::unordered_set<int> growthSourcesFinished;
        tbb::spin_mutex ridgeBoundGuard;

        if (landformVariation && newData.tablelandType && newData.currentDropLvl)
            slopeAngle = newData.currentDropLvl < *newData.desiredDropLvl ? PTablelandTypes[*landformVariation][*newData.tablelandType].dropRatio.getRandomValue() : slopeAngle;

        float initialIncrement = std::tanf(qDegreesToRadians(90.0f - slopeAngle)) * newData.heightDeltas.back();

        if (landformVariation && newData.tablelandType && !newData.currentDropLvl)
            initialIncrement = PTablelandTypes[*landformVariation][*newData.tablelandType].flatRadius.getRandomValue();

        // Compute predicted increments
        tbb::parallel_for(tbb::blocked_range<int>(0, oldHull.size()),
            [&](tbb::blocked_range<int>& r)
            {
                for (int i = r.begin(); i != r.end(); ++i)
                {
                    // This point contributes to the next hull
                    Q_ASSERT(sourceIhData.ptr);
                    newSources[i] = { sourceIhData.ptr.get(), i };

                    if (slopeAngle != -1.0)
                    {
                        projectedIncrements[i] = initialIncrement;
                        projectedIncrements[i] *= sourceIhData.modifiedBy[i].first;
                        projectedIncrements[i] *= sourceIhData.groupingFactor;
                        projectedIncrements[i] = std::max(5.0f, projectedIncrements[i]);
                    }
                    else
                        projectedIncrements[i] = sourceIhData.increments[i] + baseIncrement;
                }
            });

        int incrementGrowthFactor = 1;
        while (true)
        {
            auto newIhc = asCircular(newHull);
            auto currentHull = newHull;
            std::vector<QVector3D> projectedHull(newHull.size());
            std::vector<float> currentIncrements(newHull.size());
            std::vector<bool> hullGrowthEnded(newHull.size(), false);

            for (int i = 0; i < newHull.size(); ++i)
                if (growthSourcesFinished.contains(newSources[i].idx))
                    hullGrowthEnded[i] = true;

            //Compute the initial hull
            tbb::parallel_for(tbb::blocked_range<int>(0, newHull.size()),
                [&](tbb::blocked_range<int>& r)
                {
                    for (int i = r.begin(); i != r.end(); ++i)
                    {
                        // Skip if hull point already satisfies desired increment
                        auto currentInc = GVector2D(newHull[i]).dist(GVector2D(oldHull[newSources[i].idx]));
                        if (projectedIncrements[newSources[i].idx] - 2.0f <= currentInc)
                        {
                            hullGrowthEnded[i] = true;
                            std::scoped_lock<tbb::spin_mutex> dlock(ridgeBoundGuard);
                            growthSourcesFinished.emplace(newSources[i].idx);
                            continue;
                        }

                        auto&& prev = newIhc[newIhc.findIdx(i, -1)];
                        if (prev == QVector3D())
                            continue;

                        auto&& next = newIhc[newIhc.findIdx(i, 1)];
                        if (next == QVector3D())
                            continue;

                        // Compute final angle.
                        auto&& point = newHull[i];
                        QVector3D pV = (prev - point).normalized();
                        QVector3D nV = (next - point).normalized();

                        if (pV.isNull() || nV.isNull())
                            continue;

                        float maxIncrement = projectedIncrements[newSources[i].idx] - currentInc;
                        maxIncrement = std::min(maxIncrement, baseIncrement * incrementGrowthFactor);
                        currentIncrements[i] = maxIncrement;

                        // Mark growth as finished if current increment is lower than max allowed
                        if(maxIncrement < baseIncrement * incrementGrowthFactor)
                        {
                            hullGrowthEnded[i] = true;
                            std::scoped_lock<tbb::spin_mutex> dlock(ridgeBoundGuard);
                            if (!growthSourcesFinished.contains(newSources[i].idx))
                                growthSourcesFinished.emplace(newSources[i].idx);
                        }

                        projectedHull[i] = generateNewHullPoint(prev, point, next, maxIncrement);
                        projectedHull[i].setY(0.0f);

                        Q_ASSERT(projectedHull[i] != QVector3D());

                        #if DEBUG_IH_RIDGE_FOLLOW
                        spawn<DLineMarker>(QVector3D(projectedHull[i].x(), DEBUG_2D_VIEW ? 60.0f : newData.height, projectedHull[i].z()), 300, QVector4D(0, 0, 1, 1));
                        #endif
                    }
                });

            // Compute final hull 
            tbb::parallel_for(tbb::blocked_range<int>(0, newHull.size()),
                [&](tbb::blocked_range<int>& r)
                {
                    for (int i = r.begin(); i != r.end(); ++i)
                    {
                        if (currentIncrements[i] < 1.0f || projectedIncrements[newSources[i].idx] < 1.0f)
                        {
                            hullGrowthEnded[i] = true;
                            std::scoped_lock<tbb::spin_mutex> dlock(ridgeBoundGuard);
                            growthSourcesFinished.emplace(newSources[i].idx);
                            continue;
                        }

                        float computedIncrement = computeIncrement(currentHull, projectedHull, currentIncrements, i, &newData, heightBounds, slopeAngle);

                        //Check if the prediction was correct
                        if (std::abs(computedIncrement - currentIncrements[i]) <= 2.0f)
                        {
                            newHull[i] = projectedHull[i];
                            Q_ASSERT(newHull[i] != QVector3D());

                            auto newInc = GVector2D(newHull[i]).dist(GVector2D(oldHull[newSources[i].idx]));
                            if (projectedIncrements[newSources[i].idx] - 2.0f <= newInc)
                            {
                                std::scoped_lock<tbb::spin_mutex> dlock(ridgeBoundGuard);
                                growthSourcesFinished.emplace(newSources[i].idx);
                            }
                        }
                        else
                        {
                            GVector2D projectedGrowthDir = projectedHull[i] - currentHull[i];
                            newHull[i] = currentHull[i] + projectedGrowthDir * (computedIncrement / currentIncrements[i]);
                            std::scoped_lock<tbb::spin_mutex> dlock(ridgeBoundGuard);
                            growthSourcesFinished.emplace(newSources[i].idx);

                            Q_ASSERT(newHull[i] != QVector3D());
                        }
                    }
                });

            for (int i = 0; i < newHull.size(); ++i)
                if (growthSourcesFinished.contains(newSources[i].idx))
                    hullGrowthEnded[i] = true;

            if (std::all_of(hullGrowthEnded.begin(), hullGrowthEnded.end(), [](const auto& ele) {return ele == true;}))
            {
                #if DEBUG_IH_FIX
                for (int i = 0; i < newHull.size(); ++i)
                {
                    QVector3D point(newHull[i].x(), newData.height + 10.0f, newHull[i].z());
                    QVector3D sourcePoint(oldHull[newData.sources[i].idx].x(), newData.height + 10.0f, oldHull[newData.sources[i].idx].z());
                    spawn<DLineMarker>(sourcePoint, point, QVector4D(1, 0, 1, 0.5), 0.0f, ELineDecorator::Arrow);
                }
                #endif

                for (int i = 0; i < newHull.size(); ++i)
                    newIncrements[i] = GVector2D(newHull[i]).dist(GVector2D(oldHull[newSources[i].idx]));

                fixHull(&newData, {oldHull}, true);
                break;
            }
            else
            {
                #if DEBUG_IH_FIX
                for (int i = 0; i < newHull.size(); ++i)
                {
                    QVector3D point(newHull[i].x(), newData.height, newHull[i].z());
                    QVector3D sourcePoint(oldHull[newData.sources[i].idx].x(), newData.height, oldHull[newData.sources[i].idx].z());
                    spawn<DLineMarker>(sourcePoint, point, QVector4D(1, 1, 1, 0.5), 0.0f, ELineDecorator::Arrow);
                }
                #endif

                incrementGrowthFactor++;
                fixHull(&newData, {}, true);
            }
        }

        #if DEBUG_HEIGHT_BOUNDS
        for (int i = 0; i < newHull.size(); ++i)
                spawn<DLineMarker>(QVector3D(oldHull[newSources[i].idx].x(), newData.height, oldHull[newSources[i].idx].z()), QVector3D(newHull[i].x(), newData.height, newHull[i].z()), QVector4D(1,1,1,0.5), 0.0f, ELineDecorator::Arrow);

        for (auto&& [pts, sc] : debugDistances)
            Generation::Data::get()->createMarker<DLineMarker>(pts.first, pts.second, QVector4D(sc, sc, sc, 1));
        #endif

        Q_ASSERT(newHull.size() == newSources.size());
        Q_ASSERT(newIncrements.size() == newSources.size());

        bool flatlandCheck = true;
        if (slopeAngle < 5.0f)
            flatlandCheck = checkFlatlandsAgainstOthers(nextIHs);

        auto&& intersectionPoints = checkForIntersectionWithRidge(newData.ridgeIds, newHull, newData.height);
        if (!intersectionPoints.empty() && flatlandCheck)
            reshapeHullToRidgeline(intersectionPoints, &newData);

        if (randomIncrement == 2 || (randomIncrement == 1 && intersectionPoints.empty()))
            findAndModifySegments(&newData, sourceIhData);

        modifiedByRidgeline.emplace_back(!intersectionPoints.empty());
        fixHull(&newData, { oldHull });

        Q_ASSERT(newHull.size() == newSources.size());
        Q_ASSERT(newIncrements.size() == newSources.size());

        if (newData.pts.size() < 3)
            nextIHs.pop_back();
    }

    // Attempt to merge hulls that grow close enough.
    int pSize = nextIHs.size();
    nextIHs << *otherIhs;
    Q_ASSERT(otherIhs->empty() || (nextIHs.size() == pSize + otherIhs->size()));

    mergeNextIHs(&nextIHs);
    swallowAll(&nextIHs);
    computeGroupParamFulfillment(&nextIHs, modifiedByRidgeline);

    QMultiMap<float, IHProtoData> results;

    // Create markers
    for (int hi = 0; hi < nextIHs.size(); ++hi)
    {
        // Depending whether we are dealing with a pre existing marker we either insert it into map or fill out the data and create the underlying IH
        auto&& newMarkerData = nextIHs[hi];
        if (newMarkerData.ptr)
        {
            results.insert(newMarkerData.height, newMarkerData);
            continue;
        }

        auto newMarker = spawnBatchedIH(newMarkerData, level);
        Q_ASSERT(newMarker);
        newMarkerData.ptr = newMarker;
        Q_ASSERT(newMarkerData.pts.size() == newMarkerData.sources.size());

        // Add descendants to sources
        for (int i = 0; i < newMarkerData.sources.size(); ++i)
        {
            auto&& source = newMarkerData.sources[i];
            if (source)
                source.ih->setDescendant(source.idx, { newMarker.get(), i });
            Q_ASSERT(i < newMarker->getVertexBufferSize());
        }

        // Check if all bounds of isohypse are reached
        if (!newMarkerData.boundsReached)
        {
            bool allReached = true;

            for (auto&& [boundId, data] : newMarkerData.bounds)
            {
                if (data.first->isCrossing(newMarker))
                    data.second = EIHGenerationStage::BoundsCrossed;
                else if (data.second == EIHGenerationStage::BoundsCrossed)
                    data.second = EIHGenerationStage::Done;

                if (data.second != EIHGenerationStage::Done)
                    allReached = false;
            }

            if (allReached)
                newMarkerData.boundsReached = true;
        }

        // Don't grow swallowed ihs ever again
        if (!newMarkerData.swallowedBy)
            results.insert(newMarkerData.height, newMarkerData);
        else
        {
            // Compute "descendants" for swallowed ih, they will lead to the ih that swallowed it
            auto&& pts = newMarker->getCircularPoints();
            for (int i = 0; i < pts.getSize(); ++i)
            {
                float minD = std::numeric_limits<float>::max();
                IHSrcInfo desc;
                for (auto* swallowerParent : newMarkerData.swallowedBy->parentIhs)
                {
                    auto&& swallowerPts = swallowerParent->getCircularPoints();
                    for (int j = 0; j < swallowerPts.getSize(); ++j)
                        if (float d = distance(pts[i], swallowerPts[j]); d < minD)
                        {
                            minD = d;
                            desc = { swallowerParent, j };
                        }
                }

                //spawn<DLineMarker>(pts[i] + QVector3D(0, newMarker->getHeight(), 0), desc.getPoint() + QVector3D(0, desc.ih->height, 0));
                newMarker->setDescendant(i, desc);
            }

            // Add parent to an existing IH or a new IH already processed by this loop
            if (auto newMarkerPtr = newMarkerData.swallowedBy->ptr; newMarkerPtr)
                newMarkerPtr->addParent(newMarker.get());
            else // Add parent to new IH not yet processed by this loop
                newMarkerData.swallowedBy->parentIhs.insert(newMarker.get());
        }
    }

    return results;
}

QVector3D ContourLines::findIHPeak(const IHProtoData& data, int id)
{
    auto topIH = data.sources[id];
    while (topIH && topIH.getSource())
        topIH = topIH.getSource();

    return topIH ? topIH.getPoint() : data.pts[id];
}

void ContourLines::mergeNextIHs(std::vector<IHProtoData>* ihProtoData)
{
    OmniProfile("IH Merging");

    // map from premerged isohypses to merged isohpyse
    std::map<int, int> toMergedIHId;
    auto findMergedIHId = [&toMergedIHId](int id)
    {
        int finalId = id;
        while (true)
        {
            auto&& it = toMergedIHId.find(finalId);
            if (it != toMergedIHId.end())
            {
                finalId = toMergedIHId.at(finalId);
                continue;
            }

            return finalId;
        }
    };

    tbb::parallel_for(0, int(ihProtoData->size()), [&](int i)
        {
            ihProtoData->at(i).computeMergingData();
        });

    // Merging logic core
    auto&& closestIHInfos = detectClosestIsohypseColissions(*ihProtoData);
    if (closestIHInfos.empty())
        return;

    std::vector<int> ihIdsToRemove;
    for (auto&& [ihsToMerge, mergeInfo] : closestIHInfos)
    {
        int ihId1 = findMergedIHId(ihsToMerge.first);
        int ihId2 = findMergedIHId(ihsToMerge.second);

        if (ihId1 == -1 || ihId2 == -1 || ihId1 == ihId2)
            continue;

        auto&& ihData1 = (*ihProtoData)[ihId1];
        auto&& ihData2 = (*ihProtoData)[ihId2];

        auto cPTs1 = asCircular(ihData1.pts);
        auto cPTs2 = asCircular(ihData2.pts);

        // have to find new merge info when considering already merged/shrinked isohypse
        if (auto newMergeInfo = detectClosestIsohypseInfo(ihData1, ihData2); newMergeInfo)
            mergeInfo = newMergeInfo.value();
        else
            continue;

#if DEBUG_IH_MERGES
        for (int i = mergeInfo.shrinkBounds1.first; i != mergeInfo.shrinkBounds1.second; i = cPTs1.findIdx(i, 1))
        {
            QVector3D first(ihData1.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : ihData1.height, ihData1.pts[i].z());
            QVector3D second(ihData1.pts[cPTs1.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : ihData1.height, ihData1.pts[cPTs1.findIdx(i, 1)].z());
            spawn<DLineMarker>(first, second, QVector4D(0, 1, 1, 1), 30.0f, ELineDecorator::Arrow);
        }

        for (int i = mergeInfo.shrinkBounds2.first; i != mergeInfo.shrinkBounds2.second; i = cPTs2.findIdx(i, 1))
        {
            QVector3D first(ihData2.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : ihData2.height, ihData2.pts[i].z());
            QVector3D second(ihData2.pts[cPTs2.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : ihData2.height, ihData2.pts[cPTs2.findIdx(i, 1)].z());
            spawn<DLineMarker>(first, second, QVector4D(0, 1, 1, 1), 30.0f, ELineDecorator::Arrow);
        }

        for (int i = mergeInfo.mergeBounds1.first; i != mergeInfo.mergeBounds1.second; i = cPTs1.findIdx(i, 1))
        {
            QVector3D first(ihData1.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : ihData1.height, ihData1.pts[i].z());
            QVector3D second(ihData1.pts[cPTs1.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : ihData1.height, ihData1.pts[cPTs1.findIdx(i, 1)].z());
            spawn<DLineMarker>(first, second, QVector4D(0, 0, 1, 1), 50.0f, ELineDecorator::Arrow);
        }

        for (int i = mergeInfo.mergeBounds2.first; i != mergeInfo.mergeBounds2.second; i = cPTs2.findIdx(i, 1))
        {
            QVector3D first(ihData2.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : ihData2.height, ihData2.pts[i].z());
            QVector3D second(ihData2.pts[cPTs2.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : ihData2.height, ihData2.pts[cPTs2.findIdx(i, 1)].z());
            spawn<DLineMarker>(first, second, QVector4D(0, 0, 1, 1), 50.0f, ELineDecorator::Arrow);
        }
#endif

        auto minIdx = std::min(ihData1.originIdx, ihData2.originIdx);
        auto maxIdx = std::max(ihData1.originIdx, ihData2.originIdx);

        if (!ihMerges.contains(minIdx) || !ihMerges[minIdx].contains(maxIdx))
            ihMerges[minIdx][maxIdx] = ihData1.height < ihData2.height ? ihData1.height : ihData2.height;

        if (ihData1.regenerated && ihData2.regenerated)
            continue;

        // Only isohypses of same height should be merged, otherwise the higher ih should be shrunken to give enough space for self to reach that height
        // Isohypses of lvl 0 and 1 should not shrink, hence the check
        if (ihData1.height == ihData2.height && (ihData1.heightDeltas.size() > 1 && ihData2.heightDeltas.size() > 1) && (!ihData1.regenerated && !ihData2.regenerated))
        {
            // Skip valley creation for the standard case when coverage % is achieved and a openness factor present
            if (ihData1.groupingFactor == 1.0f || ihData2.groupingFactor == 1.0f)
            {
                auto minValleyWidth = computeValleyWidth(&ihData1, &ihData2, mergeInfo);

                if (!minValleyWidth || *minValleyWidth <= baseIncrement)
                    goto mergeAnyway;

                mergeCancelShrinkIsohypse(&ihData1, mergeInfo.shrinkBounds1, &ihData2, mergeInfo.shrinkBounds2, *minValleyWidth, true);
            }

            for (int idx1 = mergeInfo.shrinkBounds1.first; idx1 != mergeInfo.shrinkBounds1.second; idx1 = cPTs1.findIdx(idx1, 1))
            {
                Segment2D firstSegment(GVector2D(ihData1.pts[idx1]), GVector2D(ihData1.pts[cPTs1.findIdx(idx1, 1)]));
                for (int idx2 = mergeInfo.shrinkBounds2.first; idx2 != mergeInfo.shrinkBounds2.second; idx2 = cPTs2.findIdx(idx2, 1))
                {
                    Segment2D secondSegment(GVector2D(ihData2.pts[idx2]), GVector2D(ihData2.pts[cPTs2.findIdx(idx2, 1)]));
                    if (secondSegment.intersects(firstSegment, true))
                        goto mergeAnyway;
                }
            }

            fixHull(&ihData1, {}, true);
            fixHull(&ihData2, {}, true);
            continue;

        }
        else if (ihData1.regenerated || ihData2.regenerated || ihData1.heightDeltas.size() > 1 || ihData2.heightDeltas.size() > 1)
        {
            float heightDelta = qAbs(ihData1.height - ihData2.height);
            IHProtoData* higherData;
            std::pair<int, int> higherBound;

            IHProtoData* otherData;
            std::pair<int, int> otherBound;

            auto minValleyWidth = computeValleyWidth(&ihData1, &ihData2, mergeInfo);
            if (!minValleyWidth)
                goto mergeAnyway;

            // This is the case when it should be a normal valley, but one of the Ihs is not valid for shrinking
            if (fCmp(heightDelta, 0.0f) == std::strong_ordering::equal)
            {
                if(!ihData1.regenerated && !ihData2.regenerated)
                {
                    higherData = ihData1.heightDeltas.size() > 1 ? &ihData1 : &ihData2;
                    higherBound = ihData1.heightDeltas.size() > 1 ? mergeInfo.shrinkBounds1 : mergeInfo.shrinkBounds2;

                    otherData = ihData1.heightDeltas.size() > 1 ? &ihData2 : &ihData1;
                    otherBound = ihData1.heightDeltas.size() > 1 ? mergeInfo.shrinkBounds2 : mergeInfo.shrinkBounds1;
                }
                else
                {
                    higherData = ihData1.regenerated ? &ihData2 : &ihData1;
                    higherBound = ihData1.regenerated ? mergeInfo.shrinkBounds2 : mergeInfo.shrinkBounds1;

                    otherData = ihData1.regenerated ? &ihData1 : &ihData2;
                    otherBound = ihData1.regenerated ? mergeInfo.shrinkBounds1 : mergeInfo.shrinkBounds2;
                }
            }
            // Shrink higher Ih to give enough space to reach height of lower Ih
            else
            {
                higherData = ihData1.height < ihData2.height ? &ihData2 : &ihData1;
                higherBound = ihData1.height < ihData2.height ? mergeInfo.shrinkBounds2 : mergeInfo.shrinkBounds1;

                otherData = ihData1.height < ihData2.height ? &ihData1 : &ihData2;
                otherBound = ihData1.height < ihData2.height ? mergeInfo.shrinkBounds1 : mergeInfo.shrinkBounds2;
            }

            mergeCancelShrinkIsohypse(higherData, higherBound, otherData, otherBound, *minValleyWidth, false);
            for (int idx1 = mergeInfo.shrinkBounds1.first; idx1 != mergeInfo.shrinkBounds1.second; idx1 = cPTs1.findIdx(idx1, 1))
            {
                Segment2D firstSegment(GVector2D(ihData1.pts[idx1]), GVector2D(ihData1.pts[cPTs1.findIdx(idx1, 1)]));
                for (int idx2 = mergeInfo.shrinkBounds2.first; idx2 != mergeInfo.shrinkBounds2.second; idx2 = cPTs2.findIdx(idx2, 1))
                {
                    Segment2D secondSegment(GVector2D(ihData2.pts[idx2]), GVector2D(ihData2.pts[cPTs2.findIdx(idx2, 1)]));
                    if (secondSegment.intersects(firstSegment, true))
                        goto mergeAnyway;
                }
            }

            fixHull(higherData, {}, true);
            continue;
        }
        // Merge different height lvl 0 and 1 isohypses only if critically close to each other
        else if (ihData1.heightDeltas.size() == 1 && ihData2.heightDeltas.size() == 1 && mergeInfo.distance > baseIncrement)
        {
            #if DEBUG_IH_MERGES
            for (int i = 0; i < ihData1.pts.size(); ++i)
            {
                QVector3D first(ihData1.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : ihData1.height, ihData1.pts[i].z());
                QVector3D second(ihData1.pts[cPTs1.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : ihData1.height, ihData1.pts[cPTs1.findIdx(i, 1)].z());
                spawn<DLineMarker>(first, second, QVector4D(1, 0, 0, 0.5), 50.0f, ELineDecorator::Arrow);
            }

            for (int i = 0; i < ihData2.pts.size(); ++i)
            {
                QVector3D first(ihData2.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : ihData2.height, ihData2.pts[i].z());
                QVector3D second(ihData2.pts[cPTs2.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : ihData2.height, ihData2.pts[cPTs2.findIdx(i, 1)].z());
                spawn<DLineMarker>(first, second, QVector4D(0, 1, 0, 0.5), 50.0f, ELineDecorator::Arrow);
            }
            #endif

            continue;
        }

    mergeAnyway:
        if (ihData1.regenerated || ihData2.regenerated)
            continue;

        if (auto mergedIH = createMergedIH(ihData1, ihData2, mergeInfo.mergeBounds1, mergeInfo.mergeBounds2); !mergedIH.pts.empty())
        {
            (*ihProtoData) << mergedIH;
            toMergedIHId[ihId1] = (*ihProtoData).size() - 1;
            toMergedIHId[ihId2] = (*ihProtoData).size() - 1;
        }
        else
        {
            toMergedIHId[ihId1] = -1;
            toMergedIHId[ihId2] = -1;
        }

        ihIdsToRemove << ihId1;
        ihIdsToRemove << ihId2;
    }

    // remove after merging, to not conflict closestIHInfos ids
    removeIHs(ihProtoData, ihIdsToRemove);
}

std::optional<float> ContourLines::computeValleyWidth(IHProtoData* ihData1, IHProtoData* ihData2, const IsohypseMergeInfo& mergeInfo)
{
    QVector3D mergePoint;
    float amountOfPoints = 0;

    auto cPTs1 = asCircular(ihData1->pts);
    auto cPTs2 = asCircular(ihData2->pts);

    for (int i = mergeInfo.mergeBounds1.first; i != cPTs1.findIdx(mergeInfo.mergeBounds1.second, 1); i = cPTs1.findIdx(i, 1))
    {
        mergePoint += ihData1->pts[i];
        amountOfPoints++;
    }

    for (int i = mergeInfo.mergeBounds2.first; i != cPTs2.findIdx(mergeInfo.mergeBounds2.second, 1); i = cPTs2.findIdx(i, 1))
    {
        mergePoint += ihData2->pts[i];
        amountOfPoints++;
    }

    mergePoint /= amountOfPoints;

    auto mergeDomainId = findDomainBetweenIsohypses(ihData1->sources[mergeInfo.mergeBounds1.first], ihData2->sources[mergeInfo.mergeBounds2.first], mergePoint);
    Q_ASSERT(mergeDomainId != 0);

    auto&& domain = (*Generation::Data::get()->findDomainByGuid(mergeDomainId));
    if (!domain)
        return -1.0f;

    auto landform = domain->getData<EDomainType::Terrain>()->landform;
    auto peakHeight1 = ihData1->height + std::accumulate(ihData1->heightDeltas.begin(), ihData1->heightDeltas.end(), 0.0f);
    auto peakHeight2 = ihData2->height + std::accumulate(ihData2->heightDeltas.begin(), ihData2->heightDeltas.end(), 0.0f);
    auto peakPos = peakHeight1 < peakHeight2 ? findIHPeak(*ihData1, mergeInfo.mergeBounds1.first) : findIHPeak(*ihData2, mergeInfo.mergeBounds2.first);
    float slopeAngle = findSlopeAngleBetweenRidges(*ihData1, *ihData2, domain);
    float distanceToPeak = GVector2D(peakPos).dist(GVector2D(mergePoint));
    float valleyHeight = std::min(peakHeight1, peakHeight2) - (distanceToPeak / std::tanf(qDegreesToRadians(90.0f - slopeAngle)));
    float closestDistance = std::numeric_limits<float>::max();

    for (int idx = mergeInfo.mergeBounds1.first; idx <= mergeInfo.mergeBounds1.second; ++idx)
    {
        auto&& point = ihData1->sources[idx].getPoint();
        if (auto&& dist = GVector2D(point).dist(GVector2D(mergePoint)); dist < closestDistance)
            closestDistance = dist;
    }

    for (int idx = mergeInfo.mergeBounds2.first; idx <= mergeInfo.mergeBounds2.second; ++idx)
    {
        auto&& point = ihData2->sources[idx].getPoint();
        if (auto&& dist = GVector2D(point).dist(GVector2D(mergePoint)); dist < closestDistance)
            closestDistance = dist;
    }

    int stepsToAchieveHeight = std::ceil((ihData1->height + fixedHeightIncrement - valleyHeight) / fixedHeightIncrement);
    float increment = (closestDistance / stepsToAchieveHeight);

    if (stepsToAchieveHeight < 1 || increment < 0.0f)
        return {};

    std::vector<float> increments1;
    std::vector<float> increments2;

    for (int idx = mergeInfo.shrinkBounds1.first; idx != cPTs1.findIdx(mergeInfo.shrinkBounds1.second, 1); idx = cPTs1.findIdx(idx, 1))
        increments1.emplace_back(ihData1->increments[idx]);

    for (int idx = mergeInfo.shrinkBounds2.first; idx != cPTs2.findIdx(mergeInfo.shrinkBounds2.second, 1); idx = cPTs2.findIdx(idx, 1))
        increments2.emplace_back(ihData2->increments[idx]);

    float averageInc1 = std::accumulate(increments1.begin(), increments1.end(), 0.0f) / increments1.size();
    float averageInc2 = std::accumulate(increments2.begin(), increments2.end(), 0.0f) / increments2.size();

    if (increment > averageInc1 || increment > averageInc2)
        increment = std::min(averageInc1, averageInc2);

    return (closestDistance - increment) * 2;
}

quint64 ContourLines::findDomainBetweenIsohypses(const IHSrcInfo& data1, const IHSrcInfo& data2, const QVector3D& point)
{
    auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(point).toGPoint(), EDomainType::Terrain);
    if (domain)
        return domain->getGuid();

    auto testSrc1 = data1;
    auto testSrc2 = data2;

    while (true)
    {
        auto sq1 = ((GVector2D)testSrc1.getPoint()).toGPoint();
        auto sq2 = ((GVector2D)testSrc2.getPoint()).toGPoint();

        auto&& domain1 = testSrc1 ? Generation::Data::get()->getDomainAtSquare(sq1, EDomainType::Terrain) : nullptr;
        auto&& domain2 = testSrc2 ? Generation::Data::get()->getDomainAtSquare(sq2, EDomainType::Terrain) : nullptr;

        if (domain1 && domain2)
        {
            auto distance1 = testSrc1.getPoint().distanceToPoint(point);
            auto distance2 = testSrc2.getPoint().distanceToPoint(point);
            
            return distance1 < distance2 ? domain1->getGuid() : domain2->getGuid();
        }
        else if (domain1 || domain2)
        {
            return domain1 ? domain1->getGuid() : domain2->getGuid();
        }

        testSrc1 = testSrc1 ? testSrc1.ih->getSources()[testSrc1.idx] : testSrc1;
        testSrc2 = testSrc2 ? testSrc2.ih->getSources()[testSrc2.idx] : testSrc2;

        if (!testSrc1 && !testSrc2)
            return 0;
    }
}

float ContourLines::findSlopeAngleBetweenRidges(const IHProtoData& data1, const IHProtoData& data2, const QSharedPointer<DDomain>& domain)
{
    if (!std::any_of(data1.ridgeIds.begin(), data1.ridgeIds.end(), [data2](auto& id) { return data2.ridgeIds.contains(id); }))
        return domain->getData<EDomainType::Terrain>()->landformInstanceParams->slopeAngleDifferentRidges.getRandomValue();
    else if (data1.lowestRidgeTier == 0 && data2.lowestRidgeTier == 0)
        return domain->getData<EDomainType::Terrain>()->landformInstanceParams->slopeAngleSameRidgesLevel0.getRandomValue();
    else
        return domain->getData<EDomainType::Terrain>()->landformInstanceParams->slopeAngleSameRidges.getRandomValue();
}

bool ContourLines::mergeCancelShrinkIsohypse(IHProtoData* data1, const std::pair<int, int>& segmentToShrink1, IHProtoData* data2, const std::pair<int, int>& segmentToShrink2, float minDistRequired, bool shrinkSecondSegment)
{
    auto ihc1 = asCircular(data1->pts);
    auto ihc2 = asCircular(data2->pts);
    auto&& hull1 = data1->pts;
    auto&& hull2 = data2->pts;

    data1->affectedBy[EIHAffectType::Merge] += data2->ridgeIds;
    data2->affectedBy[EIHAffectType::Merge] += data1->ridgeIds;

    std::unordered_map<int, float> movePoint1;
    std::unordered_map<int, float> movePoint2;

    auto getSource = [](const IHProtoData& data, int pointIdx)
    {
        auto it = data.ridgelineSources.find(pointIdx);
        if (it != data.ridgelineSources.end())
            return it->second;

        return data.sources[pointIdx].getPoint();
    };

    // Check distances between each point of both segments, and compute how much they need to shrink to achieve desired distance
    for (int idx1 = segmentToShrink1.first; idx1 != ihc1.findIdx(segmentToShrink1.second, 1); idx1 = ihc1.findIdx(idx1, 1))
    {
        GVector2D firstPoint(ihc1[idx1]);
        for (int idx2 = segmentToShrink2.first; idx2 != ihc2.findIdx(segmentToShrink2.second, 1); idx2 = ihc2.findIdx(idx2, 1))
        {
            if (float dist = GVector2D(ihc2[idx2]).dist(firstPoint); dist < minDistRequired)
            {
                auto valleyVector1 = (GVector2D(ihc2[idx2]) - firstPoint).normalized();
                auto valleyVector2 = valleyVector1.rotateDegrees(valleyVector1, 180);
                auto sourcePoint1 = getSource(*data1, idx1);
                auto sourcePoint2 = getSource(*data2, idx2);

                auto dir1 = (GVector2D(ihc1[idx1]) - GVector2D(sourcePoint1)).normalized();
                auto dir2 = (GVector2D(ihc2[idx2]) - GVector2D(sourcePoint2)).normalized();

                float distDelta = minDistRequired - dist;
                float distanceToShrink = shrinkSecondSegment ? distDelta / 2 : distDelta;
                auto oldIncrement1 = GVector2D(sourcePoint1).dist(GVector2D(ihc1[idx1]));
                float angle1 = angle180(dir1, valleyVector1);
                float factor1 = std::clamp(1.0f - ((angle1 - 20.0f) / 100.0f), 0.0f, 1.0f);
                float maxDistToShrink1 = std::min(oldIncrement1, distanceToShrink);

                if (!movePoint1.contains(idx1) || movePoint1[idx1] < maxDistToShrink1)
                    movePoint1[idx1] = maxDistToShrink1 * factor1;

                if (!shrinkSecondSegment)
                    continue;

                float angle2 = angle180(dir2, valleyVector2);
                float factor2 = std::clamp(1.0f - ((angle2 - 20.0f) / 100.0f), 0.0f, 1.0f);
                auto oldIncrement2 = GVector2D(sourcePoint2).dist(GVector2D(ihc2[idx2]));
                float maxDistToShrink2 = std::min(oldIncrement2, distanceToShrink);

                if (!movePoint2.contains(idx2) || movePoint2[idx2] < maxDistToShrink2)
                    movePoint2[idx2] = maxDistToShrink2 * factor2;
            }
        }
    }

    auto shrinkSegment = [getSource](IHProtoData* data, const std::pair<int, int>& segmentToShrink, const CircularVectorView<std::vector, QVector3D>& ihc, const std::unordered_map<int, float>& movePoint)
    {
        auto&& hull = data->pts;
        auto&& increments = data->increments;
        auto&& ridgelineSources = data->ridgelineSources;

        for (int idx = segmentToShrink.first; idx != ihc.findIdx(segmentToShrink.second, 1); idx = ihc.findIdx(idx, 1))
        {
            #if DEBUG_IH_MERGES
            auto debugOldPos = hull[idx];
            #endif

            // No need to shrink point
            if (!movePoint.contains(idx))
                continue;

            auto sourcePoint = getSource(*data, idx);
            auto oldIncrement = GVector2D(sourcePoint).dist(GVector2D(ihc[idx]));

            auto dir = (ihc[idx] - sourcePoint).normalized();
            QVector3D movedPoint = sourcePoint + (dir * std::max(5.0f, oldIncrement - movePoint.at(idx)));

            hull[idx] = movedPoint;
            increments[idx] = GVector2D(sourcePoint).dist(GVector2D(hull[idx]));

            #if DEBUG_IH_MERGES
            spawn<DLineMarker>(QVector3D(debugOldPos.x(), DEBUG_2D_VIEW ? 60.0f : data->height, debugOldPos.z()), QVector3D(hull[idx].x(), DEBUG_2D_VIEW ? 60.0f : data->height, hull[idx].z()), QVector4D(1, 0, 0, 1), 0.0f, ELineDecorator::Arrow);
            #endif
        }
    };

    shrinkSegment(data1, segmentToShrink1, ihc1, movePoint1);
    if(shrinkSecondSegment)
        shrinkSegment(data2, segmentToShrink2, ihc2, movePoint2);

    return true;
}

auto ContourLines::computeProtoDataQtree(const std::vector<IHProtoData>& ihProtoData) -> QSharedPointer<tml::qtree<float, IHProtoTreeData>>
{
    OmniProfile("Merging QTree computations");

    // 3 squares lookup margin
    constexpr float minCoord = -3 * GRID_SEGMENT_WIDTH;
    constexpr float maxCoord = (GRID_SEGMENT_COUNT + 3) * GRID_SEGMENT_WIDTH;
    auto qtree = QSharedPointer<tml::qtree<float, IHProtoTreeData>>::create(minCoord, maxCoord, maxCoord, minCoord);

    for (int dataIdx = 0; dataIdx < ihProtoData.size(); ++dataIdx)
    {
        auto&& pts = ihProtoData[dataIdx].pts;
        for (int i0 = 0; i0 < pts.size(); ++i0)
        {
            // Split each segment into multiple points
            int i1 = (i0 == pts.size() - 1) ? 0 : i0 + 1;
            auto&& p0 = pts[i0];
            auto&& p1 = pts[i1];

            float length = distance(p0, p1);
            int stepCount = std::ceil(length / baseIncrement);
            for (int s = 0; s <= stepCount; ++s)
            {
                GVector2D p = std::lerp(p0, p1, float(s) / float(stepCount));
                if (s == 0 || s == stepCount)
                {
                    // We can have overlaps
                    if (auto* existingNode = qtree->find_nearest(p.x, p.z, 1.f); existingNode && existingNode->data.hullIdx == dataIdx)
                        if (s == 0)
                            const_cast<IHProtoTreeData&>(existingNode->data).i1 = i1;
                        else
                            const_cast<IHProtoTreeData&>(existingNode->data).i0 = i0;
                    else
                        qtree->add_node(p.x, p.z, { dataIdx, i0, i1 });
                }
                else
                {
                    qtree->add_node(p.x, p.z, { dataIdx, i0, i1 });
                }
            }
        }
    }

    return qtree;
}

void ContourLines::thickenHull(IHProtoData* data)
{
    OmniProfile("Hull thickening");

    auto&& currentHull = data->pts;
    auto&& ridgelineSources = data->ridgelineSources;

    std::unordered_set<int> indicesToPrettify;
    std::unordered_set<int> indicesToDelete;
    std::unordered_set<int> indicesToLinebreak;
    std::unordered_set<int> indicesToMakeDomainCorner;
    std::vector<std::pair<QVector3D, int /*old idx*/>> newHull;
    std::unordered_map<int, QVector3D> newRidgelineSources;

    const float convexAngle = 110;
    const float concaveAngle = 250;

    for (int i = 0; i < currentHull.size(); ++i)
    {
        auto&& prev = currentHull[i == 0 ? currentHull.size() - 1 : i - 1];
        auto&& next = currentHull[i == currentHull.size() - 1 ? 0 : i + 1];
        auto&& point = currentHull[i];

        // Check for edge cases where two points are out of bounds - thus finished generation, 
        // but the domain corner still intersects with the segment between them
        GPoint sq = GVector2D(point).toGPoint();
        auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
        if (!domain)
        {
            sq = GVector2D(next).toGPoint();
            domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
            if (!domain)
            {
                Segment2D outOfBoundsSegment(point, next);
                for (auto&& [boundId, data] : data->bounds)
                {
                    auto boundIhc = asCircular(data.first->getControlPoints());
                    for (int j = 0; j < boundIhc.getSize(); ++j)
                    {
                        int j2 = boundIhc.findIdx(j, 1);
                        Segment2D controlSegment({ boundIhc[j].x(), boundIhc[j].z() }, { boundIhc[j2].x(), boundIhc[j2].z() });

                        if (controlSegment.intersects(outOfBoundsSegment, true))
                            indicesToMakeDomainCorner.emplace(i);
                    }
                }
            }
            continue;
        }

        if (GVector2D(prev).dist(GVector2D(point)) > 5 * baseIncrement)
            indicesToLinebreak.emplace(i);

        QVector3D pV = (prev - point).normalized();
        QVector3D nV = (next - point).normalized();

        float angle = angle360(pV, nV);

        if (angle <= convexAngle)
            indicesToPrettify.emplace(i);
        else if (angle > concaveAngle)
            indicesToPrettify.emplace(i);
    }

    auto ihc = asCircular(currentHull);

    for (int i = 0; i < currentHull.size(); ++i)
    {
        auto&& point = currentHull[i];

        if (indicesToPrettify.contains(i))
        {
            auto&& prev = currentHull[ihc.findIdx(i, -1)];
            auto&& next = currentHull[ihc.findIdx(i, 1)];

            if(i == 0 || !indicesToPrettify.contains(i - 1))
            {
                QVector3D newPoint = (prev + point) / 2;
                newHull.emplace_back(newPoint, i);

                if (ridgelineSources.contains(i))
                    newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

                #if DEBUG_IH_FIX
                spawn<DLineMarker>(QVector3D(newPoint.x(), DEBUG_2D_VIEW ? 60.0f : data->height, newPoint.z()), 300, QVector4D(1, 1, 1, 1));
                #endif
            }

            auto prevDist = prev.distanceToPoint(point);
            auto nextDist = next.distanceToPoint(point);
            float smoothDistance = prevDist < nextDist ? prevDist / 10.0f : nextDist / 10.0f;

            // Smoothing point prev-point
            if (QVector3D((prev + point) / 2).distanceToPoint(point) > smoothDistance * 2)
            {
                QVector3D prevDir = (prev - point).normalized();
                QVector3D pointOnLine = point + (prevDir * smoothDistance * 1.2f);

                // Check if valley or ridge
                QVector3D pV = (prev - point).normalized();
                QVector3D nV = (next - point).normalized();
                float angle = angle360(pV, nV);
                float rotation = 90.0f; // left
                if (angle > concaveAngle)
                    rotation = 270.0f; // right

                QVector3D smoothingPoint = pointOnLine + QQuaternion::fromEulerAngles(0, rotation, 0).rotatedVector(prevDir) * smoothDistance;
                newHull.emplace_back(smoothingPoint, i);

                if (ridgelineSources.contains(i))
                    newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

                #if DEBUG_IH_FIX
                spawn<DLineMarker>(QVector3D(smoothingPoint.x(), DEBUG_2D_VIEW ? 60.0f : data->height, smoothingPoint.z()), 300, QVector4D(1, 0.5, 0.5, 1));
                #endif
            }

            newHull.emplace_back(point, i);

            if (ridgelineSources.contains(i))
                newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

            // Smoothing point point-next
            if (QVector3D((next + point) / 2).distanceToPoint(point) > smoothDistance * 2)
            {
                QVector3D nextDir = (next - point).normalized();
                QVector3D pointOnLine = point + (nextDir * smoothDistance * 1.2f);

                // Check if valley or ridge
                QVector3D pV = (prev - point).normalized();
                QVector3D nV = (next - point).normalized();
                float angle = angle360(pV, nV);
                float rotation = 270.0f; // right
                if (angle > concaveAngle)
                    rotation = 90.0f; // left

                QVector3D smoothingPoint = pointOnLine + QQuaternion::fromEulerAngles(0, rotation, 0).rotatedVector(nextDir) * smoothDistance;
                newHull.emplace_back(smoothingPoint, i);

                if (ridgelineSources.contains(i))
                    newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

                #if DEBUG_IH_FIX
                spawn<DLineMarker>(QVector3D(smoothingPoint.x(), DEBUG_2D_VIEW ? 60.0f : data->height, smoothingPoint.z()), 300, QVector4D(1, 0.5, 0.5, 1));
                #endif
            }

            if(i != currentHull.size() - 1 || !indicesToPrettify.contains(0))
            {
                QVector3D newPoint = (next + point) / 2;
                newHull.emplace_back(newPoint, i);

                if (ridgelineSources.contains(i))
                    newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

                #if DEBUG_IH_FIX
                spawn<DLineMarker>(QVector3D(newPoint.x(), DEBUG_2D_VIEW ? 60.0f :  data->height, newPoint.z()), 300, QVector4D(1, 1, 1, 1));
                #endif
            }
        }
        else if (indicesToDelete.contains(i))
        {
            // Skip adding a point marked for delete, thus eliminating it from the hull
            #if DEBUG_IH_FIX
            spawn<DLineMarker>(QVector3D(point.x(), DEBUG_2D_VIEW ? 60.0f : data->height, point.z()), 500, QVector4D(1, 0, 0, 1));
            #endif
            continue;
        }
        else if (indicesToLinebreak.contains(i) && !indicesToPrettify.contains(ihc.findIdx(i, -1)))
        {
            auto&& prev = currentHull[ihc.findIdx(i, -1)];
            QVector3D newPoint = (prev + point) / 2;
            newHull.emplace_back(newPoint, i);

            if (ridgelineSources.contains(i))
                newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

            newHull.emplace_back(point, i);

            if (ridgelineSources.contains(i))
                newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));

            #if DEBUG_IH_FIX
            spawn<DLineMarker>(QVector3D(point.x(), DEBUG_2D_VIEW ? 60.0f : data->height, point.z()), 500, QVector4D(0.5, 0.5, 1, 1));
            #endif
        }
        else if (indicesToMakeDomainCorner.contains(i))
        {
            newHull.emplace_back(point, i);
            auto&& next = currentHull[ihc.findIdx(i, 1)];
            QVector3D newPoint = (next + point) / 2;
            QVector3D nextToPrev = (point - next).normalized();
            newPoint += rotateLeft90.rotatedVector(nextToPrev) * (GVector2D(point).dist(GVector2D(next)) / 2);
            newHull.emplace_back(newPoint, i);
        }
        else
        {
            newHull.emplace_back(point, i);

            if (ridgelineSources.contains(i))
                newRidgelineSources.emplace(int(newHull.size()) - 1, ridgelineSources.at(i));
        }
    }

    std::vector<QVector3D> newPts(newHull.size());
    std::vector<IHSrcInfo> newSources(newHull.size());
    std::vector<float> newIncrements(newHull.size());
    std::vector<std::pair<float, bool>> newModifications(newHull.size());

    auto&& sources = data->sources;
    auto&& increments = data->increments;
    auto&& modifications = data->modifiedBy;

    for (int i = 0; i < newHull.size(); ++i)
    {
        newPts[i] = newHull[i].first;
        newSources[i] = sources[newHull[i].second];
        float increment = GVector2D(newPts[i]).dist(GVector2D(newSources[i].getPoint()));
        newIncrements[i] = increment;
        newModifications[i] = modifications[newHull[i].second];
    }

    data->pts = newPts;
    sources = newSources;
    increments = newIncrements;
    modifications = newModifications;
    data->ridgelineSources = newRidgelineSources;
}

void ContourLines::findAndDeleteDuplicatePoints(IHProtoData* data)
{
    OmniProfile("Find And Delete Duplicate Points");

    const float minimumDistance = 3.0f;
    auto& pts = data->pts;
    auto& sources = data->sources;
    auto& increments = data->increments;
    auto& modifications = data->modifiedBy;
    auto& ridgelineSources = data->ridgelineSources;
    static const IHSrcInfo markedForDelete = { nullptr, -2 };

    tbb::parallel_for(0, int(pts.size() - 1), [&](int idx1)
        {
            for (int idx2 = idx1 + 1; idx2 < pts.size(); ++idx2)
            {
                if(GVector2D(pts[idx1]).dist(GVector2D(pts[idx2])) < minimumDistance)
                {
                    #if DEBUG_IH_FIX
                    spawn<DLineMarker>(QVector3D(pts[idx1].x(), DEBUG_2D_VIEW ? 60.0f : data->height, pts[idx1].z()), 500, QVector4D(0, 0, 0, 1));
                    #endif

                    pts[idx1] = {};
                    sources[idx1] = markedForDelete;
                    increments[idx1] = -1.0f;
                    modifications[idx1] = { -9999.0f, false };
                }
            }
        });

    std::unordered_map<int, QVector3D> newRidgelineSources;
    int idx = 0;
    for (int i = 0; i < pts.size(); ++i)
    {
        if (pts[i] == QVector3D())
            continue;

        if (ridgelineSources.contains(i))
            newRidgelineSources.emplace(idx, ridgelineSources.at(i));

        idx++;
    }
    ridgelineSources = newRidgelineSources;

    pts = removeAll(pts, QVector3D());
    sources = removeAll(sources, markedForDelete);
    increments = removeAll(increments, (-1.0f));
    modifications = removeAll(modifications, std::pair(-9999.0f, false));
}

std::unordered_map<QVector3D, std::pair<std::vector<QVector3D>, qint64>>  ContourLines::findRidgePoints(const QSharedPointer<DRidgeMarker>& intersectionRidge, const QVector3D& intersectionPoint, float height, int firstIdx, bool isForwardDirection)
{
    OmniProfile("Find Ridge Points");

    std::unordered_map<QVector3D /* starting point*/, std::pair<std::vector<QVector3D>/*ridge shape*/, qint64 /*ridge Id*/>> shapeMap;
    std::vector<QSharedPointer<DRidgeMarker>> ridges;

    auto searchForPoints = [&ridges](QSharedPointer<DRidgeMarker> ridge, int startingIdx, bool isForwardSearch, float desiredHeight, float previousHeight) -> std::optional<std::vector<QVector3D>>
    {
        auto&& heights = ridge->getHeights();
        auto&& ridgePoints = ridge->getControlPoints();
        auto&& children = ridge->getChildren();
        std::vector<QVector3D> ridgeShape;

        int endIdx = isForwardSearch ? ridgePoints.size() - 1 : 0;
        float prevHeight = previousHeight;

        // Look for ridge point with desired height and gather all point in between it and starting idx
        for (int i = startingIdx; i != endIdx; i += isForwardSearch ? 1 : -1)
        {
            int nextIdx = i + (isForwardSearch ? 1 : -1);

            // Each point of the ridgeline should be lower or equal to its previous point, otherwise it is a peak and the search should end there
            if (fCmp(heights[nextIdx], prevHeight + 10.0f) == std::strong_ordering::greater)
            {
                if (ridgeShape.size() > 1)
                    return ridgeShape;
                else
                    return {};
            }

            prevHeight = heights[nextIdx];

            // Find subridges originating from points gathered
            if (!children.empty())
            {
                auto&& results = children | std::views::filter([i](const auto& child) {return child->getSourcePointIdx() == i; });
                for (auto&& child : results)
                    ridges.emplace_back(child);
            }

            if (desiredHeight < heights[i] && desiredHeight < heights[nextIdx])
            {
                ridgeShape.emplace_back(QVector3D(ridgePoints[i].x(), 0.0f, ridgePoints[i].z()));
                continue;
            }

            // If for any reason absolute precision of snap point to ridgeline is required, change this 
            // (as it simply takes the midpoint between points, considering the small incrementation of ridge points)
            QVector3D point = (ridgePoints[i] + ridgePoints[nextIdx]) / 2;
            point.setY(0.0f);
            ridgeShape.emplace_back(point);

            return ridgeShape;
        }

        if (ridgeShape.empty())
            return {};
        else
            return  ridgeShape;
    };

    // Search the first ridge
    auto&& firstRidgeShape = searchForPoints(intersectionRidge, firstIdx, isForwardDirection, height, intersectionRidge->getHeights()[firstIdx]);

    if (firstRidgeShape)
        shapeMap.emplace(intersectionPoint, std::pair(*firstRidgeShape, intersectionRidge->getGuid()));

    std::vector<QSharedPointer<DRidgeMarker>> ridgesToCheck = ridges;

    // After gathering the points from the first ridge, check all eligible subridges originating from points gathered
    // (and any subridges originating from them)
    while (true)
    {
        for (auto&& ridge : ridgesToCheck)
        {
            if (auto&& ridgeShape = searchForPoints(ridge, 1, true, height, ridge->getHeights().front()); ridgeShape)
            {
                auto sourcePoint = ridge->getControlPoints().front();
                shapeMap.emplace(QVector3D(sourcePoint.x(), 0.0f, sourcePoint.z()), std::pair(*ridgeShape, ridge->getGuid()));
            }
        }

        if (ridges.empty())
            break;

        ridgesToCheck.clear();
        ridgesToCheck = std::move(ridges);
        ridges.clear();
    }

    return shapeMap;
}

auto ContourLines::checkForIntersectionWithRidge(const std::unordered_set<qint64>& rootRidges, const std::vector<QVector3D>& currentHull, const float height) -> std::vector<RidgelineDirection>
{
    OmniProfile("Intersection With Ridge Checks");

    std::vector<RidgelineDirection> intersections;
    std::vector<QSharedPointer<DRidgeMarker>> ridges;
    for (auto&& ridgeId : rootRidges)
        ridges.emplace_back(Generation::Data::get()->findMarkerByGuid<DRidgeMarker>(ridgeId));

    while (true)
    {
        std::vector<QSharedPointer<DRidgeMarker>> nextRidges;
        for (auto&& ridge : ridges)
        {
            auto&& heights = ridge->getHeights();
            auto&& ridgePoints = ridge->getControlPoints();

            std::mutex ridgeGuard;

            tbb::parallel_for(tbb::blocked_range<int>(0, ridgePoints.size() - 1),
                [&](tbb::blocked_range<int>& r)
                {
                    for (int i = r.begin(); i != r.end(); ++i)
                    {
                        Segment2D ridgeSegment = { GVector2D(ridgePoints[i]), GVector2D(ridgePoints[i + 1]) };
                        for (int j = 0; j < currentHull.size(); ++j)
                        {
                            int nextPoint = j + 1 == currentHull.size() ? 0 : j + 1;
                            Segment2D hullSegment = { GVector2D(currentHull[j]), GVector2D(currentHull[nextPoint]) };
                            if (ridgeSegment.intersects(hullSegment, true))
                            {
                                auto&& point = ridgeSegment.getIntersectionPoint(hullSegment);
                                if (!point)
                                    continue;

                                QVector3D intersectionPoint(point->x, 0.0f, point->z);
                                GVector2D hullVec = (GVector2D(currentHull[nextPoint]) - GVector2D(currentHull[j])).normalized();
                                GVector2D ridgeVec1 = (GVector2D(ridgePoints[i]) - GVector2D(intersectionPoint)).normalized();
                                GVector2D ridgeVec2 = (GVector2D(ridgePoints[i + 1]) - GVector2D(intersectionPoint)).normalized();

                                // If intersection point and ridge point are the same, try to find the next/prev point to get intersection direction, otherwise discard result
                                if (qAbs(ridgePoints[i].x() - intersectionPoint.x()) < 2.0f && qAbs(ridgePoints[i].z() - intersectionPoint.z()) < 2.0f)
                                {
                                    if (i - 1 >= 0)
                                        ridgeVec1 = (GVector2D(ridgePoints[i - 1]) - GVector2D(intersectionPoint)).normalized();
                                    else
                                    {
                                        #if DEBUG_IH_RIDGE_FOLLOW
                                        spawn<DLineMarker>(QVector3D(intersectionPoint.x(), DEBUG_2D_VIEW ? 60.0f : height, intersectionPoint.z()), 5000, QVector4D(1, 1, 1, 1));
                                        #endif
                                        continue;
                                    }
                                }

                                if (qAbs(ridgePoints[i + 1].x() - intersectionPoint.x()) < 2.0f && qAbs(ridgePoints[i + 1].z() - intersectionPoint.z()) < 2.0f)
                                {
                                    if (i + 2 < ridgePoints.size())
                                        ridgeVec2 = (GVector2D(ridgePoints[i + 2]) - GVector2D(intersectionPoint)).normalized();
                                    else
                                    {
                                        #if DEBUG_IH_RIDGE_FOLLOW
                                        spawn<DLineMarker>(QVector3D(intersectionPoint.x(), DEBUG_2D_VIEW ? 60.0f : height, intersectionPoint.z()), 5000, QVector4D(1, 1, 1, 1));
                                        #endif
                                        continue;
                                    }
                                }

                                float firstVecAngle = hullVec.angle(ridgeVec1);
                                GVector2D ridgelineVec = firstVecAngle <= 180.0f ? ridgeVec1 : ridgeVec2;
                                int firstIdx = firstVecAngle <= 180.0f ? i : i + 1;
                                bool forwardDirection = firstVecAngle <= 180.0f ? false : true;

                                auto&& ridgePoints = findRidgePoints(ridge, intersectionPoint, height, firstIdx, forwardDirection);
                                if (ridgePoints.empty())
                                {
                                    #if DEBUG_IH_RIDGE_FOLLOW
                                    spawn<DLineMarker>(QVector3D(intersectionPoint.x(), DEBUG_2D_VIEW ? 60.0f : height, intersectionPoint.z()), 100, QVector4D(0, 1, 1, 1));
                                    #endif
                                    continue;
                                }

                                RidgelineDirection data = 
                                { 
                                    .ridgeForwardDirection = forwardDirection,
                                    .prevPointIdx = j,
                                    .nextPointIdx = nextPoint,
                                    .intersectionOnHull= intersectionPoint,
                                    .ridgelinePoints = ridgePoints
                                };

                                #if DEBUG_IH_RIDGE_FOLLOW
                                QVector3D inter(intersectionPoint.x(), DEBUG_2D_VIEW ? 60.0f : height, intersectionPoint.z());
                                QVector3D interVec(intersectionPoint.x() + ridgelineVec.x * 1000, DEBUG_2D_VIEW ? 60.0f : height, intersectionPoint.z() + ridgelineVec.z * 1000);
                                spawn<DLineMarker>(inter, interVec, QVector4D(0,0,0,1), 0.0f, ELineDecorator::Arrow);
                                #endif

                                std::scoped_lock lock(ridgeGuard);

                                // If a ridge segment will go directly through a hull point it will be technically intersected twice, as such the second occurrence is discarded
                                auto&& duplicate = std::find_if(intersections.begin(), intersections.end(), [intersectionPoint](const auto& ele) 
                                    { return  qAbs(ele.intersectionOnHull.x() - intersectionPoint.x()) < 5.0f && qAbs(ele.intersectionOnHull.z() - intersectionPoint.z()) < 5.0f; });

                                auto&& pointDuplicate = std::find_if(intersections.begin(), intersections.end(), [ridgePoints](const auto& ele)
                                    { return ele.ridgelinePoints == ridgePoints; });

                                if (duplicate == intersections.end() && pointDuplicate == intersections.end())
                                    intersections.emplace_back(data);
                            }
                        }
                    }
                });

            if (auto&& children = ridge->getChildren(); !children.empty())
                nextRidges.insert(nextRidges.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
        }

        if (nextRidges.empty())
            break;

        ridges.clear();
        ridges = std::move(nextRidges);
    }

    return intersections;
}

void ContourLines::reshapeHullToRidgeline(const std::vector<RidgelineDirection>& intersectionData, IHProtoData* data)
{
    OmniProfile("Reshaping to ridgeline");
    auto& hull = data->pts;
    auto ihc = asCircular(hull);

    std::vector<std::tuple<QVector3D, int /*old idx*/, float /*ridge slope factor*/, QVector3D /*ridge source point*/>> ridgelineDirectedHull;
    for (int i = 0; i < hull.size(); ++i)
    {
        std::map<float, RidgelineDirection> distanceMap;
        int prevIdx = i == 0 ? hull.size() - 1 : i - 1;

        auto results = intersectionData | std::views::filter([i](const auto& ele) {return ele.nextPointIdx == i; });
        for (auto&& result : results)
        {
            float dist = GVector2D(result.intersectionOnHull).dist(GVector2D(hull[prevIdx]));
            Q_ASSERT(!distanceMap.contains(dist));
            distanceMap.emplace(dist, result);
        }

        for (auto&& it = distanceMap.begin(); it != distanceMap.end(); ++it)
        {
            float increment = GVector2D(hull[it->second.nextPointIdx]).dist(GVector2D(hull[it->second.prevPointIdx])) / 2;
            auto section = getHullSectionFromRidge(it->second, increment);

            auto checkForIntersections = [&]() 
            {
                if (section.size() == 1)
                {
                    Segment2D newSegment(GVector2D(it->second.intersectionOnHull), GVector2D(std::get<0>(section.front())));
                    for (int idx = 0; idx < hull.size(); ++idx)
                    {
                        if (Segment2D(hull[idx], hull[ihc.findIdx(idx, 1)]).intersects(newSegment, false))
                            return true;
                    }
                }

                for (int j = 0; j < section.size() - 1; ++j)
                {
                    Segment2D newSegment(std::get<0>(section[j]), std::get<0>(section[j + 1]));
                    for (int idx = 0; idx < hull.size(); ++idx)
                    {
                        if (Segment2D(hull[idx], hull[ihc.findIdx(idx, 1)]).intersects(newSegment, false))
                            return true;
                    }
                }

                return false;
            };

            if (checkForIntersections())
                continue;

            for (int j = 0; j < section.size(); ++j)
            {
                #if DEBUG_IH_RIDGE_FOLLOW
                spawn<DLineMarker>(QVector3D(std::get<0>(section[j]).x(), DEBUG_2D_VIEW ? 60.0f : data->height, std::get<0>(section[j]).z()), 250, QVector4D(0, 1, 1, 1));
                #endif
                ridgelineDirectedHull.emplace_back(std::get<0>(section[j]), i, std::get<1>(section[j]), std::get<2>(section[j]));
            }
        }

        QVector3D ridgeSource;
        if (data->ridgelineSources.contains(i))
            ridgeSource = data->ridgelineSources.at(i);
        ridgelineDirectedHull.emplace_back(hull[i], i, data->modifiedBy[i].first, ridgeSource);
    }

    // Apply proper slope factors to unchanged segments, based on new sections
    std::optional<std::pair<int, int>> unchangedSegment;
    if (hull.size() < ridgelineDirectedHull.size())
    {
        // Find new section fragments
        int prevIdx = -1;
        std::set<int> newSectionStartingPoints;
        auto newIhc = asCircular(ridgelineDirectedHull);
        for (int idx = 0; idx < ridgelineDirectedHull.size(); ++idx)
        {
            int originIdx = std::get<1>(ridgelineDirectedHull[idx]);
            if (!newSectionStartingPoints.contains(originIdx) && originIdx == prevIdx)
                newSectionStartingPoints.emplace(originIdx);
            else
                prevIdx = originIdx;
        }

        // Check if neighboring points of new section are of proper factor
        if (data->slopeFactorForPeakApplied)
        {
            for (auto&& newSectionIdx : newSectionStartingPoints)
            {
                int leftIdx = -1;
                int rightIdx = -1;
                for (int idx = 0; idx < ridgelineDirectedHull.size(); ++idx)
                {
                    if (std::get<1>(ridgelineDirectedHull[idx]) == newSectionIdx && leftIdx == -1)
                    {
                        leftIdx = idx;
                        continue;
                    }

                    if (std::get<1>(ridgelineDirectedHull[idx]) == newSectionIdx && std::get<1>(ridgelineDirectedHull[newIhc.findIdx(idx, 1)]) != newSectionIdx)
                    {
                        rightIdx = newIhc.findIdx(idx, -1);
                        break;
                    }
                }

                std::get<2>(ridgelineDirectedHull[newIhc.findIdx(leftIdx, -1)]) = std::get<2>(ridgelineDirectedHull[leftIdx]);
                std::get<2>(ridgelineDirectedHull[newIhc.findIdx(rightIdx, 1)]) = std::get<2>(ridgelineDirectedHull[rightIdx]);
            }

            goto skip;
        }

        std::vector<std::pair<int, int>> segments;
        if (newSectionStartingPoints.size() == 1)
        {
            int idx = *newSectionStartingPoints.begin();
            segments.emplace_back(idx, ihc.findIdx(idx, std::floor(data->pts.size() / 2)));
            segments.emplace_back(segments.front().second, idx);
        }
        else
        {
            // Get all segments between new fragments
            std::vector<int> startingPoints(newSectionStartingPoints.begin(), newSectionStartingPoints.end());
            for (int i = 0; i < startingPoints.size(); ++i)
            {
                int next = i < startingPoints.size() - 1 ? i + 1 : 0;
                segments.emplace_back(startingPoints[i], startingPoints[next]);
            }
        }

        // Assign each segment the proper factor
        for (auto&& segment : segments)
        {
            int startingIdx;
            for (int idx = 0; idx < ridgelineDirectedHull.size(); ++idx)
                if (std::get<1>(ridgelineDirectedHull[idx]) == segment.first && std::get<1>(ridgelineDirectedHull[newIhc.findIdx(idx, 1)]) != segment.first)
                {
                    startingIdx = idx;
                    break;
                }

            int endIdx;
            // Find the first new point after 
            for (int idx = 0; idx < ridgelineDirectedHull.size(); ++idx)
                if (std::get<1>(ridgelineDirectedHull[idx]) == segment.second)
                {
                    endIdx = idx;
                    break;
                }

            float properFactor;
            if (newSectionStartingPoints.size() == 1 && endIdx != *newSectionStartingPoints.begin())
                properFactor = std::get<2>(ridgelineDirectedHull[newIhc.findIdx(startingIdx, -1)]);
            else
                properFactor = std::get<2>(ridgelineDirectedHull[endIdx]);

            // The last point of the segment should not have its factor modified, as its position in the new hull will be after, not before the new segment
            for(int idx = startingIdx; idx != endIdx; idx = newIhc.findIdx(idx, 1))
                std::get<2>(ridgelineDirectedHull[idx]) = properFactor;
        }

        data->slopeFactorForPeakApplied = true;
    }

    skip:

    std::vector<QVector3D> newPts(ridgelineDirectedHull.size());
    std::vector<IHSrcInfo> newSources(ridgelineDirectedHull.size());
    std::vector<float> newIncrements(ridgelineDirectedHull.size());
    std::vector<std::pair<float, bool>> newModifications(ridgelineDirectedHull.size());
    std::unordered_map<int, QVector3D> newRidgelineSources;

    auto& sources = data->sources;
    auto& increments = data->increments;
    auto& modifications = data->modifiedBy;
    auto& ridgelineSources = data->ridgelineSources;

    for (int i = 0; i < ridgelineDirectedHull.size(); ++i)
    {
        newPts[i] = std::get<0>(ridgelineDirectedHull[i]);
        newSources[i] = sources[std::get<1>(ridgelineDirectedHull[i])];
        newIncrements[i] = increments[std::get<1>(ridgelineDirectedHull[i])];
        newModifications[i] = {std::get<2>(ridgelineDirectedHull[i]), true};
        if (auto&& rs = std::get<3>(ridgelineDirectedHull[i]); rs != QVector3D())
            newRidgelineSources.emplace(i, rs);
    }

    data->pts = newPts;
    sources = newSources;
    increments = newIncrements;
    modifications = newModifications;
    ridgelineSources = newRidgelineSources;
}

void ContourLines::findAndDeleteLoopsOfHull(IHProtoData* data)
{
    OmniProfile("Find And Delete Hull Loops");

    std::multimap<int /*loop size*/, std::pair<int, int>> loopMap;
    std::mutex guard;
    auto ihc = asCircular(data->pts);

    tbb::parallel_for(0, int(data->pts.size() - 1), [&](int idx)
        {
            Segment2D firstSegment(data->pts[idx], data->pts[ihc.findIdx(idx, 1)]);
            for (int otherIdx = idx + 1; otherIdx < data->pts.size(); ++otherIdx)
            {
                int leftIdx = idx;
                int rightIdx = otherIdx;
                bool intersection = false;

                Segment2D secondSegment(data->pts[otherIdx], data->pts[ihc.findIdx(otherIdx, 1)]);
                if (!firstSegment.intersects(secondSegment, false))
                    continue;

                #if DEBUG_IH_FIX
                std::vector<QVector3D> first({ QVector3D(firstSegment.first.x, DEBUG_2D_VIEW ? 80.0f : data->height + 20, firstSegment.first.z) , QVector3D(firstSegment.second.x, DEBUG_2D_VIEW ? 80.0f : data->height + 20, firstSegment.second.z) });
                std::vector<QVector3D> second({ QVector3D(secondSegment.first.x, DEBUG_2D_VIEW ? 80.0f : data->height + 20, secondSegment.first.z), QVector3D(secondSegment.second.x, DEBUG_2D_VIEW ? 80.0f : data->height + 20, secondSegment.second.z) });
                spawn<DLineMarker>(first, QVector4D(1, 1, 0, 1));
                spawn<DLineMarker>(second, QVector4D(1, 1, 0, 1));

                auto intPoint = firstSegment.getIntersectionPoint(secondSegment);
                if(intPoint)
                {
                    QVector3D p(intPoint->x, DEBUG_2D_VIEW ? 60.0f : data->height, intPoint->z);
                    spawn<DLineMarker>(p, 400, QVector4D(1, 1, 0, 0.5));
                }
                #endif

                rightIdx = ihc.findIdx(otherIdx, 1);

                // For bay recognition the pairs are checked in a clockwise fashion, so left idx < right idx
                // that is untrue for the edge case where a point from the end and start of array are close enough
                // as such this swaps them
                if (ihc.distCW(idx, otherIdx) > ihc.distCW(otherIdx, idx))
                {
                    leftIdx = otherIdx;
                    rightIdx = intersection ? ihc.findIdx(idx, 1) : idx;
                }

                #if DEBUG_IH_FIX
                QVector3D leftP(data->pts[leftIdx].x(), DEBUG_2D_VIEW ? 60.0f : data->height, data->pts[leftIdx].z());
                QVector3D rightP2(data->pts[rightIdx].x(), DEBUG_2D_VIEW ? 60.0f : data->height, data->pts[rightIdx].z());
                QVector3D rightP(data->pts[rightIdx].x(), DEBUG_2D_VIEW ? 135.0f : data->height + 75, data->pts[rightIdx].z());
                spawn<DLineMarker>(leftP, 75, QVector4D(1, 0, 1, 1));
                spawn<DLineMarker>(rightP, 25, QVector4D(0, 0, 0, 1));
                spawn<DLineMarker>(rightP2, 75, QVector4D(0, 0, 0, 0.3));
                #endif

                std::scoped_lock lock(guard);
                loopMap.emplace(ihc.distCW(leftIdx, rightIdx), std::pair(leftIdx, rightIdx));
            }
        });

    std::unordered_set<int> claimedPoints;
    std::vector<std::pair<int, int>> finalPairs;

    auto claimPoints = [&](int startIdx, int endIdx)
    {
        int idx = ihc.findIdx(startIdx, 1);
        while (idx != endIdx)
        {
            claimedPoints.emplace(idx);
            idx = ihc.findIdx(idx, 1);
        }
    };

    for (auto&& [distance, mergePair] : loopMap)
    {
        // Check if both of the intersection points are already claimed by any shorter bay - invalidating the current pair
        if (claimedPoints.contains(mergePair.first) && claimedPoints.contains(mergePair.second))
        {
            int leftIdx = mergePair.first;
            int rightIdx = mergePair.second;
            continue;
        }

        claimPoints(mergePair.first, mergePair.second);
        finalPairs.emplace_back(mergePair.first, mergePair.second);
    }

    auto oldHull = data->pts;
    auto& hull = data->pts;
    auto& sources = data->sources;
    auto& increments = data->increments;
    auto& modifications = data->modifiedBy;
    auto& ridgelineSources = data->ridgelineSources;
    static const IHSrcInfo markedForDelete = { nullptr, -2 };

    #if DEBUG_IH_FIX
    for (auto&& [leftIdx, rightIdx] : finalPairs)
    {
        for(int i = leftIdx; i != rightIdx; i = ihc.findIdx(i, 1))
        {
            std::vector<QVector3D> vec({ QVector3D(hull[i].x(), DEBUG_2D_VIEW ? 60.0f : data->height, hull[i].z()) , QVector3D(hull[ihc.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 60.0f : data->height, hull[ihc.findIdx(i, 1)].z()) });
            spawn<DLineMarker>(vec, QVector4D(0, 0, 0, 1));
        }
    }
    #endif

    for (auto&& [leftIdx, rightIdx] : finalPairs)
    {
        for (int idx = ihc.findIdx(leftIdx, 1); idx != rightIdx; idx = ihc.findIdx(idx, 1))
        {
            hull[idx] = {};
            sources[idx] = markedForDelete;
            increments[idx] = -1.0f;
            modifications[idx] = { -9999.0f, false };
        }
    }

    std::unordered_map<int, QVector3D> newRidgelineSources;
    int idx = 0;
    for (int i = 0; i < data->pts.size(); ++i)
    {
        if (hull[i] == QVector3D())
            continue;

        if (ridgelineSources.contains(i))
            newRidgelineSources.emplace(idx, ridgelineSources.at(i));

        idx++;
    }
    ridgelineSources = newRidgelineSources;

    hull = removeAll(hull, QVector3D());
    sources = removeAll(sources, markedForDelete);
    increments = removeAll(increments, (-1.0f));
    modifications = removeAll(modifications, std::pair(-9999.0f, false));

    Q_ASSERT(hull.size() == sources.size());
    Q_ASSERT(increments.size() == sources.size());
    Q_ASSERT(modifications.size() == sources.size());
}

std::vector<std::tuple<QVector3D, float, QVector3D>> ContourLines::getHullSectionFromRidge(const RidgelineDirection& ridgeData, const float increment)
{
    OmniProfile("Get hull section from ridge");

    QVector3D intersectionPoint = ridgeData.intersectionOnHull;
    auto&& ridgeShape = ridgeData.ridgelinePoints;
    auto&& rootShape = find_if(ridgeShape.begin(), ridgeShape.end(), [&intersectionPoint](const auto& ele) {return ele.first == intersectionPoint; });

    if (rootShape == ridgeShape.end())
        return { std::tuple(intersectionPoint, 1.0f, QVector3D()) };

    // Having the ridge shape create a new hull section by calling generateNewHullPoint for each point
    // The shape vector was not simply reverse copied to achieve circular shape similar to how lvl 0 isohypses are handled
    // This is due to the fact that the ridge final shape is yet unknown (where the subridges branch out)
    // As each subridge is a separate vector with its source point as a map key
    std::function<std::vector<std::tuple<QVector3D, float, QVector3D>>(const QVector3D&, const std::vector<QVector3D>&, qint64, bool, bool)> getShape = [&](const QVector3D& sourcePoint, const std::vector<QVector3D>& sourceShape, qint64 ridgeId, bool reversed, bool ridgeForwardDirection)
    {
        std::vector<std::tuple<QVector3D /*new point*/, float /*ridgeline slope factor*/, QVector3D/*ridge source point*/>> newPoints;
        std::vector<QSharedPointer<DRidgeMarker>> ridges;
        Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
        auto&& ridge = find_if(ridges.begin(), ridges.end(), [&](const auto& ele) { return ele->getGuid() == ridgeId; });
        Q_ASSERT(ridge != ridges.end());
        // ridgeForwardDirection check is for the peak case, where the section goes "in the opposite direction" to what would be expected
        // thus the left factor is actually the right factor, and vice versa
        float leftFactor = ridgeForwardDirection ? (*ridge)->getLeftSlopeFactor() : (*ridge)->getRightSlopeFactor();
        float rightFactor = ridgeForwardDirection ? (*ridge)->getRightSlopeFactor() : (*ridge)->getLeftSlopeFactor();

        for (int i = 0; i < sourceShape.size(); ++i)
        {
            GVector2D prevPoint, nextPoint;

            if (i == 0)
                prevPoint = sourcePoint;
            else
                prevPoint = sourceShape[i - 1];

            if (reversed && i == sourceShape.size() - 1)
                return newPoints;

            // Gather branches that are on the left of the currently created part of the section
            std::map<float, std::pair<const std::vector<QVector3D>&, qint64>> branchesMap;
            auto&& branches = ridgeShape | std::views::filter([&](const auto& ele) {return ele.first == sourceShape[i]; });
            for (auto&& branch : branches)
            {
                auto branchVector = (branch.second.first.front() - sourceShape[i]).normalized();
                auto reversedPrevVector = (QVector3D(prevPoint.x, 0.0f, prevPoint.z) - sourceShape[i]).normalized();
                float minAngle = 0.0f;
                if (i < sourceShape.size() - 1)
                {
                    auto next = sourceShape[i + 1];
                    auto nextVector = (next - sourceShape[i]).normalized();
                    minAngle = angle360(reversedPrevVector, nextVector);
                }

                float branchAngle = angle360(reversedPrevVector, branchVector);
                if (branchAngle > minAngle)
                    branchesMap.emplace(branchAngle, branch.second);
            }

            // If a right branch exist, get its shape first and put it before continuing with this ridge part 
            // (also omit creating a new point for current ridge)
            bool branched = false;
            if (!branchesMap.empty())
            {
                branched = true;
                for (auto branchIt = branchesMap.rbegin(); branchIt != branchesMap.rend(); ++branchIt)
                {
                    auto newPts = getShape(sourceShape[i], branchIt->second.first, branchIt->second.second, false, ridgeForwardDirection);
                    for (auto&& pt : newPts)
                        newPoints.emplace_back(pt);
                }
            }

            // End of shape
            if (i == sourceShape.size() - 1)
            {
                if (reversed)
                    return newPoints;

                if(!branched)
                {
                    auto ridgeVec = (sourceShape[i] - prevPoint).normalized();
                    QVector3D endPoint = sourceShape[i] + (ridgeVec * increment);

                    // Make two endpoints instead of a single, to properly propagate slope factor to future points
                    auto rightPoint = endPoint + (QQuaternion::fromEulerAngles(0, 270.0f, 0).rotatedVector(ridgeVec) * increment * 0.25);
                    auto leftPoint = endPoint + (QQuaternion::fromEulerAngles(0, 90.0f, 0).rotatedVector(ridgeVec) * increment * 0.25);

                    newPoints.emplace_back(rightPoint, rightFactor, sourceShape[i]);
                    newPoints.emplace_back(leftPoint, leftFactor, sourceShape[i]);
                }

                // The shape needs to go back, as such it's reversed, the (current) endpoint is poped, 
                // the previous source point added as the final point (to be omitted, but for proper direction vector) 
                // and the current endpoint will be the new source point
                std::vector<QVector3D> newSourceShape = sourceShape;
                newSourceShape.pop_back();
                std::reverse(newSourceShape.begin(), newSourceShape.end());
                newSourceShape.emplace_back(sourcePoint);
                QVector3D newSource = sourceShape.back();

                // Get the reversed shape
                if(newSourceShape.size() == 1)
                    return newPoints;

                auto newPts = getShape(newSource, newSourceShape, ridgeId, true, ridgeForwardDirection);
                for (auto&& pt : newPts)
                    newPoints.emplace_back(pt);

                return newPoints;
            }
            else if (!branched)
            {
                nextPoint = sourceShape[i + 1];
                auto newHullPoint = generateNewHullPoint(prevPoint, sourceShape[i], nextPoint, increment);
                newPoints.emplace_back(newHullPoint, reversed ? leftFactor : rightFactor, sourceShape[i]);
            }
        }

        return newPoints;
    };

    return getShape(intersectionPoint, rootShape->second.first, rootShape->second.second, false, ridgeData.ridgeForwardDirection);
}

void ContourLines::computeGroupParamFulfillment(std::vector<IHProtoData>* ihProtoData, const std::vector<bool>& modifiedByRidgeline)
{
    std::unordered_map<qint64, std::vector<std::pair<IndexType, std::vector<QVector3D>>>> landCoverage;

    for (int i = 0; i < ihProtoData->size(); ++i)
    {
        auto&& domainGuid = (*ihProtoData)[i].usedDomainId;
        auto&& it = landCoverage.find(domainGuid);
        if (it != landCoverage.end())
        {
            landCoverage.at(domainGuid).emplace_back(i , (*ihProtoData)[i].pts);
            continue;
        }

        landCoverage[domainGuid] = { {i, (*ihProtoData)[i].pts} };
    }

    auto processTableland = [&](IndexType idx, const TablelandParams& params)
    {
        if (!(*ihProtoData)[idx].currentDropLvl)
        {
            (*ihProtoData)[idx].currentDropLvl = 0;
            (*ihProtoData)[idx].desiredDropLvl = params.desiredPrecipiceSteps;
        }
        else if ((*ihProtoData)[idx].currentDropLvl < (*ihProtoData)[idx].desiredDropLvl)
            (*ihProtoData)[idx].currentDropLvl = *(*ihProtoData)[idx].currentDropLvl + 1;
    };

    for (auto&& [domainGuid, coverageData] : landCoverage)
    {
        auto&& domain = Generation::Data::get()->findDomainByGuid(domainGuid);
        Q_ASSERT(domain);
        auto&& domainData = (*domain)->getData<EDomainType::Terrain>();

        // Skip plains as openness should not affect them
        if (domainData->landform == ELandform::Plains || domainData->landform == ELandform::RuggedPlains)
            continue;

        float openness = domainData->landformOpenness * 0.1f;
        float desiredCoverage = domainData->desiredRidgeCoverage;

        auto&& domainPolygons = PolygonUtils::calculatePolygonsFromGridSquares((*domain)->getSquares());
        float finalCoverage = 0.0f;
        for (auto&& [ihIdx, polygon] : coverageData)
        {
            auto coverage = PolygonUtils::coverage(polygon, domainPolygons);
            finalCoverage += (std::accumulate(coverage.begin(), coverage.end(), 0.0f) / float(coverage.size()));
            Q_ASSERT(finalCoverage >= 0.0f || finalCoverage <= 1.0f);

            // For Tablelands, each "form" achieves its desired coverage separately
            if (domainData->landform == ELandform::Tablelands)
            {
                float tablelandArea = PolygonUtils::calculateArea(polygon);
                ETableLand currentForm = *(*ihProtoData)[ihIdx].tablelandType;
                auto&& landformVariation = domainData->landformVariation;
                float desiredArea = PTablelandTypes[landformVariation][currentForm].desiredFormSize;
                Q_ASSERT(desiredArea > 0.0f);

                // Desired Area can be both a flat number (polygon size) or a percentage of area coverage to scale with domain size (Plateau)
                if(desiredArea < 1.0f)
                {
                    if (std::accumulate(coverage.begin(), coverage.end(), 0.0f) > desiredArea)
                        processTableland(ihIdx, PTablelandTypes[landformVariation][currentForm]);
                }
                else if(tablelandArea > desiredArea)
                    processTableland(ihIdx, PTablelandTypes[landformVariation][currentForm]);
            }
        }

        if (domainData->landform == ELandform::Tablelands)
            continue;

        float modifiedPercent =  std::accumulate(modifiedByRidgeline.begin(), modifiedByRidgeline.end(), 0.0f) / float(modifiedByRidgeline.size());
        if (finalCoverage > desiredCoverage && modifiedPercent <= 0.1f)
            for (auto&& [ihIdx, unused] : coverageData)
                    (*ihProtoData)[ihIdx].groupingFactor = 1.0f + (openness * 10);
    }
}

void ContourLines::modifySegmentByValue(IHProtoData* data, const IHProtoData& oldData, const std::pair<int, int>& segment, const float modifyValue, const bool tendency)
{
    auto& hull = data->pts;
    auto& increments = data->increments;
    auto& sources = data->sources;
    auto& modifications = data->modifiedBy;

    auto ihc = asCircular(hull);

    auto modifyPoint = [&](int idx) 
    {
        #if DEBUG_IH_FIX
        QVector3D oldPos(hull[idx].x(), DEBUG_2D_VIEW ? 60.0f : data->height, hull[idx].z());
        #endif

        auto prevIdx = ihc.findIdx(idx, -1);
        auto nextIdx = ihc.findIdx(idx, 1);
        QVector3D nextToPrev = (ihc[prevIdx] - ihc[nextIdx]).normalized();
        int sourceIdx = data->sources[idx].idx;
        float changeDelta = (modifyValue * increments[idx]) - increments[idx];

        if (increments[idx] + changeDelta < baseIncrement)
            changeDelta = baseIncrement - increments[idx];

        hull[idx] += rotateLeft90.rotatedVector(nextToPrev) * changeDelta;
        increments[idx] += changeDelta;
        modifications[idx] = { modifyValue , tendency };

        #if DEBUG_IH_FIX
        spawn<DLineMarker>(oldPos, QVector3D(hull[idx].x(), DEBUG_2D_VIEW ? 60.0f : data->height, hull[idx].z()), QVector4D(0, 1, 0, 1), 0.0f, ELineDecorator::Arrow);
        #endif
    };

    ihc.forRangeCW(segment.first, segment.second, modifyPoint);
}

void ContourLines::findAndModifySegments(IHProtoData* data, const IHProtoData& oldData)
{
    auto& hull = data->pts;
    auto& modifications = data->modifiedBy;
    auto ihc = asCircular(hull);
    std::vector<std::pair<int, int>> straightSegments;

    bool loopedWithFront = false;
    for (int idx = 0; idx < hull.size(); ++idx)
    {
        int prevIdx = ihc.findIdx(idx, -1);
        int nextIdx = ihc.findIdx(idx, 1);

        QVector3D prevToCurrent = (ihc[idx] - ihc[prevIdx]).normalized();
        QVector3D currentToNext = (ihc[nextIdx] - ihc[idx]).normalized();

        // Prevents point modification when out of domain bounds
        GPoint sq = GVector2D(hull[idx]).toGPoint();
        auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
        if (!domain)
            continue;

        // Find straight segments
        if (float angle = angle180(prevToCurrent, currentToNext); angle < 30.0f)
        {
            // Check if last segment does not connect with the very first
            if (idx == hull.size() - 1)
            {
                auto&& result = std::find_if(straightSegments.begin(), straightSegments.end(), [&](const auto& ele) {return ele.first == nextIdx; });
                if (result != straightSegments.end())
                    loopedWithFront = true;
            }

            // Check if current segment is a continuation of a different segment
            auto result = std::find_if(straightSegments.begin(), straightSegments.end(), [&](const auto& ele) {return ele.second == idx; });
            if (result != straightSegments.end())
            {
                if (loopedWithFront)
                {
                    straightSegments[0].first = result->first;
                    straightSegments.erase(result);
                }
                else
                    *result = { result->first, nextIdx };
            }
            else if (loopedWithFront)
                straightSegments[0].first = prevIdx;
            else
                straightSegments.emplace_back(prevIdx, nextIdx);
        }
    }

    for (auto&& segment : straightSegments)
    {
        std::vector<float> modValues;
        std::vector<float> increments;
        std::vector<bool> factorTendency;
        for (int idx = segment.first; idx != ihc.findIdx(segment.second, 1); idx = ihc.findIdx(idx, 1))
        {
            modValues.emplace_back(modifications[idx].first);
            increments.emplace_back(data->increments[idx]);
            factorTendency.emplace_back(modifications[idx].second);
        }

        float averageModification = std::accumulate(modValues.begin(), modValues.end(), 0.0f) / modValues.size();
        float averageInc = std::accumulate(increments.begin(), increments.end(), 0.0f) / increments.size();
        bool forwardTendency = std::accumulate(factorTendency.begin(), factorTendency.end(), 0) < factorTendency.size() / 2 ? false : true;

        hybrid_int_distribution<int> rndInc(-1, 10, 0.5f, forwardTendency == true ? 0.9 : 0.35);

        float randomInc = static_cast<float>(rndInc(Generation::gRandomEngine)) / 100.0f;
        modifySegmentByValue(data, oldData, segment, std::clamp(averageModification + randomInc, 0.1f, 2.0f), randomInc < 0.0f ? false : true);
    }
}

bool ContourLines::checkFlatlandsAgainstOthers(const std::vector<IHProtoData>& allData)
{
    auto&& currentIhData = allData.back();
    std::vector<Polygon2D> otherIhs;
    for (auto&& data : allData)
    {
        if (data == allData.back())
            continue;

        otherIhs.emplace_back(Polygon2D(data.pts));
    }

    for (auto&& pt : currentIhData.pts)
        for (auto&& poly : otherIhs)
            if (poly.contains(pt))
                return false;

    // This prevents a scenario where a lvl 1 Ih is expanded all the way to a ridgeline of a different peak
    // All other scenarios should be recognized during the above checks
    if (currentIhData.heightDeltas.size() <= 1)
    {
        auto&& mainRidges = Generation::Data::get()->getMarkers<DRidgeMarker>();
        Polygon2D currentPoly(currentIhData.pts);

        for (auto&& ridge : mainRidges)
        {
            // Skip own ridge
            if (currentIhData.ridgeIds.contains(ridge->getGuid()))
                continue;

            auto&& cPts = ridge->getControlPoints();
            for (auto&& pt : cPts)
                if (currentPoly.contains(pt))
                    return false;
        }
    }

    return true;
}

void ContourLines::modifyPointsAccordingToOldHulls(IHProtoData* data, const std::vector<std::vector<QVector3D>>& oldHulls)
{
    float fixedIncrement = 5.0f;

    auto ihc = asCircular(data->pts);
    std::vector<CircularVectorView<std::vector, QVector3D>> oldHullsCircular;

    for (auto&& oldHull : oldHulls)
        oldHullsCircular.emplace_back(oldHull);

    std::vector<std::tuple<int /*hullPointIdx*/, int /*otherHullIdx*/, int/*otherHullPointIdx*/>> intersections;
    std::mutex guard;

    // Find any intersections of new hull against old hulls
    tbb::parallel_for(0, int(data->pts.size()), [&](int idx)
        {
            Segment2D segment(data->pts[idx], data->pts[ihc.findIdx(idx, 1)]);
            for (int oldHullIdx = 0; oldHullIdx < oldHulls.size(); ++oldHullIdx)
            {
                auto&& oldHull = oldHullsCircular[oldHullIdx];
                for (int otherHullPointIdx = 0; otherHullPointIdx < oldHulls[oldHullIdx].size(); ++otherHullPointIdx)
                {
                    Segment2D otherSegment(oldHull[otherHullPointIdx], oldHull[oldHull.findIdx(otherHullPointIdx, 1)]);
                    if (segment.intersects(otherSegment, true))
                    {
                        std::scoped_lock lock(guard);
                        intersections.emplace_back(idx, oldHullIdx, oldHull.findIdx(otherHullPointIdx, 1));//otherHullPointIdx);
                    }
                }
            }
        });

    std::unordered_set<int> claimedPoints;

    struct NewSegment 
    {
        int from = -1;
        int to = -1;
        std::vector<QVector3D> newPoints;
        std::vector<int> newSources;
    };

    std::vector<NewSegment> newSegments;
    for (auto&& [hullPointIdx, otherHullIdx, otherHullPointIdx] : intersections)
    {
        if (claimedPoints.contains(hullPointIdx))
            continue;

        // This checks if the actual beginning of the whole intersection segment isn't actually earlier
        int firstPoint = hullPointIdx;
        if (firstPoint == 0)
        {
            int tempPoint = ihc.findIdx(hullPointIdx, -1);

            while(true)
            {
                if (claimedPoints.size() == data->pts.size())
                    return;

                auto&& it = std::find_if(intersections.begin(), intersections.end(), [tempPoint](const auto& ele) {return std::get<0>(ele) == tempPoint; });
                if (it == intersections.end())
                    break;

                claimedPoints.emplace(tempPoint);
                firstPoint = tempPoint;
                tempPoint = ihc.findIdx(tempPoint, -1);
            }
        }

        int finalPoint = hullPointIdx;

        // If point is inside polygon, or on its border it needs to be moved
        int tempPoint = ihc.findIdx(hullPointIdx, 1);
        while (true)
        {
            auto&& it = std::find_if(intersections.begin(), intersections.end(), [tempPoint](const auto& ele) {return std::get<0>(ele) == tempPoint; });
            if (it == intersections.end())
                break;

            claimedPoints.emplace(tempPoint);
            finalPoint = tempPoint;
            tempPoint = ihc.findIdx(finalPoint, 1);
        }

        auto&& firstPointData = std::find_if(intersections.begin(), intersections.end(), [firstPoint](const auto& ele) {return std::get<0>(ele) == firstPoint; });
        auto&& finalPointData = std::find_if(intersections.begin(), intersections.end(), [finalPoint](const auto& ele) {return std::get<0>(ele) == finalPoint; });
        int oldHullfirstIdx = std::get<2>(*firstPointData);
        int oldHullLastIdx = oldHullsCircular[otherHullIdx].findIdx(std::get<2>(*finalPointData), 1);
        std::vector<QVector3D> newSegment;
        std::vector<int> newSourceIndices;

        if (oldHullfirstIdx > oldHullLastIdx)
            bool here = true;

        for (int idx = oldHullsCircular[otherHullIdx].findIdx(oldHullfirstIdx, -1); idx != oldHullsCircular[otherHullIdx].findIdx(oldHullLastIdx, 1); idx = oldHullsCircular[otherHullIdx].findIdx(idx, 1))
        {
            auto&& prevPoint = oldHullsCircular[otherHullIdx][oldHullsCircular[otherHullIdx].findIdx(idx, -1)];
            auto&& nextPoint = oldHullsCircular[otherHullIdx][oldHullsCircular[otherHullIdx].findIdx(idx, 1)];
            auto newPoint = generateNewHullPoint(prevPoint, oldHullsCircular[otherHullIdx][idx], nextPoint, fixedIncrement);
            newSegment.emplace_back(newPoint);
            newSourceIndices.emplace_back(idx);
        }

        NewSegment segmentData = {
            .from = std::get<0>(*firstPointData),
            .to = ihc.findIdx(std::get<0>(*finalPointData), 1),
            .newPoints = newSegment,
            .newSources = newSourceIndices
        };

        newSegments.emplace_back(segmentData);
    }

    #if DEBUG_IH_FIX
    for (auto&& segData : newSegments)
    {
        auto&& first = data->pts[segData.from];
        std::vector<QVector3D> points = { QVector3D(first.x(), 0.0f, first.z()) };

        for (auto&& point : segData.newPoints)
            points.emplace_back(QVector3D(point.x(), 0.0f, point.z()));

        auto&& last = data->pts[segData.to];
        points.emplace_back(QVector3D(last.x(), 0.0f, last.z()));

        spawn<DLineMarker>(points, QVector4D(0, 0, 0, 1), false, data->height + 40.0f);
    }
    #endif

    std::vector<QVector3D> newPts;
    std::vector<IHSrcInfo> newSources;
    std::vector<float> newIncrements;
    std::vector<std::pair<float, bool>> newModifications;

    auto&& hull = data->pts;
    auto&& sources = data->sources;
    auto&& increments = data->increments;
    auto&& modifications = data->modifiedBy;

    int startingIdx = 0;
    auto&& iter = std::find_if(newSegments.begin(), newSegments.end(), [](const auto& ele) {return ele.from > ele.to; });
    if (iter != newSegments.end())
        startingIdx = (*iter).to;

    for (int i = startingIdx; i < hull.size(); ++i)
    {
        auto&& it = std::find_if(newSegments.begin(), newSegments.end(), [i](const auto& ele) {return ele.from == i; });
        if (it != newSegments.end())
        {
            int segmentLength = ihc.distCW((*it).from, (*it).to) + 1;
            int newSegmentLength = (*it).newPoints.size();

            for (int segmentIdx = 0; segmentIdx < newSegmentLength; ++segmentIdx)
            {
                newPts.emplace_back((*it).newPoints[segmentIdx]);

                auto modifiedSource = sources[i];
                modifiedSource.idx = (*it).newSources[segmentIdx];
                newSources.emplace_back(modifiedSource);
                newIncrements.emplace_back(fixedIncrement);
                newModifications.emplace_back(modifications[i]); 
            }

            i += segmentLength;
        }
        else
        {
            newPts.emplace_back(hull[i]);
            newSources.emplace_back(sources[i]);
            newIncrements.emplace_back(increments[i]);
            newModifications.emplace_back(modifications[i]);
        }
    }

    hull = newPts;
    sources = newSources;
    increments = newIncrements;
    modifications = newModifications;
}

void ContourLines::removeIHs(std::vector<IHProtoData>* ihProtoData, const std::vector<int>& ids)
{
    for (auto id : ids)
    {
        if (auto&& ih = (*ihProtoData)[id].ptr; ih)
            despawnBatched(ih);

        for (int i = 0; i < (*ihProtoData)[id].sources.size(); ++i)
        {
            auto&& src = (*ihProtoData)[id].sources[i];
            src.ih->setDescendant(src.idx, IHSrcInfo());
        }

        (*ihProtoData)[id] = {};
    }

    *ihProtoData = removeAll(*ihProtoData, IHProtoData());
}

IHProtoData ContourLines::createMergedIH(const IHProtoData& ih1, const IHProtoData& ih2, const std::pair<int, int>& mergePts1, const std::pair<int, int>& mergePts2)
{
#if DEBUG_IH_MERGES
    QMap<QVector3D, int> mergeLevels;
#endif

    auto hull1 = asCircular(ih1.pts);
    auto hull2 = asCircular(ih2.pts);

    auto [h1_right, h1_left] = mergePts1;
    auto [h2_right, h2_left] = mergePts2;
    auto&& b1l = hull1[h1_left];
    auto&& b1r = hull1[h1_right];
    auto&& b2l = hull2[h2_left];
    auto&& b2r = hull2[h2_right];

    // Create a new hull node
    IHProtoData mergedData;
    std::ranges::set_union(ih1.parentIhs, ih2.parentIhs, std::inserter(mergedData.parentIhs, mergedData.parentIhs.begin()));

    mergedData.originIdx = std::min(ih1.originIdx, ih2.originIdx);
    auto idxToReplace = std::max(ih1.originIdx, ih2.originIdx);

    ihMerges[mergedData.originIdx].erase(idxToReplace);

    for(auto&& [id, merges] : ihMerges)
        if (merges.contains(idxToReplace))
        {
            if (!merges.contains(mergedData.originIdx))
                merges[mergedData.originIdx] = merges[idxToReplace];

            merges.erase(idxToReplace);
        }

#if DEBUG_IH_MERGES
    Generation::Data::get()->createMarker<DLineMarker>(b1l, b1r, QVector4D(0, 0, 1, 1));
    Generation::Data::get()->createMarker<DLineMarker>(b2l, b2r, QVector4D(0, 0, 1, 1));

    static std::array<QVector4D, 10> colors =
    {
        QVector4D(1,0,0,1),
        QVector4D(1,1,0,1),
        QVector4D(0.5,1,0,1),
        QVector4D(0,1,0,1),
        QVector4D(0,1,0.5,1),
        QVector4D(0,1,1,1),
        QVector4D(0,0.5,1,1),
        QVector4D(0,0,1,1),
        QVector4D(0,0,0.5,1),
        QVector4D(0,0,0,1),
    };
#endif
    float maxInc1 = 0.0f;
    float maxInc2 = 0.0f;

    std::unordered_map<int, QVector3D> newRidgelineSources;

    // ensure the loop starts if second = first+1
    bool began = false;
    int endIdx = hull1.findIdx(h1_right, 1);
    for (int i = h1_left; !began || i != endIdx; i = hull1.findIdx(i, 1))
    {
        began = true;
        mergedData.pts << hull1[i];
        mergedData.sources << ih1.sources[i];
        mergedData.increments << ih1.increments[i];
        mergedData.modifiedBy << ih1.modifiedBy[i];
        if (ih1.ridgelineSources.contains(i))
            newRidgelineSources.emplace(int(mergedData.pts.size()) - 1,ih1.ridgelineSources.at(i));

        maxInc1 = std::max(maxInc1, mergedData.increments.back());
    }

    // ensure the loop starts if second = first+1
    began = false;
    endIdx = hull2.findIdx(h2_right, 1);
    for (int i = h2_left; !began || i != endIdx; i = hull2.findIdx(i, 1))
    {
        began = true;
        mergedData.pts << hull2[i];
        mergedData.sources << ih2.sources[i];
        mergedData.increments << ih2.increments[i];
        mergedData.modifiedBy << ih2.modifiedBy[i];
        if (ih2.ridgelineSources.contains(i))
            newRidgelineSources.emplace(int(mergedData.pts.size()) - 1, ih2.ridgelineSources.at(i));

        maxInc2 = std::max(maxInc2, mergedData.increments.back());
    }

    mergedData.ridgelineSources = newRidgelineSources;

    // prevent big mismatches in increments causing rare bugs
    float maxInc = std::min(maxInc1, maxInc2);
    tbb::parallel_for(0, int(mergedData.increments.size()), [&](int i)
        {
            if (mergedData.increments[i] > maxInc)
                mergedData.increments[i] = maxInc;
        });

    // Merge IH Bounds
    mergedData.bounds = ih1.bounds;
    for (auto&& [boundId, data] : ih2.bounds)
    {
        // For duplicates, retrieve the more progressed state
        if (mergedData.bounds.contains(boundId))
            mergedData.bounds[boundId].second = std::max(mergedData.bounds[boundId].second, ih2.bounds.at(boundId).second);
    }

    mergedData.boundsReached = ih1.boundsReached && ih2.boundsReached;

    mergedData.ridgeIds = ih1.ridgeIds;
    mergedData.ridgeIds.insert(ih2.ridgeIds.begin(), ih2.ridgeIds.end());
    mergedData.lowestRidgeTier = std::max(ih1.lowestRidgeTier, ih2.lowestRidgeTier);
    mergedData.groupingFactor = std::min(ih1.groupingFactor, ih2.groupingFactor);

    if (ih1.tablelandType || ih2.tablelandType)
    {
        if (ih1.tablelandType && ih2.tablelandType)
        {
            if (int(*ih1.tablelandType) == int(*ih2.tablelandType))
            {
                mergedData.currentDropLvl = std::max(ih1.currentDropLvl, ih2.currentDropLvl);
                mergedData.desiredDropLvl = std::max(ih1.desiredDropLvl, ih2.desiredDropLvl);
                mergedData.tablelandType = ih1.tablelandType;
            }
            else
            {
                auto&& largerType = int(*ih1.tablelandType) > int(*ih2.tablelandType) ? ih1 : ih2;
                mergedData.currentDropLvl = largerType.currentDropLvl;
                mergedData.desiredDropLvl = largerType.desiredDropLvl;
                mergedData.tablelandType = largerType.tablelandType;
            }

            mergedData.tablelandType = ETableLand(std::max(int(*ih1.tablelandType), int(*ih2.tablelandType)));
        }
        else
        {
            mergedData.currentDropLvl = std::max(ih1.currentDropLvl, ih2.currentDropLvl);
            mergedData.desiredDropLvl = std::max(ih1.desiredDropLvl, ih2.desiredDropLvl);
            mergedData.tablelandType = std::max(ih1.tablelandType, ih2.tablelandType);
        }
    }

    mergedData.mergedDomains.insert(ih1.mergedDomains.begin(), ih1.mergedDomains.end());
    mergedData.mergedDomains.insert(ih2.mergedDomains.begin(), ih2.mergedDomains.end());
    mergedData.affectedBy.insert(ih1.affectedBy.begin(), ih1.affectedBy.end());
    mergedData.affectedBy.insert(ih2.affectedBy.begin(), ih2.affectedBy.end());

    // set merged data params to the higher IH and readjust the lower IHs 
    Landform::mergeIHParams(ih1, ih2, &mergedData);

#if DEBUG_IH_MERGES
    int nextMergeLevel = std::max(mergeLevels[ih2.pts[0]], mergeLevels[ih1.pts[0]]);
    mergeLevels[mergedData.pts[0]] = nextMergeLevel + 1;

    std::vector<QVector3D> elevatedPts;
    elevatedPts.reserve(mergedData.pts.size());
    for (auto&& p : mergedData.pts)
        elevatedPts << p + QVector3D(0, mergedData.height + 80 * (nextMergeLevel + 1), 0);

    if (nextMergeLevel < colors.size())
        Generation::Data::get()->createMarker<DLineMarker>(elevatedPts, colors[nextMergeLevel], true);
#endif

    Q_ASSERT(mergedData.pts.size() == mergedData.sources.size());
    Q_ASSERT(!mergedData.pts.empty());
    int previousSize = mergedData.pts.size();

    // Perform self-only fixup
    fixHull(&mergedData, {}, true);

#if DEBUG_IH_MERGES
    for (int i = 0; i < mergedData.pts.size(); ++i)
    {
        int j = i + 1 == mergedData.pts.size() ? 0 : i + 1;
        QVector3D first(mergedData.pts[i].x(), mergedData.height, mergedData.pts[i].z());
        QVector3D second(mergedData.pts[j].x(), mergedData.height, mergedData.pts[j].z());
        spawn<DLineMarker>(first, second, QVector4D(1,0,1,1), 0.0f, ELineDecorator::Arrow);
    }
#endif

    // Replace source hulls with the results.
#if DEBUG_IH_MERGES
    Generation::Data::get()->createMarker<DLineMarker>(ih1.pts[0], ih1.height, QVector4D{ 0,1,0,1 });
    Generation::Data::get()->createMarker<DLineMarker>(ih2.pts[0], ih2.height, QVector4D{ 0,1,0,1 });

#endif

    mergedData.computeMergingData();

    return mergedData;
}

float ContourLines::randomIncrement(std::vector<QVector3D>* newHull, const std::vector<QVector3D>* oldHull, const QSharedPointer<Isohypse>* sourceMarkers, const int i, const float baseIncrement, const float baseIncrementValue, const int levelIdx, int extremeDeviations)
{
    // Get source point increment
    const auto& sourcePoint = (*sourceMarkers)->getSourcePoint(i);
    const float sourcePointIncrement = distance((*oldHull)[i],sourcePoint);

    // Get previous non null point
    float previousPointIncrement = baseIncrement;
    int j = i;

    if (i > 0)
    {
        while (j > 0)
        {
            j--;

            if (!(*newHull)[j].isNull())
            {
                previousPointIncrement = distance((*newHull)[j],(*oldHull)[j]);
                break;
            }
        }
    }
    else
    {
        // For first point in newHull deduce previous point based on increment of oldHull last point
        j = int((*oldHull).size()) - 1;
        const auto& prevSourcePoint = (*sourceMarkers)->getSourcePoint(j);
        const float prevSourceIncrement = distance((*oldHull)[j],prevSourcePoint);
        const double prevSourceOffset = prevSourceIncrement / (baseIncrement - baseIncrementValue);
        previousPointIncrement = baseIncrement * prevSourceOffset;
    }

    // Source and previous point offsets from their base increments
    double sourceOffset = (sourcePointIncrement - baseIncrementValue) / ((baseIncrementValue * ((2 * (levelIdx - 1)) - 1)) - baseIncrementValue);
    double prevOffset = (previousPointIncrement - baseIncrementValue) / ((baseIncrementValue * (2 * levelIdx - 1)) - baseIncrementValue);
    sourceOffset = std::clamp(sourceOffset, 0.0, 1.0);
    prevOffset = std::clamp(prevOffset, 0.0, 1.0);

    double incOffset = (sourceOffset + prevOffset) / 2;

    const float segmentLenght = distance((*oldHull)[i],(*oldHull)[j]);

    // Amplify deviation if current extreme deviation count and segment length are under threshold, else amplify towards baseIncrement
    if ((incOffset < 0.45 || incOffset > 0.55) && extremeDeviations < 2 && segmentLenght < 5 * baseIncrement)
    {
        // Tendency amplifier
        const double sourceTendency = (sourceOffset - 0.5) * 2;
        const double prevTendency = (prevOffset - 0.5) * 2;
        incOffset = (prevOffset + prevTendency + sourceOffset + sourceTendency) / 2;
    }
    else
    {
        // Amplify towards baseIncrement (offset == 0.5)
        incOffset = (prevOffset + sourceOffset + 2) / 6;
    }

    // Final offset
    incOffset = std::clamp(incOffset, 0.0, 1.0);

    auto incrementGen = hybrid_int_distribution<int>(baseIncrementValue, (baseIncrementValue * (2 * levelIdx - 1)), 0.15, incOffset);
    const float randomIncrement = incrementGen(Generation::gRandomEngine);

    // Increment extreme deviations count, or decrement it if the offset returns to standard
    if (incOffset > 0.95 || incOffset < 0.05)
        extremeDeviations++;
    else if (incOffset >= 0.48 && incOffset <= 0.52 && extremeDeviations >= 1)
        extremeDeviations--;

    float nextIncrement = baseIncrement;

    // Amplify extreme high deviation for visibility purposes
    if (incOffset > 0.95)
        return nextIncrement = randomIncrement * 1.5;
    else
        return nextIncrement = randomIncrement;
}

void ContourLines::breakStraightLines(std::vector<QVector3D>* newHull, const std::vector<QVector3D>* oldHull, float baseIncrement, const QQuaternion* rotateLeft90)
{
    auto oldIhc = asCircular(*oldHull);
    auto newIhc = asCircular(*newHull);

    tbb::parallel_for(tbb::blocked_range<int>(0, newHull->size()),
        [&](tbb::blocked_range<int>& r)
        {
            for (int i = r.begin(); i != r.end(); ++i)
            {
                int prev = (i == 0) ? int(newHull->size()) - 1 : i - 1;
                if ((*newHull)[i].isNull() || (*newHull)[prev].isNull())
                    continue;

                // Find line range.
                int j = i;
                int p1 = j;
                int p2 = j;
                bool fullLoop = false;

                while (true)
                {
                    // Crossing last-first: End after this iteration.
                    if (++j == (*newHull).size())
                    {
                        j = 0;
                        fullLoop = true;
                    }

                    if ((*newHull)[j].isNull())
                        continue;

                    p2 = j;
                    break;
                }

                auto distanceToCheck = distance((*newHull)[p1],(*newHull)[p2]);

                // If line is too long, break it and add new points along its normal
                if (distanceToCheck > 3 * baseIncrement)
                {
                    const QVector3D segment = ((*newHull)[p1] - (*newHull)[p2]).normalized();

                    // Look for points between p1 and p2
                    int j = newIhc.findIdx(p1, 1);
                    for (int p3 = j; p3 != p2; p3++)
                    {
                        if (p3 >= (*newHull).size())
                        {
                            p3 = 0;

                            if (p3 == p2)
                                break;
                        }

                        // get an increment out of source points
                        const float inc = ((distance((*newHull)[p1],(*oldHull)[p1]) + distance((*newHull)[p2],(*oldHull)[p2])) / 2);

                        const QVector3D pV = ((*newHull)[p1] - ((*oldHull)[p3] + (*rotateLeft90).rotatedVector(segment) * inc)).normalized();
                        const QVector3D nV = ((*newHull)[p2] - ((*oldHull)[p3] + (*rotateLeft90).rotatedVector(segment) * inc)).normalized();
                        const float anglePV = angle360(pV, nV);
                        const float angleNV = angle360(nV, pV);

                        // Prevents new segments from having sharp angles
                        if (fCmp(anglePV, 270.0f) == std::strong_ordering::greater && fCmp(angleNV, 90.0f) == std::strong_ordering::greater)
                            (*newHull)[p3] = (*oldHull)[p3] + ((*rotateLeft90).rotatedVector(segment) * inc);
                    }
                }
            }
        });
}

bool ContourLines::fixHull(IHProtoData* data, const std::vector<std::vector<QVector3D>>& oldHulls, bool omitThicken)
{
    auto&& pts = data->pts;
    auto&& sources = data->sources;
    auto&& increments = data->increments;

    if (pts.size() < 3)
        return false;

    OmniProfile("Fix New Hull");

    findAndDeleteLoopsOfHull(data);

    if (!oldHulls.empty())
    {
        OmniProfile("Checks against old hull");
        modifyPointsAccordingToOldHulls(data, oldHulls);
    }

    if (!omitThicken)
        thickenHull(data);

    findAndDeleteDuplicatePoints(data);
    findAndDeleteLoopsOfHull(data);

    return true;
}

std::vector<std::pair<std::pair<int, int>, ContourLines::IsohypseMergeInfo>> ContourLines::detectClosestIsohypseColissions(const std::vector<IHProtoData>& ihProtoData)
{
    auto qtree = computeProtoDataQtree(ihProtoData);

    OmniProfile("Raw detection");

    std::vector<std::pair<std::pair<int, int>, IsohypseMergeInfo>> closestMerges;
    std::mutex outputGuard;

    tbb::parallel_for(0, int(ihProtoData.size()), [&](int hullIdx)
        {
            // Perform a wide search from ih center to radius + range
            auto&& hull = ihProtoData[hullIdx];
            float range = 2 * hull.mergeDistanceMult * (baseIncrement + hull.maxIncrement);
            auto nodesOut = qtree->find_all_nearest(hull.center.x, hull.center.z, hull.radius + range);
            std::unordered_map<int /*other hull idx*/, std::tuple<std::pair<int, int> /*myBound*/, std::pair<int, int> /*theirBound*/, float /*distance*/>> distanceToOthers;

            for (auto* nodeOut : nodesOut)
                if (auto&& [otherHullIdx, otherSegmentsBeginIdx, otherSegmentsEndIdx] = nodeOut->data; otherHullIdx != hullIdx)
                {
                    // Found other ih segment in range
                    // Perform a reverse search with only range
                    auto&& otherHull = ihProtoData[otherHullIdx];
                    auto nodesIn = qtree->find_all_nearest(nodeOut->x, nodeOut->y, range);

                    for (auto&& nodeIn : nodesIn)
                        if (auto&& [myHullIdx, mySegmentsBeginIdx, mySegmentsEndIdx] = nodeIn->data; myHullIdx == hullIdx)
                        {
                            // Relaxed distance criteria met!

                            // Precise distance criteria
                            float pointRange = hull.mergeDistanceMult * (baseIncrement + std::max(hull.increments[mySegmentsBeginIdx], hull.increments[mySegmentsEndIdx]))
                                + otherHull.mergeDistanceMult * (baseIncrement + std::max(otherHull.increments[otherSegmentsBeginIdx], otherHull.increments[otherSegmentsEndIdx]));

                            float dist = distance(GVector2D(nodeOut->x, nodeOut->y), GVector2D(nodeIn->x, nodeIn->y));
                            // Find closest viable bounds
                            if (dist < pointRange)
                            {
                                std::pair<int, int> myBounds, theirBounds;
                                myBounds = { mySegmentsBeginIdx, mySegmentsEndIdx };
                                theirBounds = { otherSegmentsBeginIdx, otherSegmentsEndIdx };

                                auto&& it = distanceToOthers.find(otherHullIdx);
                                if(it != distanceToOthers.end())
                                {
                                    if (std::get<2>(it->second) < std::get<2>(distanceToOthers[otherHullIdx]))
                                        distanceToOthers[otherHullIdx] = std::make_tuple(myBounds, theirBounds, dist);
                                }
                                else
                                    distanceToOthers[otherHullIdx] = std::make_tuple(myBounds, theirBounds, dist);
                            }
                        }
                }

            for(auto&& [otherIdx, data] : distanceToOthers)
            {
                // Create merge info
                IsohypseMergeInfo mergeInfo{ .distance = std::get<2>(data) };

                // Sorted hull indices are keys for reverse matching
                std::pair<int, int> hullPair;

                if (hullIdx < otherIdx)
                {
                    hullPair = { hullIdx, otherIdx };
                    mergeInfo.mergeBounds1 = std::get<0>(data);
                    mergeInfo.mergeBounds2 = std::get<1>(data);
                }
                else
                {
                    hullPair = { otherIdx, hullIdx };
                    mergeInfo.mergeBounds2 = std::get<0>(data);
                    mergeInfo.mergeBounds1 = std::get<1>(data);
                }

                // Critical section
                std::scoped_lock lock(outputGuard);

                for (auto&& [hullIndices, mergeInfo] : closestMerges)
                    if (hullIndices == hullPair)
                        continue;

                closestMerges <<= std::pair{ hullPair, mergeInfo };
            }
        });

    std::sort(closestMerges.begin(), closestMerges.end(), [](auto& r1, auto& r2) { return r1.second.distance < r2.second.distance; });

    return closestMerges;
}

std::optional<ContourLines::IsohypseMergeInfo> ContourLines::detectClosestIsohypseInfo(const IHProtoData& ih1, const IHProtoData& ih2)
{
    OmniProfile("Detect Closest Isohypse Info");

    auto&& h1 = asCircular(ih1.pts);
    auto&& h2 = asCircular(ih2.pts);
    auto&& inc1 = ih1.increments;
    auto&& inc2 = ih2.increments;
    float mult1 = ih1.mergeDistanceMult;
    float mult2 = ih2.mergeDistanceMult;

    std::unordered_set<std::pair<int, int>> bounds1;
    std::unordered_set<std::pair<int, int>> bounds2;

    std::array<std::array<int, 2>, 2> closestSegments;
    float minD = std::numeric_limits<float>::max();

    std::mutex distGuard;
    tbb::parallel_for(tbb::blocked_range2d<int, int>(0, h1.getSize(), 0, h2.getSize()), [&](tbb::blocked_range2d<int>& r)
        {
            for (int p1 = r.rows().begin(); p1 < r.rows().end(); ++p1)
            {
                int q1 = h1.findIdx(p1, 1);

                for (int p2 = r.cols().begin(); p2 < r.cols().end(); ++p2)
                {
                    int q2 = h2.findIdx(p2, 1);
                    float scanIncrement = mult1 * mult2 * 2 * baseIncrement + (std::max(inc1[p1], inc1[q1]) + std::max(inc2[p2], inc2[q2]));

                    auto&& [v1, v2, d] = distance(std::array<GVector2D, 2>{ h1[p1], h1[q1] }, { h2[p2], h2[q2] }, false);
                    if (d >= scanIncrement)
                        continue;

                    // critical section
                    {
                        std::scoped_lock<std::mutex> dlock(distGuard);
                        bounds1.emplace(p1, q1);
                        bounds2.emplace(p2, q2);

                        if (d < minD)
                        {
                            minD = d;
                            closestSegments = { std::array{ p1, q1 }, std::array{ p2, q2 } };
                        }
                        else if ((d == minD))
                        {
                            if (closestSegments[0][0] == q1)
                                closestSegments[0][0] = p1;
                            else if (closestSegments[0][1] == p1)
                                closestSegments[0][1] = q1;

                            if (closestSegments[1][0] == q2)
                                closestSegments[1][0] = p2;
                            else if (closestSegments[1][1] == p2)
                                closestSegments[1][1] = q2;
                        }
                    }
                }
            }
        });

    if (minD == std::numeric_limits<float>::max())
        return {};

    std::pair<int, int> furthestPair1({ bounds1.begin()->first, bounds1.begin()->second });
    std::pair<int, int> furthestPair2({ bounds2.begin()->first, bounds2.begin()->second });;

    // Find the widest valid segment
    auto findWidestPair = [](const std::unordered_set<std::pair<int, int>>& segmentSet, const CircularVectorView<std::vector, QVector3D>& ihc) 
    {
        std::pair<int, int> shortestValidPair;
        std::unordered_set<int> allIndices;
        std::unordered_set<int> allFirstIndices;
        std::unordered_set<int> allSecondIndices;
        std::vector<std::pair<int, int>> pairs;

        for (auto&& segment : segmentSet)
        {
            allIndices.emplace(segment.first);
            allIndices.emplace(segment.second);
            allFirstIndices.emplace(segment.first);
            allSecondIndices.emplace(segment.second);
        }

        for (auto&& idx1 : allFirstIndices)
            for (auto&& idx2 : allSecondIndices)
                pairs.emplace_back(idx1, idx2);

        std::sort(pairs.begin(), pairs.end(), [&ihc](auto const& firstElement, auto const& secondElement) 
            {return ihc.distCW(firstElement.first, firstElement.second) < ihc.distCW(secondElement.first, secondElement.second); });

        for (auto&& [idx1, idx2] : pairs)
        {
            int dist = ihc.distCW(idx1, idx2);
            bool valid = true;
            for (auto&& otherIdx : allIndices)
            {
                if (otherIdx == idx1 || otherIdx == idx2)
                    continue;

                if (ihc.distCW(idx1, otherIdx) > dist)
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
            {
                shortestValidPair = { idx1, idx2 };
                break;
            }
        }

        return shortestValidPair;
    };

    furthestPair1 = findWidestPair(bounds1, h1);
    furthestPair2 = findWidestPair(bounds2, h2);

    std::unordered_set<std::pair<int, int>> allIndicesOfFirst;

    // Check for intersections
    std::vector<std::array<std::array<int, 2>, 2>> intersections;
    for (int i = furthestPair1.first; i != furthestPair1.second; i = h1.findIdx(i, 1))
    {
        int myNext = h1.findIdx(i, 1);
        Segment2D mySegment(h1[i], h1[myNext]);
        for (int j = furthestPair2.first; j != furthestPair2.second; j = h2.findIdx(j, 1))
        {
            int otherNext = h2.findIdx(j, 1);
            Segment2D otherSegment(h2[j], h2[otherNext]);
            if (mySegment.intersects(otherSegment, true))
            {
                allIndicesOfFirst.emplace(std::pair<int, int>({i, myNext }));
                intersections.emplace_back(std::array<std::array<int, 2>, 2>({ std::array<int, 2>({i, myNext}), std::array<int, 2>({j, otherNext}) }));
            }
        }
    }

    IsohypseMergeInfo mergeInfo;
    mergeInfo.shrinkBounds1 = furthestPair1;
    mergeInfo.shrinkBounds2 = furthestPair2;
    mergeInfo.distance = minD;

    if (intersections.size() > 1)
    {
        // Find the furthest intersection pair of the first hull
        auto widestFirstPair = findWidestPair(allIndicesOfFirst, h1);
        auto firstIntersectionPair = std::find_if(intersections.begin(), intersections.end(), [widestFirstPair](const auto& ele) { return ele[0][0] == widestFirstPair.first || ele[0][1] == widestFirstPair.first; });
        auto secondIntersectionPair = std::find_if(intersections.begin(), intersections.end(), [widestFirstPair](const auto& ele) { return ele[0][0] == widestFirstPair.second || ele[0][1] == widestFirstPair.second; });

        mergeInfo.mergeBounds1 = { widestFirstPair.first, widestFirstPair.second };
        mergeInfo.mergeBounds2 = { (*secondIntersectionPair)[1][0], (*firstIntersectionPair)[1][1] };

        #if DEBUG_IH_MERGES
        for (int i = mergeInfo.mergeBounds1.first; i != mergeInfo.mergeBounds1.second; i = h1.findIdx(i, 1))
        {
            std::vector<QVector3D> vec({ QVector3D(h1[i].x(), DEBUG_2D_VIEW ? 150.0f : ih1.height + 90.0f, h1[i].z()) , QVector3D(h1[h1.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 150.0f : ih1.height + 90.0f, h1[h1.findIdx(i, 1)].z()) });
            spawn<DLineMarker>(vec, QVector4D(1, 0, 1, 1));
        }

        for (int i = mergeInfo.mergeBounds2.first; i != mergeInfo.mergeBounds2.second; i = h2.findIdx(i, 1))
        {
            std::vector<QVector3D> vec({ QVector3D(h2[i].x(), DEBUG_2D_VIEW ? 120.0f : ih2.height + 60.0f, h2[i].z()) , QVector3D(h2[h2.findIdx(i, 1)].x(), DEBUG_2D_VIEW ? 120.0f : ih2.height + 60.0f, h2[h2.findIdx(i, 1)].z()) });
            spawn<DLineMarker>(vec, QVector4D(0, 1, 0, 1));
        }
        #endif
    }
    else
    {
        mergeInfo.mergeBounds1 = { closestSegments[0][0], closestSegments[0][1] };
        mergeInfo.mergeBounds2 = { closestSegments[1][0], closestSegments[1][1] };
    }

    return mergeInfo;
}

QVector3D ContourLines::generateNewHullPoint(const GVector2D& prev, const GVector2D& src, const GVector2D& next, float increment)
{
    QVector3D pV = (prev - src).normalized();
    QVector3D nV = (next - src).normalized();
    float angle = angle360(pV, nV);
    // Case where the source points are a straight line
    if (angle <= 0.1f || angle >= 359.9f)
        return QVector3D(QVector3D(src.x, 0.0f, src.z) - pV * increment);

    QVector3D nextToPrev = (prev - next).normalized();
    return QVector3D(src) + rotateLeft90.rotatedVector(nextToPrev) * increment;
}

QVector4D ContourLines::getColorByLevelAndHeight(int level, float height)
{
    static QVector3D baseColor = { 0.9, 0.45, 0 };
    static QVector3D baseUnderwaterColor = { 0.2, 0.2, 0.5 };

    if (fCmp(height, 0.0f) == std::strong_ordering::greater)
        return QVector4D(baseColor, 1);
    else if (fCmp(height, 0.0f) == 0)
        return QVector4D(0,1,1,1);
    else
        return QVector4D(baseUnderwaterColor, 1);
}

void ContourLines::computePreflow()
{
    OmniProfile("Preflow");
    QMap<IHSrcInfo, std::vector<IHSrcInfo>> descendants2sources;

    auto ihsByLevel = Generation::Data::get()->getIsohypseMarkersByLevel();
    for (auto&& ihLevel : ihsByLevel)
    {
        for (auto&& ih : ihLevel)
        {
            auto&& cPts = ih->getCircularPoints();
            for (int i = 0; i < cPts.getSize(); ++i)
                if (auto desc = ih->getNearestDescendant(i); desc)
                    descendants2sources[desc] << IHSrcInfo{ih.get(), i};
        }
    }

    for (auto it = descendants2sources.keyValueBegin(); it != descendants2sources.keyValueEnd(); ++it)
    {
        auto&& [descendant, sources] = *it;
        for (auto&& src : sources)
            descendant.ih->addPreflow(descendant.idx, src);

        // Arrange preflow in a traversible line
        for (auto&& preflow : descendant.ih->preflow[descendant.idx])
        {
            if (preflow.indices.size() <= 1)
                continue;

            std::sort(preflow.indices.begin(), preflow.indices.end());

            // Find the correct rotation.
            auto circularIH = preflow.ih->getCircularPoints();
            int dCW = circularIH.distCW(preflow.indices.front(), preflow.indices.back());
            int bestRotation = 0;

            for (int i = 1; i < preflow.indices.size(); ++i)
            {
                std::vector<int> aux(preflow.indices.size());
                std::rotate_copy(preflow.indices.begin(), preflow.indices.begin() + i, preflow.indices.end(), aux.begin());
                if (int d = circularIH.distCW(aux.front(), aux.back()); d < dCW)
                {
                    bestRotation = i;
                    dCW = d;
                }
            }

            // Finalize rotation
            std::rotate(preflow.indices.begin(), preflow.indices.begin() + bestRotation, preflow.indices.end());
        }

#if DEBUG_PREFLOW
        for (auto preflow : descendant.ih->getPreflow()[descendant.idx])
        {
            if (preflow.indices.size() <= 1)
                continue;

            std::vector<QVector3D> pts;
            for (int i : preflow.indices)
                pts << preflow[i].getPoint();

            // Find point nearest to the dest.
            float dMin = std::numeric_limits<float>::max();
            const QVector3D* nearest;
            for (auto&& pt : pts)
                if (float d = distanceSquared(pt, descendant.getPoint()); d < dMin)
                {
                    dMin = d;
                    nearest = &pt;
                }

            static auto h = QVector3D(0, 140, 0);
            for (auto&& p : pts)
                p += h;

            Generation::Data::get()->createMarker<DLineMarker>(pts, QVector4D(1, 1, 0, 1));
            Generation::Data::get()->createMarker<DLineMarker>(std::vector{ *nearest + h * 10, descendant.getPoint() + h }, QVector4D(0, 1, 0, 1));
        }
#endif
    }
}


bool ContourLines::isRidgeInsideBound(const std::vector<QVector3D>& ridgePoints, const std::vector<QVector3D>& boundPoints)
{
    int intersectCount = 0;
    QVector3D pointToCheck = ridgePoints[0];
    auto bpc = asCircular(boundPoints);

    for (int i = 0; i < ridgePoints.size(); ++i)
    {
        float xRemainder = fmod(ridgePoints[i].x(), GRID_SEGMENT_WIDTH);
        float zRemainder = fmod(ridgePoints[i].x(), GRID_SEGMENT_WIDTH);

        // Get the first ridge point not on grid lines
        if ((xRemainder > 100) && (zRemainder > 100))
        {
            pointToCheck = ridgePoints[i];
            break;
        }
    }

    // Make an "infinite" line
    GVector2D ridgePointX({ pointToCheck.x(), pointToCheck.z() });
    GVector2D ridgePointZ({ pointToCheck.x(), pointToCheck.z() + (GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH) });
    Segment2D ridgeSegment({ ridgePointX, ridgePointZ });

    for (int i = 0; i < boundPoints.size(); ++i)
    {
        int j = bpc.findIdx(i, 1);

        GVector2D controlPointX({ boundPoints[i].x(), boundPoints[i].z() });
        GVector2D controlPointZ({ boundPoints[j].x(), boundPoints[j].z() });
        Segment2D controlSegment({ controlPointX, controlPointZ });

        if (ridgeSegment.intersects(controlSegment, true))
            intersectCount++;
    }

    // A point is inside a polygon if count of intersections is odd
    if (intersectCount % 2 == 1)
        return true;
    else
        return false;
}

float ContourLines::computeIncrement(const std::vector<QVector3D>& oldHull, const std::vector<QVector3D>& projectedHull, const std::vector<float>& projectedIncrements, int i, IHProtoData* currentData, const std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>& heightBounds, float slopeAngle)
{
    OmniProfile("Compute Increments");

    auto point = GVector2D(oldHull[i]);
    auto oihc = asCircular(oldHull);

    // Narrow down the lookup to domain's bounds
    GPoint sq = point.toGPoint();
    auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
    if (!domain)
        return 5.0f;

    quint64 domainGuid = domain->getGuid();

    float result = projectedIncrements[i];
    if (!heightBounds.contains(domainGuid) || heightBounds.at(domainGuid).empty())
        return result;

    auto&& prev = oihc[oihc.findIdx(i, -1)];
    auto&& next = oihc[oihc.findIdx(i, 1)];

    // Find closest bound ahead
    std::array<GVector2D, 2> s1 = { prev, point };
    std::array<GVector2D, 2> s2 = { point, next };
    float minD = std::numeric_limits<float>::max();
    float targetH = -1.0f;
    auto growthDir = (projectedHull[i] - oldHull[i]).normalized();
    float shortenedIncrement = projectedIncrements[i];

    // Search in a radius so that a smooth descent is still viable, but no less than half of a grid mesh eye
    auto&& nearestNodes = heightBoundsqtree->find_all_nearest(point.x, point.z, std::max(projectedIncrements[i] * 5, GRID_SEGMENT_WIDTH / 2));

    std::unordered_set<int> checkedBounds;
    for (auto&& node : nearestNodes)
    {
        for (auto&& boundData : node->data.bounds)
        {
            if (!boundData.domains.contains(domainGuid))
                continue;

            if (checkedBounds.contains(boundData.id))
                continue;

            if (boundData.height >= currentData->height)
                continue;

            checkedBounds.emplace(boundData.id);

            for(auto&& s : boundData.segments)
            {
                auto [unused1, v1, d1] = distance(s1, { s.first, s.second });
                auto [unused2, v2, d2] = distance(s2, { s.first, s.second });

                if (angle180(growthDir, (v1 - point).normalized()) < 90.0f)
                {
                    float heightDelta = currentData->height - boundData.height;
                    float distanceToAchieve = std::tanf(qDegreesToRadians(90.0f - slopeAngle)) * heightDelta;

                    if (distanceToAchieve >= (d1 * 0.8))
                    {
                        int steps = std::ceil(heightDelta / currentData->heightDeltas.back());
                        float sInc = (d1 * 0.8) / steps;
                        if (sInc < shortenedIncrement)
                            shortenedIncrement = sInc;
                    }

                    if (minD > d1)
                        minD = d1;

                    if (targetH > boundData.height)
                        targetH = boundData.height;

                    if (boundData.originType == Generation::HeightBoundOrigin::Domain)
                        currentData->affectedBy[EIHAffectType::Domain] += boundData.originObject;
                    else if (boundData.originType == Generation::HeightBoundOrigin::Shoreline)
                        currentData->affectedBy[EIHAffectType::Shoreline] += boundData.originObject;
                }

                if (angle180(growthDir, (v2 - point).normalized()) < 90.0f)
                {
                    float heightDelta = currentData->height - boundData.height;
                    float distanceToAchieve = std::tanf(qDegreesToRadians(90.0f - slopeAngle)) * heightDelta;

                    if (distanceToAchieve >= (d2 * 0.8))
                    {
                        int steps = std::ceil(heightDelta / currentData->heightDeltas.back());
                        float sInc = (d2 * 0.8) / steps;
                        if (sInc < shortenedIncrement)
                            shortenedIncrement = sInc;
                    }

                    if (minD > d2)
                        minD = d2;

                    if (targetH > boundData.height)
                        targetH = boundData.height;

                    if (boundData.originType == Generation::HeightBoundOrigin::Domain)
                        currentData->affectedBy[EIHAffectType::Domain] += boundData.originObject;
                    else if (boundData.originType == Generation::HeightBoundOrigin::Shoreline)
                        currentData->affectedBy[EIHAffectType::Shoreline] += boundData.originObject;
                }

            }
        }
    }

    // No bounds
    if (minD == std::numeric_limits<float>::max())
        return result;

    // Compute scaling
    float heightDiff = currentData->height - targetH;
    if (heightDiff <= 0.0f)
        return result;

    Q_ASSERT(shortenedIncrement - 1.0f <= projectedIncrements[i]);

    return std::max(shortenedIncrement, 25.0f);
}

QSharedPointer<tml::qtree<float, ContourLines::IHHeightBoundNode>> ContourLines::computeHeightBoundsQtree(const std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>& heightBounds)
{
    OmniProfile("Height Bounds QTree computations");

    // 3 squares lookup margin
    constexpr float minCoord = -3 * GRID_SEGMENT_WIDTH;
    constexpr float maxCoord = (GRID_SEGMENT_COUNT + 3) * GRID_SEGMENT_WIDTH;
    auto qtree = QSharedPointer<tml::qtree<float, IHHeightBoundNode>>::create(minCoord, maxCoord, maxCoord, minCoord);
    static int id = 0;

    for (auto&& [firstDomain, data] : heightBounds)
    {
        for (auto&& [type, boundsPerType] : data)
        {
            IHHeightBoundNodeData dummyNodeData;
            dummyNodeData.domains.emplace(firstDomain);

            for (auto&& [originObjectGuid, boundData] : boundsPerType)
            {
                if (type == Generation::HeightBoundOrigin::Domain)
                    dummyNodeData.domains.emplace(originObjectGuid);

                for (auto&& [height, segments] : boundData)
                {
                    for (auto&& [firstPoint, secondPoint] : segments)
                    {
                        IHHeightBoundNodeData finalData = dummyNodeData;
                        finalData.id = id++;
                        finalData.height = height;
                        finalData.originObject = originObjectGuid;
                        finalData.originType = type;
                        finalData.segments = segments;

                        auto addBoundNode = [&](const GVector2D& point)
                        {
                            if (auto* existingNode = qtree->find_nearest(point.x, point.z, 1.0f); existingNode)
                                const_cast<IHHeightBoundNode&>(existingNode->data).bounds.emplace_back(finalData);
                            else
                                qtree->add_node(point.x, point.z, { std::vector<IHHeightBoundNodeData>({finalData}) });
                        };

                        addBoundNode(firstPoint);
                        auto lastPoint = firstPoint;
                        // Add additional nodes if the distance between points is too great (this is predicted only for domain bounds)
                        while (true)
                        {
                            if (auto dist = lastPoint.dist(secondPoint); dist > 1500.0f)
                            {
                                auto dir = (secondPoint - lastPoint).normalized();
                                lastPoint += (dir * 1000.0f);
                                addBoundNode(lastPoint);
                            }
                            else
                                break;
                        }
                        addBoundNode(lastPoint);
                    }
                }
            }
        }
    }

    return qtree;
}

float ContourLines::distanceToHeightStepsScalling(const IHProtoData& data, float increment, float targetDistance, float targetHeight)
{
    float projectedHeightSteps = Landform::computeHeightStepsProjection(data, targetHeight);
    auto projectedIHSteps = computeDistanceStepsProjection(increment, targetDistance);

    return projectedIHSteps / projectedHeightSteps;
}

float ContourLines::computeDistanceStepsProjection(float increment, float targetDistance)
{
    if (increment < targetDistance)
    {
        float D = std::pow(baseIncrement, 2) - 8.0f * baseIncrement * (increment - 2.0f * targetDistance);
        if (D < 0)
            return increment;

        return (baseIncrement + sqrt(D)) / (2.0f * baseIncrement);
    }
    else
        return targetDistance / increment;
}

void ContourLines::correctSwallowedIHHeights(const std::set<IHProtoData*>& ihDataToCorrect, const std::set<Isohypse*>& ihsToCorrect, float currentH, std::vector<IHProtoData>* ihProtoData)
{
    std::set<Isohypse*> furtherIhsToCorrect;

    for (auto* ihData : ihDataToCorrect)
        if (currentH > ihData->height)
        {
            OmniLog() <<= std::format("Corrected IH {} to {}", ihData->height, currentH);
            ihData->height = currentH;

            for (auto&& src : ihData->sources)
                if (src)
                    furtherIhsToCorrect.insert(src.ih);
        }

    for (auto* ih : ihsToCorrect)
        if (currentH > ih->data.height)
        {
            OmniLog() <<= std::format("Corrected IH {} to {}", ih->data.height, currentH);
            ih->data.height = currentH;
            //ih->setLineColor(getColorByLevelAndHeight(ih->level, currentH));

            for (auto&& src : ih->data.sources)
                if (src)
                    furtherIhsToCorrect.insert(src.ih);
        }

    if (furtherIhsToCorrect.empty())
        return;

    correctSwallowedIHHeights({}, furtherIhsToCorrect, currentH + 1.0f, ihProtoData);
}

void ContourLines::swallowAll(std::vector<IHProtoData>* ihProtoData)
{
    OmniProfile("Swallowing");

    for (int swallowerIdx = 0; swallowerIdx < ihProtoData->size(); ++swallowerIdx)
    {
        auto&& swallower = ihProtoData->at(swallowerIdx);
        Polygon2D poly = swallower.pts;

        tbb::parallel_for(0, int(ihProtoData->size()), [&](int idx)
            {
                if (idx == swallowerIdx)
                    return;

                auto&& data = ihProtoData->at(idx);
                for (auto&& p : data.pts)
                    if (!poly.contains(p))
                        return;

                (*ihProtoData)[idx].swallowedBy = &swallower;

#if DEBUG_IH_FIX
                // Coords formated to work with online point plotters
                QString debugCoords;
                for (int i = 0; i < swallower.pts.size(); ++i)
                {
                    debugCoords.append("(" + toQString(swallower.pts[i].x()) + " , " + toQString(swallower.pts[i].z()) + ")" + ",");

                    int j = i + 1 == swallower.pts.size() ? 0 : i + 1;
                    QVector3D firstPoint(swallower.pts[i].x(), DEBUG_2D_VIEW ? 60.0f : swallower.height, swallower.pts[i].z());
                    QVector3D secondPoint(swallower.pts[j].x(), DEBUG_2D_VIEW ? 60.0f : swallower.height, swallower.pts[j].z());
                    spawn<DLineMarker>(firstPoint, secondPoint, QVector4D(1,0,0,1), 0.0f, ELineDecorator::Arrow);
                }
                debugCoords.append("(" + toQString(swallower.pts[0].x()) + " , " + toQString(swallower.pts[0].z()) + ")" + '\n');
                OmniLog(ELoggingLevel::Warn) <<= debugCoords;
#endif

                // Find height baseline
                float hBase = 0;
                for (auto&& src : swallower.sources)
                    if (src.ih->data.height > hBase)
                        hBase = src.ih->data.height;
                    
                // Correct heights
                correctSwallowedIHHeights({ &(*ihProtoData)[idx] }, {}, hBase, ihProtoData);
            });
    }
}

std::array<std::vector<IHProtoData>, 2> ContourLines::chooseIhsToGrow()
{
    std::array<std::vector<IHProtoData>, 2> results;
    auto&& growing = results[0];
    auto&& merging = results[1];

    growing = { ihStack.pop() };
    float topHeight = growing[0].height;
    float botHeight = topHeight - growing[0].heightDeltas.back();

    while (!ihStack.isEmpty() && ihStack.top().height > botHeight && ihStack.top().height > ihStack.top().mergeThreshold)
        growing << ihStack.pop();

    while (!ihStack.isEmpty())
        merging << ihStack.pop();

    return results;
}
