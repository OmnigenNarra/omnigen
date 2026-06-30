#include "stdafx.h"
#include "RiverMarker.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Omnigen.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Utils/CircularVectorView.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "../Lake/TerrainModLake.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"

#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <execution>

#include "Mathematics/BezierCurve.h"

#include "Utils/Triangulation/Earcut.hpp"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"

#define MIN_RIVER_WIDTH 500
#define MAX_RIVER_WIDTH 5000
#define MAX_RIVER_SOURCES_PER_GSW 1

#define DEBUG_MOUTH_SCAN 0
#define DEBUG_INFLUENTS 0

// Used in GuideRiverThroughValley as mutable
bool rightTurn;

DRiverMarker::DRiverMarker(const RiverData& riverData, QWeakPointer<DRiverMarker> inParent)
    : DLineMarker(riverData.axis, QVector4D(0,0,1,1))
    , riverBounds({ riverData.leftFloodBound, riverData.rightFloodBound })
    , boundPolygon(createBoundPolygon())
    , bFallsIntoSea(riverData.fallsIntoSea)
    , area(riverData.affectedClustersKeyCells)
{
    parent = inParent;
    //height = 100;

    //for (int cell : area)
    //    spawn<DLineMarker>(Generation::Data::get()->getTerrainCells()->getCellAt(cell)->getCenter(), 20000, Colors::red);
}

void DRiverMarker::addInfluent(const InfluentInfo& info)
{
    influents.insert(std::lower_bound(influents.begin(), influents.end(), info), info);
    children << info.river.lock();
}

QSharedPointer<DRiverMarker> DRiverMarker::generateOne(const IHSrcInfo& source, const std::vector<QVector3D>& riverPoints)
{
    RiverData river;
    if (riverPoints.empty())
    {
        auto shores = Generation::Data::get()->getMarkers<DShorelineMarker>();

        // New rivers
        std::tie(river.axis, river.fallsIntoSea) = generateTerrainCompatibleRiver(source, shores);
        if (river.axis.size() < 2)
            return nullptr;
    }
    else
        river.axis = riverPoints;

    auto existingRivers = Generation::Data::get()->getMarkers<DRiverMarker>();

    InfluentInfo infInfo = tryMergeIntoOtherRiver(existingRivers, &river);

    addFloodBounds(&river);
    if (infInfo)
        mergeInfluentBounds(&river, infInfo);

    auto newRiver = Generation::Data::get()->createMarker<DRiverMarker, true>(river, infInfo.river);

    // infInfo here contains parent river instead of the influent
    if (infInfo)
        infInfo.river.lock()->addInfluent({ newRiver, infInfo.angle, infInfo.parentSegmentIdx, infInfo.parentSegmentPart });

    return newRiver;
}

bool DRiverMarker::generateAll()
{
    OmniProfile("Rivers");

    auto isohypsesByLevel = Generation::Data::get()->getIsohypseMarkersByLevel();
    if (isohypsesByLevel.empty())
        return true;

    auto shores = Generation::Data::get()->getMarkers<DShorelineMarker>();

    std::vector<IHSrcInfo> validSources;

    for (auto&& ih0 : isohypsesByLevel[0])
    {
        float ih0length = ih0->getLength();
        int potentialSources = (ih0length / GRID_SEGMENT_WIDTH) * MAX_RIVER_SOURCES_PER_GSW;
        if (potentialSources == 0)
            potentialSources = 1;

        int size0 = int(ih0->getCircularPoints().getSize()) - 1;
        int sourceOffset = size0 / potentialSources;
        int totalOffset = std::uniform_int_distribution(0, sourceOffset)(Generation::gRandomEngine);
        int o = totalOffset;

        while (true)
        {
            // Do
            IHSrcInfo ihs = { ih0.get(), o };
            int sourceLevel = hybrid_int_distribution(1, 5, 0.5, 0.5)(Generation::gRandomEngine);

            while (ihs && ihs.ih->getLevel() < sourceLevel)
                ihs = ihs.ih->getNearestDescendant(ihs.idx);

            // First check if the source wasn't generated outside the world or on the sea.
            auto p = ihs.getPoint();
            GPoint sq = GVector2D(p).toGPoint();

            if (Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain))
            {
                IHSrcInfo ihs0 = ihs;
                QSharedPointer<DDomain> sourceTerrainDomain;
                while (true)
                {
                    if (!ihs0.getSource())
                        break;

                    // Consult the 'Rivers' domain parameter to determine whether to use this point
                    auto p0 = ihs0.getPoint();
                    GPoint sq0 = GVector2D(p0).toGPoint();

                    auto&& potentialDomain = Generation::Data::get()->getDomainAtSquare(sq0, EDomainType::Terrain);
                    if (potentialDomain)
                        sourceTerrainDomain = potentialDomain;
                    else
                        break;

                    ihs0 = ihs0.getSource();
                }
                auto terrainData = sourceTerrainDomain->getData<EDomainType::Terrain>();
                auto&& [minChance, maxChance] = PRivers[terrainData->rivers];
                float chance = std::uniform_real_distribution<float>(minChance, maxChance)(Generation::gRandomEngine);

                if (bool(std::binomial_distribution<int>(1, chance)(Generation::gRandomEngine)))
                    if (auto infos = getShoreCollisionInfo(shores, { ihs0.getPoint(), ihs.getPoint() }); infos.empty())
                        validSources << ihs;
            }

            // Increment
            o += sourceOffset;
            if (o >= size0)
                o -= size0;

            totalOffset += sourceOffset;

            // Check
            if (totalOffset >= size0)
                break;
        }
    }

    std::vector<RiverData> rivers;

    // Real gen
    {
        std::mutex riverGuard;
        tbb::parallel_for(0, int(validSources.size()), [&](int i)
            {
                OmniProfile("Path tracing", true);

                // New river
                RiverData river;
                std::tie(river.axis, river.fallsIntoSea) = generateTerrainCompatibleRiver(validSources[i], shores);
                if (river.axis.size() < 2)
                    return;

                //Generation::Data::get()->createMarker<DLineMarker>(riverBase, QVector4D(0, 1, 1, 1));

                std::scoped_lock lock(riverGuard);
                rivers.emplace_back(std::move(river));
            });
    }

    // Randomize river order to increase influent side variety
    std::ranges::shuffle(rivers, Generation::gRandomEngine);
    createRiverNetworks(std::move(rivers));

    return true;
}

Polygon2D DRiverMarker::createBoundPolygon()
{
    std::vector<QVector3D> boundPolygonPoints;
    auto l = Generation::Data::get()->createMarker<DTrueRiverBoundMarker>(riverBounds[0]);
    boundPolygonPoints.reserve(l->getControlPoints().size());
    std::reverse_copy(l->getControlPoints().begin(), l->getControlPoints().end(), std::back_inserter(boundPolygonPoints));

    auto r = Generation::Data::get()->createMarker<DTrueRiverBoundMarker>(riverBounds[1]);
    boundPolygonPoints << r->getControlPoints();

    return Polygon2D(boundPolygonPoints);
}

void DRiverMarker::createRiverNetworks(std::vector<RiverData>&& rivers)
{
    OmniProfile("Merging");
    std::vector<QSharedPointer<DRiverMarker>> createdMarkers;

    for (auto&& river : rivers)
    {
        InfluentInfo infInfo = tryMergeIntoOtherRiver(createdMarkers, &river);
        if (river.axis.size() < 3)
            continue;

        addFloodBounds(&river);
        if (infInfo)
            mergeInfluentBounds(&river, infInfo);

        Q_ASSERT(!river.axis.empty());
        auto newRiver = Generation::Data::get()->createMarker<DRiverMarker>(river, infInfo.river);
        createdMarkers << newRiver;

        // infInfo here contains parent river instead of the influent
        if (infInfo)
            infInfo.river.lock()->addInfluent({ newRiver, infInfo.angle, infInfo.parentSegmentIdx, infInfo.parentSegmentPart });
    }
    Generation::Data::get()->initializeQueuedMarkers();

#if DEBUG_INFLUENTS
    static std::array<QVector4D, 6> colors =
    {
        QVector4D(1,0,0,1),
        QVector4D(1,1,0,1),
        QVector4D(0.5,1,0,1),
        QVector4D(0,1,0,1),
        QVector4D(0,1,0.5,1),
        QVector4D(0,1,1,1),
    };

    for (auto&& river : createdMarkers)
    {
        auto&& influents = river->getInfluents();
        for (int i = 0; i < influents.size(); ++i)
        {
            Generation::Data::get()->createMarker<DLineMarker>(
                std::vector{ influents[i].river.lock()->getControlPoints()[0] + QVector3D(0,influents[i].isRight() ? 1000 : 10000,0), influents[i].river.lock()->getControlPoints().back() }, colors[i]);
        }
    }
#endif
}

std::array<QVector3D, 2> DRiverMarker::findFloodBounds(RiverData* riverData, int pointIdx, const GVector2D& dir)
{
    Q_ASSERT(dir.length() < 1.01f && dir.length() > 0.99f);
    static const float boundStep = 50.0f;
    static const float maxFloodRange = 100'00;
    static const int maxIters = maxFloodRange / boundStep;
    static std::mutex clustersGuard;

    GVector2D leftDir = dir.rotatedLeft90();
    GVector2D rightDir = -leftDir;

    auto&& point = riverData->axis[pointIdx];
    float floodMargin = point.y() + 30.0f;

    std::vector<QVector3D> leftTrace = { point };
    std::vector<QVector3D> rightTrace = { point };

    auto leftPred = [&](auto&& a, auto&& b) { return std::abs(leftTrace.back().y() - a.y()) < std::abs(leftTrace.back().y() - b.y()); };
    for(int i=0; i<maxIters; ++i)
    {
        // Try to move further left
        auto candidates = Generation::Utils::castPointTo3DAdv(leftTrace.back() + leftDir * boundStep, leftPred);
        if (candidates.empty() || candidates[0].pos.y() > floodMargin)
            break;

        leftTrace << candidates[0].pos;

		std::scoped_lock lock(clustersGuard);
		riverData->affectedClustersKeyCells.insert(candidates[0].cluster->keyCell);
    }

    auto rightPred = [&](auto&& a, auto&& b) { return std::abs(rightTrace.back().y() - a.y()) < std::abs(rightTrace.back().y() - b.y()); };
    for (int i = 0; i < maxIters; ++i)
    {
        // Try to move further right
        auto candidates = Generation::Utils::castPointTo3DAdv(rightTrace.back() + rightDir * boundStep, rightPred);
        if (candidates.empty() || candidates[0].pos.y() > floodMargin)
            break;

        rightTrace << candidates[0].pos;

		std::scoped_lock lock(clustersGuard);
		riverData->affectedClustersKeyCells.insert(candidates[0].cluster->keyCell);
    }

    // ensure minimal width
    while (distance(GVector2D(leftTrace.back()), GVector2D(rightTrace.back())) < 200.0f)
    {
        if (Generation::randomChance() < 0.5f)
        {
            leftTrace << leftTrace.back() + leftDir * boundStep;
        }
        else
        {
            rightTrace << rightTrace.back() + rightDir * boundStep;
        }
    }

    //spawn<DLineMarker>(leftTrace);
    //spawn<DLineMarker>(rightTrace);

    return { leftTrace.back(), rightTrace.back() };
}

std::tuple<std::optional<QVector3D>, bool> DRiverMarker::extendRiver(const GVector2D& dir, const QVector3D &p, QVector3D *prev_p, float dirForce)
{
    std::optional<QVector3D> resultCenter;
    const int maxAngle = 90;
    const int angleBreakpoint = 40;

    QVector3D extendPoint;
    GVector2D currentDir;
    
    if (prev_p && !dir.isNull())
    {
        currentDir = GVector2D(p - *prev_p).normalized();
        extendPoint = GVector2D(p) + currentDir * step;
        extendPoint.setY(p.y());
        if (auto samplePoint = findRiverPoint(extendPoint, prev_p); !samplePoint)
        {
            // Next point is out of map or doesn't exist -> extend last segment and Done
            Generation::Data::get()->createMarker<DLineMarker>(extendPoint, 5000);
            return { extendPoint, true };
        }

        float best_h;
        float best_a;      
        float weightMin = std::numeric_limits<float>::lowest();

        auto usedDir = (currentDir + dir).normalized();
        
        OmniProfile("Angle-based sampling");
        for (int i = -maxAngle; i <= maxAngle; i += 10)
        {
            QQuaternion rotation = QQuaternion::fromEulerAngles(0, i, 0);
            GVector2D nextDir = rotation.rotatedVector(usedDir);
            if (auto samplePoint = findRiverPoint(p + nextDir * step, prev_p); samplePoint)
            {
                // 1 <- [y = p.y - 10.0]
                // 0 <- [y = p.y]
                // -1 <- [y = p.y + 10.0]
                float h = (p.y() - samplePoint->y()) / 10.0f;

                // 1 <- [angle = 0 "flow straight"]
                // 0 <- [angle = angleBreakpoint]
                // -1 <- [angle = maxAngle "sharpest allowed turn"]
                float a = float(angleBreakpoint - std::abs(i)) / float(angleBreakpoint);
                
                float w = h + a * dirForce;
                if (w > weightMin)
                {
                    resultCenter = *samplePoint;
                    weightMin = w;
                    best_a = a;
                    best_h = h;
                }
            }
        }
    }
    else
    {
        extendPoint = GVector2D(p) + dir * step;
        //Generation::Data::get()->createMarker<DLineMarker>(extendPoint, 5000, QVector4D(1, 0.5, 0, 1));
        if (auto samplePoint = findRiverPoint(extendPoint, prev_p); samplePoint)
        {
            resultCenter = samplePoint;
        }
        else
        {
            resultCenter = QVector3D{ extendPoint.x(), p.y(), extendPoint.z() };
        }
    }

    return { resultCenter, false };
};

std::tuple<std::vector<QVector3D>, bool> DRiverMarker::generateTerrainCompatibleRiver(const IHSrcInfo& source, const std::vector<QSharedPointer<DShorelineMarker>>& shores)
{
    OmniProfile("Terrain-based gen");

    static auto getChildIH = [](Isohypse* ih)
    {
        return ih->getNearestDescendant(0).ih;
    };

    auto srcPt = findRiverPoint(source.getPoint(), {});
    if (!srcPt)
        return {};

    std::vector<QVector3D> river = { *srcPt };
    std::vector<Isohypse*> ihsTraversed = { source.ih };
    auto* nextIh = getChildIH(ihsTraversed.back());
    Q_ASSERT(nextIh);
    bool escapeLock = false;
    float dirForce = 1.0f;

    while (true)
    {
        GVector2D dir = findRiverDir(nextIh, river.back(), &escapeLock);

        auto [p, done] = extendRiver(dir, river.back(), river.size() > 1 ? &*(river.rbegin() + 1) : nullptr, dirForce);
        if (done)
        {
            river.push_back(*p);
            alignRiverEnd(&river);
            break;
        }

        Q_ASSERT(distance(GVector2D(river.back()), GVector2D(*p)) < step + 1);

        while (nextIh && !std::get<bool>(GVector2D(*p).isInsidePolygon(nextIh->getCircularPoints())))
        {
            ihsTraversed << nextIh;
            nextIh = getChildIH(nextIh);
            escapeLock = false;
            dirForce = 1.0f;
        }

        while (ihsTraversed.size() > 1 && std::get<bool>(GVector2D(*p).isInsidePolygon(ihsTraversed.back()->getCircularPoints())))
        {
            // Oops going up on IHs! - technically possible on a very minor scale
            nextIh = ihsTraversed.back();
            ihsTraversed.pop_back();
            escapeLock = false;
            dirForce = 1.0f;
        }

        river.push_back(*p);

        if (!nextIh)
            break;

        // Handle local height minima potentially causing the river to circle around in some hole
        if (river.size() > 3)
        {
            std::array<GVector2D, 2> lastSegment = { *river.rbegin(), *(river.rbegin() + 1) };
            for (int i = 3; i < river.size(); ++i)
            {
                std::array<GVector2D, 2> checkedSegment = { *(river.rbegin() + i), *(river.rbegin() + i - 1) };
                if (auto [p, unused, d] = distance(lastSegment, checkedSegment); d < 1.0f)
                {
                    if (auto pts = Generation::Utils::castPointTo3DAdv(p); !pts.empty() && pts[0].cluster->type == Generation::ETerrainBlock::Flatland)
                    {
                        OmniLog(ELoggingLevel::Trace) <<= "Creating lake seed from River";
                        Generation::Data::get()->createMarker<DLineMarker>(river.back(), 30000, QVector4D(1, 0, 1, 1));
                        Generation::TerrainMod<Generation::ETerrainMod::Lake>::createLake(convertStlToQSet(pts[0].cluster->cells));
                        break;
                    }
                    else
                    {
                        OmniLog(ELoggingLevel::Trace) <<= "Increasing river power...";
                        spawn<DLineMarker>(river.back(), 10000, Colors::blue);
                        dirForce += 1.0f;
                    }
                }
            }
        }
    }

    bool fallsIntoSea = endMarkerAtShoreline(shores, &river);

    removeSelfIntersections<GVector2D>(&river);
    return { river, fallsIntoSea };
}

std::vector<QPair<QSharedPointer<DShorelineMarker>, int>> DRiverMarker::getShoreCollisionInfo(const std::vector<QSharedPointer<DShorelineMarker>>& shores, const QPair<QVector3D, QVector3D>& riverSegment)
{
    std::vector<QSharedPointer<DShorelineMarker>> potentialShores;

    GPoint sq = GVector2D(riverSegment.second).toGPoint(); 
    GPoint sq2 = GVector2D(riverSegment.first).toGPoint();

    for (auto&& shore : shores)
        if (shore->getSquares().contains(sq) || shore->getSquares().contains(sq2))
            potentialShores << shore;

    std::vector<QPair<QSharedPointer<DShorelineMarker>, int>> results;

    for (auto&& shore : potentialShores)
    {
        auto&& pts = shore->getControlPoints();
        for (int s = 1; s < pts.size(); ++s)
        {
            Segment2D shoreSegment = { pts[s - 1], pts[s] };
            if (shoreSegment.intersects({ riverSegment.first, riverSegment.second }, true))
                results << QPair{ shore, s };
        }
    }

    return results;
}

InfluentInfo DRiverMarker::tryMergeIntoOtherRiver(const std::vector<QSharedPointer<DRiverMarker>>& existingRivers, RiverData* river)
{
    OmniProfile("Axis merge");

    auto fallsIntoOther = [&](const QPair<QVector3D, QVector3D>& seg) -> std::optional<std::tuple<QVector3D, InfluentInfo>>
    {
        for (auto&& otherRiver : existingRivers)
        {
            auto&& otherPts = otherRiver->getControlPoints();
            for (int i = 1; i < otherPts.size(); ++i)
            {
                Segment2D checkedSegment = { seg.first, seg.second };
                Segment2D otherSegment = { otherPts[i], otherPts[i - 1] };

                if (!checkedSegment.intersects(otherSegment, true))
                    continue;

                auto [v1, v2, d] = distance(std::array{ otherPts[i - 1], otherPts[i] }, { seg.first, seg.second });
                QVector3D myFwd = checkedSegment.second - checkedSegment.first;
                QVector3D theirFwd = otherSegment.second - otherSegment.first;

                // lerpV = a + t*(b-a);
                // t = (lerpV - a)/(b-a)
                float part = (v1 - otherPts[i - 1]).length() / (otherPts[i] - otherPts[i - 1]).length();

                return std::tuple{ v1, InfluentInfo{otherRiver, angle360(myFwd.normalized(), theirFwd.normalized()), i, part } };
            }
        }

        return {};
    };

    auto computeRiverLengthUntilMerge = [](const std::vector<QVector3D>& pts, int lastIdx, const QVector3D& mergePoint)
    {
        float result = 0.0f;
        for (int i = 1; i < lastIdx; ++i)
            result += distance(pts[i],pts[i - 1]);

        result += distance(mergePoint,pts[lastIdx-1]);
        return result;
    };

    float minD = std::numeric_limits<float>::max();
    QVector3D mergePoint;
    int firstIntersectionIdx = -1;
    InfluentInfo mergeInfo;

    for (int i = 1; i < river->axis.size(); ++i)
        if (auto result = fallsIntoOther({ river->axis[i], river->axis[i-1] }); result)
        {
            auto [v, info] = *result;
            float d = computeRiverLengthUntilMerge((river->axis), i, v);
            if (d < minD)
            {
                minD = d;
                mergePoint = v;
                firstIntersectionIdx = i;
                mergeInfo = info;
                break;
            }
        }

    if (firstIntersectionIdx >= 0)
    {
        river->axis.resize(firstIntersectionIdx + 1);

        auto&& p = river->axis[firstIntersectionIdx];
        p = mergePoint;

        return mergeInfo;
    }

    return {};
}

void DRiverMarker::mergeInfluentBounds(RiverData* river, const InfluentInfo& mergeInfo)
{
    OmniProfile("Flood bounds merging");

    auto findBoundMergePoint = [&](const std::vector<QVector3D>& A, const std::vector<QVector3D>& B)
        -> std::tuple<QVector3D, QVector3D, int, int, float>
    {
        float minD = std::numeric_limits<float>::max();
        int minA = -1;
        int minB = -1;
        QVector3D bestA;
        QVector3D bestB;
        for (int a = 1; a < A.size(); ++a)
        {
            std::array<GVector2D, 2> segA{ A[a], A[a - 1] };
            for (int b = 1; b < B.size(); ++b)
            {
                std::array<GVector2D, 2> segB{ B[b], B[b - 1] };
                auto [p1, p2, d] = distance(segA, segB);

                if (d < minD)
                {
                    minD = d;
                    minA = a;
                    minB = b;
                    bestA = p1;
                    bestB = p2;
                }

                if (d < 1.f)
                    return { p1, p2, a, b, d };
            }
        }

        return { bestA, bestB, minA, minB, minD };
    };

    auto&& [leftParentBound, rightParentBound] = mergeInfo.river.lock()->riverBounds;

    // Left bound
    {
        auto [p1c, p2c, ac, bc, dc] = findBoundMergePoint(river->leftFloodBound, rightParentBound);
        auto [p1, p2, a, b, d] = findBoundMergePoint(river->leftFloodBound, leftParentBound);
        if (ac < a)
        {
            river->leftFloodBound.resize(ac + 1);
            river->leftFloodBound[ac] = p1c;
        }
        else
        {
            river->leftFloodBound.resize(a + 1);
            river->leftFloodBound[a] = p1;
        }
    }

    // Right bound
    {
        auto [p1c, p2c, ac, bc, dc] = findBoundMergePoint(river->rightFloodBound, leftParentBound);
        auto [p1, p2, a, b, d] = findBoundMergePoint(river->rightFloodBound, rightParentBound);
        if (ac < a)
        {
            river->rightFloodBound.resize(ac + 1);
            river->rightFloodBound[ac] = p1c;
        }
        else
        {
            river->rightFloodBound.resize(a + 1);
            river->rightFloodBound[a] = p1;
        }
    }
}

bool DRiverMarker::endMarkerAtShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& shores, std::vector<QVector3D>* riverPts)
{
    // When the shore is very close to the ridges, we may not find either a terrain bound or a mouth.
    // If the marker was cut at shore, attempt to cut further, to the shoreline.
    std::mutex pushGuard;
    std::set<int> infoMap;

    tbb::parallel_for(1, int(riverPts->size()), [&](int i)
        {
            auto cInfos = getShoreCollisionInfo(shores, { (*riverPts)[i - 1], (*riverPts)[i] });

            if (!cInfos.empty())
            {
                std::scoped_lock lock(pushGuard);
                infoMap.insert(i);
            }
        });

    if (infoMap.empty())
        return false;

    int clampedSize = -1;
    if (infoMap.size() == 1)
    {
        // Extend freely beyond last shoreline intersection
        clampedSize = std::min(*infoMap.begin() + 10, int((*riverPts).size()) - 1);
    }
    else
    {
        // End between intersections
        int idx = *infoMap.begin();
        int idx2 = *(++infoMap.begin());
        clampedSize = (idx + idx2) / 2;
    }

    return true;
}

std::tuple<std::vector<QVector3D>, std::vector<GVector2D>, std::vector<GVector2D>> DRiverMarker::splitAxisPoints(const std::vector<RiverAxisPoint>& pts)
{
    std::vector<QVector3D> axis(pts.size());
    std::vector<GVector2D> leftBound(pts.size());
    std::vector<GVector2D> rightBound(pts.size());

    for (int i = 0; i < pts.size(); ++i)
    {
        axis[i] = pts[i].center;
        leftBound[i] = pts[i].leftFloodBound;
        rightBound[i] = pts[i].rightFloodBound;
    }

    return { axis, leftBound, rightBound };
}

void DRiverMarker::smoothRiver(std::vector<QVector3D>* path, std::vector<QPair<GVector2D, GVector2D>>* calibrationBounds)
{
    static const float tooSharpAngle = 40.0f;

    auto calibratePoint = [&](int i)
    {
        auto s1 = (path->at(i - 1) - path->at(i - 2)).normalized();
        auto s2 = (path->at(i) - path->at(i - 1)).normalized();
        auto s3 = (path->at(i + 1) - path->at(i)).normalized();
        auto s4 = (path->at(i + 2) - path->at(i + 1)).normalized();

        float a12 = angle360(s1, s2) - 180.0f;
        float a34 = angle360(s3, s4) - 180.0f;

        // Preserve the curve
        if (std::signbit(a12) != std::signbit(a34))
            return false;

        float a23 = angle180(s2, s3);
        if (a23 < tooSharpAngle)
            return false;

        // Correction start
        QVector3D cLeft = calibrationBounds->at(i).second;
        QVector3D cRight = calibrationBounds->at(i).first;

        float dir = std::signbit(a12) ? 1.0f : -1.0f;

        for (float offset = 0.5f + 0.1f * dir; offset <= 1.0f && offset >= 0.0f; offset += 0.1f * dir)
        {
            auto c = std::lerp(cRight, cLeft, offset);

            s2 = (c - path->at(i - 1)).normalized();
            s3 = (path->at(i + 1) - c).normalized();

            float newAngle23 = angle180(s2, s3);

            if (newAngle23 < a23)
            {
                (*path)[i] = c;
                a23 = newAngle23;

                if (a23 < tooSharpAngle)
                    return true;
            }
        }

        return false;
    };

    while (true)
    {
        bool changeMade = false;

        for (int i = 2; i < int(path->size()) - 2; ++i)
            changeMade |= calibratePoint(i);

        if (!changeMade)
            break;
    }
}

GVector2D DRiverMarker::findRiverDir(Isohypse* nextIh, const QVector3D& p3, bool* escapeLock)
{
    GVector2D p = p3;
    auto parentIhs = nextIh->getParentIHs();

    // Find closest segment -> compute influence range
    float minD = std::numeric_limits<float>::max();
    for (auto* parentIh : parentIhs)
    {
        auto cPts = parentIh->getCircularPoints();

        for (int i = 0; i < cPts.getSize(); ++i)
        {
            auto p0 = GVector2D(cPts[i]);
            auto p1 = GVector2D(cPts[cPts.findIdx(i, 1)]);
            float length = distance(p0, p1);

            // Measure each segment with constant density
            int stepCount = std::ceil(length / step);

            for (int s = 0; s <= stepCount; ++s)
            {
                GVector2D segmentPoint = std::lerp(p0, p1, float(s) / float(stepCount));
                if (float d = distanceSquared(segmentPoint, p); d < minD)
                    minD = d;
            }
        }
    }
    minD = std::sqrt(minD) + 0.01f;

    // Compute ih pushback
    GVector2D pushDir;
    std::map<IHSrcInfoMulti, IHSegmentLookupData> influencers;
    std::set<Isohypse*> influencingIHs;

    static const float lookupRangeMult = 2.0f;

    for (auto* parentIh : parentIhs)
    {
        auto cPts = parentIh->getCircularPoints();
        for (int i = 0; i < cPts.getSize(); ++i)
        {
            auto p0 = GVector2D(cPts[i]);
            auto p1 = GVector2D(cPts[cPts.findIdx(i, 1)]);
            float length = distance(p0, p1);

            // Measure each segment with constant density
            int stepCount = std::ceil(length / step);
            float segmentMinD = std::numeric_limits<float>::max();
            for (int s = 0; s <= stepCount; ++s)
            {
                GVector2D segmentPoint = std::lerp(p0, p1, float(s) / float(stepCount));
                if (float d = distance(segmentPoint, p); d < lookupRangeMult * minD)
                {
                    influencingIHs.insert(parentIh);

                    auto ihsm = IHSrcInfoMulti(parentIh, { i, cPts.findIdx(i, 1) });
                    auto&& infData = influencers[ihsm];

                    float w = std::pow((lookupRangeMult - d / minD), 2);
                    infData.w += w;

                    if (d < segmentMinD)
                    {
                        segmentMinD = d;
                        infData.closestPoint = segmentPoint;
                    }
                }
            }
        }
    }

    float maxSegmentWeight = 0.0f;
    for (auto&& [segment, data] : influencers)
        maxSegmentWeight = std::max(maxSegmentWeight, data.w);

    static const QQuaternion rotateLeft90 = QQuaternion::fromEulerAngles(0, 90.0f, 0);
    auto getPointDir = [&](const IHSrcInfo& ihs) -> GVector2D
    {
        QVector3D nextToPrev = (ihs.getPoint(-1) - ihs.getPoint(1)).normalized();
        return rotateLeft90.rotatedVector(nextToPrev);
    };

    auto getNaturalDirFromSegment = [&](const IHSrcInfoMulti& segment, const GVector2D& closestPoint) -> GVector2D
    {
        float t0 = distance(GVector2D(segment[0].getPoint()), closestPoint);
        float t1 = distance(GVector2D(segment[1].getPoint()), closestPoint);
        auto dir0 = getPointDir(segment[0]);
        auto dir1 = getPointDir(segment[1]);
        return std::lerp(dir0, dir1, t0 / (t0 + t1)).normalized();
    };

    for (auto&& [segment, data] : influencers)
    {
        auto naturalDir = getNaturalDirFromSegment(segment, data.closestPoint);
        pushDir += naturalDir * (data.w / maxSegmentWeight);
    }

//     float l = pushDir.length();
//     QVector4D color;
//     if (l < 0.1f)
//         color = { 0,0,0,1 };
//     else if (l < 0.2f)
//         color = { 1,0,0,1 };
//     else if (l < 0.3f)
//         color = { 1,0.5,0,1 };
//     else if (l < 0.4f)
//         color = { 1,1,0,1 };
//     else if (l < 0.5f)
//         color = { 0,1,0,1 };
//     else
//         color = { 0,0,1,1 };
// 
//     if (l < 1.0f)
//         Generation::Data::get()->createMarker<DLineMarker>(p3, 500, color);

    if (*escapeLock || pushDir.length() < 1.0f)
    {
        static const float escapeWeightMultiplier = 3.0f;
        auto escapeDir = computeEscapeDir(influencingIHs, influencers, p3);
        Q_ASSERT(!escapeDir.isNull());

        pushDir += escapeDir * escapeWeightMultiplier;
        Q_ASSERT(!pushDir.isNull());
        *escapeLock = true;

        //spawn<DLineMarker>(p3, p3 + pushDir.normalized() * step, QVector4D(1, 0, 1, 1), 100, ELineDecorator::Arrow);
    }

    pushDir.normalize();
    //Generation::Data::get()->createMarker<DLineMarker>(std::vector{ p3, p3 + pushDir * 2.f * step }, QVector4D(1, 0, 1, 1), false);

    return pushDir;
}

GVector2D DRiverMarker::computeEscapeDir(const std::set<Isohypse*>& ihs, const std::map<IHSrcInfoMulti, IHSegmentLookupData>& influencers, const QVector3D& p3)
{
    static auto getInflowDepth = [](const IHSrcInfo& ihs)
    {
        auto descSource = ihs.ih->getNearestDescendant(ihs.idx).getSource();
        auto&& cPts = ihs.ih->getCircularPoints();
        return cPts.dist(ihs.idx, descSource.idx);
    };

    if (ihs.size() == 1)
    {
        QVector3D h = QVector3D(0, (*ihs.begin())->getHeight(), 0);

        std::vector<int> indices;
        for (auto&& [segment, data] : influencers)
        {
            if (!contains(indices, segment.indices[0]))
                indices << segment.indices[0];

            if (!contains(indices, segment.indices[1]))
                indices << segment.indices[1];
        }

        if (indices.size() < 3)
        {
            // Just 1 segment
            auto&& [segment, data] = *influencers.begin();
            int depth0 = getInflowDepth(segment[0]);
            int depth1 = getInflowDepth(segment[1]);
            return findDirToDescendant(segment[depth0 < depth1 ? 1 : 0], p3);
        }

        auto cPts = (*ihs.begin())->getCircularPoints();
        auto polyLine = cPts.sortIndices(indices);
        std::vector<GVector2D> polyPts;
        for (int i : polyLine)
            polyPts << cPts[i];

        if (auto p = GVector2D(p3); p.isInsidePolygon(polyPts))
        {
            GVector2D& p1 = polyPts.front();
            GVector2D& p2 = polyPts.back();
            return (((p1 + p2) / 2.0f) - p).normalized();
        }

        // Take the closest point of the line
        float minD = std::numeric_limits<float>::max();
        IHSrcInfoMulti winner;
        for (auto&& [segment, data] : influencers)
        {
            if (float d = distance(GVector2D(p3), data.closestPoint); d < minD)
            {
                minD = d;
                winner = segment;
            }
        }

        int depth0 = getInflowDepth(winner[0]);
        int depth1 = getInflowDepth(winner[1]);
        return findDirToDescendant(winner[depth0 < depth1 ? 1 : 0], p3);
    }
    else
    {
        // Weight all influencers' dir to descendant
        GVector2D pushDir;

        std::map<Isohypse*, std::map<int, float>> infs;
        for (auto&& [segment, data] : influencers)
        {
            infs[segment.ih][segment.indices[0]] += data.w;
            infs[segment.ih][segment.indices[1]] += data.w;
        }

        float maxSegmentWeight = 0.0f;
        for (auto&& [ih, ptInfos] : infs)
            for (auto [idx, w] : ptInfos)
                maxSegmentWeight = std::max(maxSegmentWeight, w);

        for (auto&& [ih, ptInfos] : infs)
            for (auto [idx, w] : ptInfos)
            {
                //QVector3D h(0, ih->getHeight() - 100, 0);
                //spawn<DLineMarker>(p3, ih->getControlPoints()[idx] + h, QVector4D(1, 1, 1, 1), 100, ELineDecorator::Arc);
                auto descDir = findDirToDescendant(IHSrcInfo{ ih, idx }, p3);
                pushDir += descDir * (w / maxSegmentWeight);
            }
        pushDir.normalize();

        //Generation::Data::get()->createMarker<DLineMarker>(std::vector{ p3, p3 + pushDir * 300.0f }, QVector4D(1, 1, 0, 1));
        return pushDir;
    }
}

GVector2D DRiverMarker::findDirToDescendant(const IHSrcInfo& ihs, auto&& p3, bool debug /*= false*/)
{
    if (ihs.getDescendant())
    {
        GVector2D result = GVector2D(ihs.getDescendant().getPoint() - ihs.getPoint()).normalized();
        if (debug)
            spawn<DLineMarker>(ihs.getPoint(), ihs.getPoint() + result * 500.f, QVector4D(0, 1, 0, 1), ihs.ih->getHeight(), ELineDecorator::Arrow);

        return result;
    }

    auto ndp = ihs.ih->getNearestDescendant(ihs.idx).getSource();
    Q_ASSERT(ndp.ih == ihs.ih);

    auto cPts = ihs.ih->getCircularPoints();

    float dCW = 0.0f;
    for (int i = ihs.idx, i2 = cPts.findIdx(i, 1); i != ndp.idx; i = i2, i2 = cPts.findIdx(i, 1))
        dCW += distance(cPts[i], cPts[i2]);

    float dCCW = 0.0f;
    for (int i = ihs.idx, i2 = cPts.findIdx(i, -1); i != ndp.idx; i = i2, i2 = cPts.findIdx(i, -1))
        dCCW += distance(cPts[i], cPts[i2]);

    GVector2D result = findShortcutToDescendant(ihs, (dCW < dCCW) ? 1 : -1, ndp.idx);
    if (debug)
        spawn<DLineMarker>(ihs.getPoint(), ihs.getPoint() + result * 500.f, QVector4D(0, 1, 0, 1), ihs.ih->getHeight(), ELineDecorator::Arrow);

    return result;
}

GVector2D DRiverMarker::findShortcutToDescendant(const IHSrcInfo& ihs, int dir, int target)
{
    QVector3D src = ihs.getPoint();
    QVector3D dst = ihs.getPoint(dir);

    auto cPts = ihs.ih->getCircularPoints();
    int dist = cPts.dist(ihs.idx, target);

    // Descendant's parent is a neighbor
    if (dist == 1)
        return GVector2D(dst - src).normalized();

    // Can take shortcut only if we stay outside IH
    if (dist == 2)
        if (ihs.ih->getLevel() > 0 && !std::get<bool>(GVector2D((src + cPts[target]) / 2).isInsidePolygon(cPts)))
            return GVector2D(cPts[target] - src).normalized();
        else
            return GVector2D(dst - src).normalized();

    for (int i = target; i != cPts.findIdx(ihs.idx, dir); i = cPts.findIdx(i, -dir))
    {
        Segment2D shortcut{ cPts[ihs.idx], cPts[i] };

        auto mp = shortcut.midpoint();
        if (std::get<bool>(mp.isInsidePolygon(cPts)))
            continue;

        bool shortcutValid = true;
        for (int c = cPts.findIdx(i, -dir); c != cPts.findIdx(ihs.idx, dir); c = cPts.findIdx(c, -dir))
        {
            int c2 = cPts.findIdx(c, -dir);
            Segment2D ihSegment{ cPts[c], cPts[c2] };
            if (shortcut.intersects(ihSegment, true))
            {
                shortcutValid = false;
                break;
            }
        }

        if (!shortcutValid)
            continue;

        for (int c = cPts.findIdx(i, dir); c != cPts.findIdx(ihs.idx, -dir); c = cPts.findIdx(c, dir))
        {
            int c2 = cPts.findIdx(c, dir);
            Segment2D ihSegment{ cPts[c], cPts[c2] };
            if (shortcut.intersects(ihSegment, true))
            {
                shortcutValid = false;
                break;
            }
        }

        if (shortcutValid)
        {
            dst = cPts[i];
            break;
        }
    }

    //QVector3D h{ 0, ihs.ih->getHeight() + 200, 0 };
    //Generation::Data::get()->createMarker<DLineMarker>(src + h, dst + h, QVector4D(1, 1, 1, 1), 0, ELineDecorator::Arrow);
    return GVector2D(dst - src).normalized();
}

void DRiverMarker::addFloodBounds(RiverData* river)
{
    OmniProfile("Flood bounds");

    river->leftFloodBound.resize(river->axis.size());
    river->rightFloodBound.resize(river->axis.size());

    river->leftFloodBound[0] = river->axis[0];
    river->rightFloodBound[0] = river->axis[0];

    tbb::parallel_for(1, int(river->axis.size() - 1), [&](int i)
        {
            auto dir = GVector2D(river->axis[i + 1] - river->axis[i - 1]).normalized();
            auto&& [l, r] = findFloodBounds(river, i, dir);
            river->leftFloodBound[i] = l;
            river->rightFloodBound[i] = r;
        });

    GVector2D lastDir = river->axis.back() - *(river->axis.rbegin() + 1);
    auto&& [l, r] = findFloodBounds(river, river->axis.size() - 1, lastDir.normalized());
    river->leftFloodBound.back() = l;
    river->rightFloodBound.back() = r;

    // Smoothing
    auto&& axis = river->axis;
    auto&& leftBound = river->leftFloodBound;
    auto&& rightBound = river->rightFloodBound;

    auto findNearestAxisSegment = [&](const GVector2D& p)
    {
        float minD = std::numeric_limits<float>::max();
        int segmentHead = -1;
        for (int i = 1; i < axis.size(); ++i)
        {
            float d = distance(GVector2D(axis[i]), p) + distance(GVector2D(axis[i - 1]), p);
            if (d < minD)
            {
                minD = d;
                segmentHead = i;
            }
        }

        return segmentHead;
    };

    std::vector<int> leftBoundAxisRefs(leftBound.size());
    for (int i = 0; i < leftBound.size(); ++i)
        leftBoundAxisRefs[i] = findNearestAxisSegment(GVector2D(leftBound.at(i)));

    std::vector<int> rightBoundAxisRefs(rightBound.size());
    for (int i = 0; i < rightBound.size(); ++i)
        rightBoundAxisRefs[i] = findNearestAxisSegment(GVector2D(rightBound.at(i)));

    auto smoothBound = [&](QVector3D& p, const QVector3D& prevP, const QVector3D& ref, const QVector3D& prevRef)
    {
        float deltaH = ref.y() - prevRef.y();
        float maxBoundDelta = step / (std::max(deltaH, 0.05f) * 10.0f);

        float boundRange = distance(p, ref);
        float prevBoundRange = distance(GVector2D(prevP), GVector2D(prevRef));
        if (qAbs(boundRange - prevBoundRange) > maxBoundDelta)
        {
            auto dir = (GVector2D(p) - GVector2D(ref)).normalized();
            auto newRange = prevBoundRange + ((boundRange > prevBoundRange) ? 1.f : -1.f) * maxBoundDelta;
            p = ref + dir * newRange;
        }
    };

    // Forward correction
    for (int i = 1; i < leftBound.size(); ++i)
        smoothBound(leftBound.at(i), leftBound.at(i - 1), axis[leftBoundAxisRefs[i]], axis[leftBoundAxisRefs[i - 1]]);
    for (int i = 1; i < rightBound.size(); ++i)
        smoothBound(rightBound.at(i), rightBound.at(i - 1), axis[rightBoundAxisRefs[i]], axis[rightBoundAxisRefs[i - 1]]);

    // Backward correction
    for (int i = leftBound.size() - 2; i >= 0; --i)
        smoothBound(leftBound.at(i), leftBound.at(i + 1), axis[leftBoundAxisRefs[i]], axis[leftBoundAxisRefs[i + 1]]);
    for (int i = rightBound.size() - 2; i >= 0; --i)
        smoothBound(rightBound.at(i), rightBound.at(i + 1), axis[rightBoundAxisRefs[i]], axis[rightBoundAxisRefs[i + 1]]);

    removeSelfIntersections<GVector2D>(&leftBound);
    removeSelfIntersections<GVector2D>(&rightBound);
}

void DRiverMarker::alignRiverEnd(std::vector<QVector3D>* path)
{
    if (path->size() < 2)
        return;

    GPoint insideSq = GVector2D(*(path->rbegin() + 1)).toGPoint();
    GPoint outsideSq = GVector2D(path->back()).toGPoint();

    if (insideSq == outsideSq)
        return;

    GVector2D dir = { float(outsideSq.x - insideSq.x), float(outsideSq.z - insideSq.z) };

    path->push_back(path->back() + QVector3D(dir) * GRID_SEGMENT_WIDTH);
    spawn<DLineMarker>(path->back());
}

std::optional<QVector3D> DRiverMarker::findRiverPoint(const GVector2D& p, QVector3D* prev_p)
{
    if (prev_p)
    {
        auto pred = [&](auto&& a, auto&& b) { return std::abs(prev_p->y() - a.y()) < std::abs(prev_p->y() - b.y()); };
        auto candidates = Generation::Utils::castPointTo3DAdv(p, pred);
        if (candidates.empty() || candidates[0].cluster->type == Generation::ETerrainBlock::Seabed)
            return {};
        else
            return candidates[0].pos;
    }
    else
    {
        auto candidates = Generation::Utils::castPointTo3D(p);
        return candidates.empty() ? std::optional<QVector3D>{} : candidates[0];
    }
}

std::vector<QVector3D> DRiverMarker::getAxisCenters(const std::vector<RiverAxisPoint>& axisPoints)
{
    std::vector<QVector3D> result(axisPoints.size());
    std::ranges::transform(axisPoints, result.begin(), [](const RiverAxisPoint& axisPoint) { return axisPoint.center; });
    return result;
}

bool InfluentInfo::operator<(const InfluentInfo& other) const
{
    if (parentSegmentIdx < other.parentSegmentIdx)
    {
        return true;
    }
    else if (parentSegmentIdx == other.parentSegmentIdx)
    {
        if (parentSegmentPart < other.parentSegmentPart)
        {
            return true;
        }
        else if (parentSegmentPart == other.parentSegmentPart)
        {
            return qAbs(angle - 180.0f) < qAbs(other.angle - 180.0f);
        }
    }

    return false;
}