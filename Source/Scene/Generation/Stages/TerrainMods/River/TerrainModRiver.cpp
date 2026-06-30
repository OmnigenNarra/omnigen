#include "stdafx.h"
#include "TerrainModRiver.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Omnigen.h"
#include "RiverMarker.h"
#include <Qt3DCore>
#include <Editor/Sections/Profiler/OmnigenProfiler.h>
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "RiverNurbsMarker.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Utils/Colors.h"
#include "RiverSurfaceMarker.h"

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <unordered_set>
#include <noise/noise.h>
#include <Mathematics/BezierCurve.h>


namespace Generation
{
    std::vector<QSharedPointer<TerrainModBase>> TerrainMod<ETerrainMod::River>::generateAll()
    {
        finalRiverbeds.clear();

        DRiverMarker::generateAll();
        auto riverRoots = Data::get()->getMarkers<DRiverMarker>();

        // Create a Mod for each River marker
        std::vector<QSharedPointer<TerrainModBase>> mods;
        std::mutex modsGuard;

        tbb::parallel_for(0, int(riverRoots.size()), [&](int i)
            {
                std::vector<QSharedPointer<TerrainModBase>> newMods;
                processRiver(riverRoots[i], {}, &newMods);

                std::scoped_lock lock(modsGuard);
                mods << newMods;
            });

        return mods;
    }

    QSharedPointer<TerrainModBase> TerrainMod<ETerrainMod::River>::processSingleRiver(const QSharedPointer<DRiverMarker>& river)
    {
        // Find parent mod
        QSharedPointer<TerrainMod<ETerrainMod::River>> parentMod;
        if (river->getParent())
        {
            auto* parentRiver = river->getParent().lock().get();
            for (auto&& mod : Data::get()->getTerrainMods()[ETerrainMod::River])
                if (auto riverMod = mod.staticCast<TerrainMod<ETerrainMod::River>>(); riverMod->river == parentRiver)
                {
                    parentMod = riverMod;
                    break;
                }
        }

        auto newMod = QSharedPointer<TerrainMod<ETerrainMod::River>>::create(river, parentMod);
        Data::get()->addTerrainMod(ETerrainMod::River, newMod);

        emit Editable::created(newMod);
        return newMod;
    }

    void TerrainMod<ETerrainMod::River>::processRiver(const QSharedPointer<DRiverMarker>& river, QSharedPointer<TerrainModBase> parentMod, std::vector<QSharedPointer<TerrainModBase>>* outputMods)
    {
        // Process this river
        auto mod = QSharedPointer<TerrainMod<ETerrainMod::River>>::create(river, parentMod);
        *outputMods << mod;

        for (auto&& child : river->getChildren())
            processRiver(child, mod, outputMods);
    }

    bool TerrainMod<ETerrainMod::River>::cellContainsRiver(const QSharedPointer<DRiverMarker>& river, const Polygon2D& cell)
    {
        auto& bounds = river->getBoundPolygon();
        auto&& cPts = bounds.getCPts();

        for (int i = 0; i < cPts.getSize(); ++i)
        {
            int i2 = cPts.findIdx(i, 1);
            if (cPts[i] != cPts[i2])
                if (!cell.rayIntersections({ cPts[i], cPts[i2] }).isEmpty())
                    return true;
        }

        for (auto&& p : cell.getPts())
        {
            if (bounds.contains(p))
                return true;
        }

        return false;
    }

    void TerrainMod<ETerrainMod::River>::postLoad(TerrainModBase* object)
    {
        auto* riverMod = static_cast<TerrainMod<ETerrainMod::River>*>(object);
        riverMod->river = Generation::Data::get()->findMarkerByGuid<DRiverMarker>(riverMod->riverGuid);
        riverMod->nurb = Generation::Data::get()->findMarkerByGuid<DRiverNurbsMarker>(riverMod->nurbGuid);
        //if(riverMod->parentRiverMod) 
        //    riverMod->parentRiverMod = Generation::Data::get()->findModByGuid<ETerrainMod::River>(riverMod->parentModGuid); 
    }

    void TerrainMod<ETerrainMod::River>::setName(const QString& newName)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        name = newName;
        emit Editable::modified(sharedFromThis());
    }

    TerrainMod<ETerrainMod::River>::TerrainMod(const QSharedPointer<DRiverMarker>& inRiver, const QSharedPointer<TerrainModBase>& inParentMod)
        : TerrainModBase(ETerrainMod::River, inRiver->getArea())
        , river(inRiver)
        , parentRiverMod(inParentMod.staticCast<TerrainMod<ETerrainMod::River>>())
	{
        Q_ASSERT(!river->getControlPoints().empty());
        riverGuid = river->getGuid();
        if (parentRiverMod)
            parentModGuid = parentRiverMod->getGuid();

        createNurbShapes();
        makeName();
	}

    void TerrainMod<ETerrainMod::River>::makeName()
    {
        static int nameCounter = 0;
        name = "River " + QString::number(++nameCounter);
    }

    TerrainMeshVertex TerrainMod<ETerrainMod::River>::apply(const std::vector<TerrainMeshVertex>& alterations)
    {
        TerrainMeshVertex result;
        result.position.setY(std::numeric_limits<float>::max());

        for (auto&& alt : alterations)
            if (alt.position.y() < result.position.y())
                result = alt;

        return result;
    }

    void TerrainMod<ETerrainMod::River>::clearAll()
    {
        Data::get()->clearExactMarkers<DRiverMarker>();
        Data::get()->clearExactMarkers<DTrueRiverBoundMarker>();
        Data::get()->clearExactMarkers<DRiverNurbsMarker>();
        Data::get()->clearExactMarkers<DRiverSurfaceMarker>();
    }

    std::array<std::vector<QSharedPointer<EnvBound>>, 2> TerrainMod<ETerrainMod::River>::computeRawEnvBounds() const
    {
        auto&& cells = Data::get()->getTerrainCells()->getCells();
        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& blockTree = Generation::Data::get()->getBlockQuadTree();
        float maxRadius = Generation::Data::get()->getLargestVoronoiCellRadius();

        std::array<std::vector<QSharedPointer<EnvBound>>, 2> results;

        auto finalizeEnvBound = [&](int side, std::vector<QVector3D>&& bound, int cellIdx)
        {
            auto envBound = QSharedPointer<EnvBound>::create(clusterMap[cellIdx]->keyCell);
            envBound->line = std::move(bound);
            results[side] <<= envBound;
            //spawn<DLineMarker>(envBound->line);
        };

        auto findCellContainingPoint = [&](const GVector2D& p) -> int
        {
            for (auto&& cellNode : blockTree->find_all_nearest(p.x, p.z, maxRadius))
                if (cells[cellNode->data]->contains(p))
                    return cellNode->data;

            return -1;
        };

        for (int side = 0; side < 2; ++side)
        {
            auto&& edgePts = nurb->getEdges()[side];

            // Starting cell
            int currentCellId = findCellContainingPoint(edgePts[0]);

            // Bound being built
            std::vector<QVector3D> bound = { findRiverBoundPoint(edgePts[0], edgePts[0].y()) };

            for (int e = 1; e < edgePts.size(); ++e)
            {
                auto&& edgePoint = edgePts[e];
                Segment2D advancement = { bound.back(), edgePoint };

                if (currentCellId == -1)
                {
                    Data::get()->createMarker<DLineMarker>(edgePoint, 10000, QVector4D(1,1,0,1));

                    // Outside of the world
                    // Try to reinitialize bound if next point is inside the world
                    if (currentCellId = findCellContainingPoint(edgePoint); currentCellId != -1)
                    {
                        Q_ASSERT(e > 1);
                        auto newCellIntersections = cells[currentCellId]->rayIntersections(advancement);
                        bound = { findRiverBoundPoint(std::get<GVector2D>(newCellIntersections.first()), edgePoint.y()) };
                    }
                    else
                    {
                        bound = { edgePoint };
                        continue;
                    }
                }

                auto currentCellIntersections = cells[currentCellId]->rayIntersections(advancement);
                if (currentCellIntersections.isEmpty())
                {
                    // Moving within same cell
                    bound << findRiverBoundPoint(edgePoint, edgePoint.y());

                    // Last edge point
                    if (e == (edgePts.size() - 1))
                        finalizeEnvBound(side, std::move(bound), currentCellId);

                    continue;
                }

                // Going outside of the current cell!
                for (auto hitIt = currentCellIntersections.begin(); hitIt != currentCellIntersections.end(); ++hitIt)
                {
                    auto&& [hit, unused] = *hitIt;
                    if (hit == GVector2D(bound.back()))
                        continue;

                    // Finalize current bound
                    auto hit3D = findRiverBoundPoint(hit, edgePoint.y());
                    bound << hit3D;
                    finalizeEnvBound(side, std::move(bound), currentCellId);

                    // Initialize next bound
                    bound = { hit3D };

                    // Change current cell!
                    auto nextHitIt = hitIt + 1;
                    if (nextHitIt == currentCellIntersections.end())
                    {
                        // Leaving for good
                        currentCellId = findCellContainingPoint(edgePoint);
                        break;
                    }

                    // Point between this and next intersection is within another cell
                    auto&& [nextHit, nextUnused] = *nextHitIt;
                    currentCellId = findCellContainingPoint((bound.back() + nextHit) / 2);
                }

                // All intersections processed, end of segment
                bound << findRiverBoundPoint(edgePoint, edgePoint.y());
            }
        }
        return results;
    }

    void TerrainMod<ETerrainMod::River>::computeNurbsMerging(std::vector<RiverRowInfo>* mergedSource, const std::vector<RiverRowInfo>& mergeTarget)
    {
        Q_ASSERT(!mergeTarget.empty());

        std::array<GVector2D, 2> lastInfluentSeg = { mergedSource->back().CP.front(), mergedSource->back().CP.back() };
        std::array<GVector2D, 2> firstInfluentSeg = { mergedSource->front().CP.front(), mergedSource->front().CP.back() };
        int parentSize = mergeTarget.size();

        int closestPtLast = 0;
        float minDistLast = std::numeric_limits<float>::max();
        for (int i = 0; i < parentSize; ++i)
        {
            std::array<GVector2D, 2> seg = { mergeTarget[i].CP.front(), mergeTarget[i].CP.back() };

			if (auto [p1, p2, d] = distance(seg, lastInfluentSeg); d < minDistLast)
			{
				closestPtLast = i;
				minDistLast = d;
			}
        }

        auto addCP = [&](int idx, int closestPt, bool riverse = false)
        {
            if (closestPt + idx < parentSize && closestPt + idx >= 0)
            {
                *mergedSource << mergeTarget[closestPt + idx];
                if (riverse) std::rotate(mergedSource->rbegin(), mergedSource->rbegin() + 1, mergedSource->rend());
            }
            else
            {
                if (!riverse)
                {
                    *mergedSource << mergeTarget.back();
                }
                else
                {
                    *mergedSource << mergeTarget.front();
                    std::rotate(mergedSource->rbegin(), mergedSource->rbegin() + 1, mergedSource->rend());
                }
            }
        };

        //degree+1 times for uniform smooth transition
        const int degree = 3;
        for (int i = 0; i < degree + 1; i++)
            addCP(i + 1, closestPtLast);
    }

    std::vector<ERiverType> selectPossibleRiverTypes(float slope, float terrainSlot, float floodRange, float avgEntrenchment)
    {
        // TODO: Coarse-grained type deduction / user selection?
        return { ERiverType::C };

        std::vector<ERiverType> results;

        if (avgEntrenchment > 200.0f)
            return { ERiverType::G };

        for (int i = 0; i<int(ERiverType::Last); ++i)
        {
            auto&& [slopeMin, slopeMax] = ERiverTypeConstexpr::UseIn<EAC::GetRiverTraits>(ERiverType(i)).slopeAngleRange;
            if (slope == std::clamp(slope, slopeMin, slopeMax))
                results.push_back(ERiverType(i));
        }

        return results;
    }

    void TerrainMod<ETerrainMod::River>::createNurbShapes()
    {
        auto&& riverPts = river->getControlPoints();

        // Detect arcs
        std::vector<RiverArc> arcs = detectArcs(river);

        // Ensure river isn't going up
        std::vector<QVector3D> correctedRiverPts = riverPts;
        for (int i = 1; i < correctedRiverPts.size(); ++i)
            correctedRiverPts[i].setY(std::min(correctedRiverPts[i - 1].y(), correctedRiverPts[i].y()));
        
        // Assign possible river types to each arc
        std::vector<std::vector<ERiverType>> possibleTypes(arcs.size());
        for (int arcIdx = 0; arcIdx < arcs.size(); ++arcIdx)
        {
            auto&& arc = arcs[arcIdx];

            float distanceTotal;
            float dhTotal = 0.0f;
            float entrenchmentTotal = 0.0f;

            Q_ASSERT(arc.range[0] < arc.range[1]);
            for (int i = arc.range[0] + 1; i <= arc.range[1]; ++i)
            {
                float dh = riverPts[i].y() - riverPts[i - 1].y();
                dhTotal += dh;

                float da = GVector2D(riverPts[i]).dist(GVector2D(riverPts[i - 1]));
                distanceTotal += da;

                entrenchmentTotal += (riverPts[i].y() - correctedRiverPts[i].y());
            }
            float numPts = arc.range[1] - arc.range[0];
            dhTotal /= numPts;
            distanceTotal /= numPts;
            entrenchmentTotal /= numPts;

            possibleTypes[arcIdx] = selectPossibleRiverTypes(std::clamp(std::fabsf(dhTotal) / distanceTotal, 0.0f, 1.0f), 0, 0, entrenchmentTotal);
        }

        // Merge arcs into segments
        std::vector<RiverSegmentData> riverSegments;
        std::array<int, 2> currentSegment = { 0, 0 }; // arc indices
        std::vector<ERiverType> currentPossibleTypes = { ERiverType::Aa, ERiverType::B };
        for (int arcIdx = 1; arcIdx < arcs.size(); ++arcIdx)
        {
            auto crossTypes = container_and(currentPossibleTypes, possibleTypes[arcIdx]);
            if (!crossTypes.empty()) // Extend segment, narrow down types
            {
                currentPossibleTypes = crossTypes;
                currentSegment[1] = arcIdx;
            }
            else 
            {
                // Make new segment
                auto type = randomPick(currentPossibleTypes, gRandomEngine);
                riverSegments <<= RiverSegmentData{ type, { currentSegment[0], currentSegment[1] } };

                // Setup next segment stub
                currentSegment = { arcIdx, arcIdx };
                currentPossibleTypes = possibleTypes[arcIdx];
            }
        }

        // Finalize last segment
        auto type = randomPick(currentPossibleTypes, gRandomEngine);
        riverSegments <<= RiverSegmentData{ type, {currentSegment[0], int(arcs.size()) - 1} };

        // DEBUG RIVER SEGMENTS
#if 1
        static const std::map<ERiverType, QVector4D> riverColors =
        {
            {ERiverType::Aa, Colors::red},
            {ERiverType::A, Colors::orange},
            {ERiverType::B, Colors::yellow},
            {ERiverType::C, Colors::green},
            {ERiverType::D, Colors::cyan},
            {ERiverType::DA, Colors::white},
            {ERiverType::E, Colors::blue},
            {ERiverType::F, Colors::azure},
            {ERiverType::G, Colors::purple},
        };

        for (auto&& [type, range, w, d] : riverSegments)
            spawn<DLineMarker>(riverPts[arcs[range[0]].range[0]], riverPts[arcs[range[1]].range[1]], riverColors.at(type));
#endif

        // Segment params
        for (auto&& segment : riverSegments)
            segment.computeParams(arcs, river);

        // Generate bed axes
        auto axis = generateRiverAxes(correctedRiverPts, arcs, riverSegments);

        std::vector<QVector3D> pts(axis.size());
        std::ranges::transform(axis, pts.begin(), [](auto&& p) {return p.pos; });
        spawn<DLineMarker>(pts, Colors::green, false, 100);

        auto boundedFloodBounds = refineFloodBounds(axis, river);

        // Generate bed control points
        std::vector<RiverRowInfo> mainRiverbed = createMainRiverbed(river, axis, boundedFloodBounds);

        // Invalid river
        if (mainRiverbed.size() < 4)
            return;

        if (parentRiverMod)
            computeNurbsMerging(&mainRiverbed, finalRiverbeds[parentRiverMod.get()]);

        nurb = spawn<DRiverNurbsMarker>(mainRiverbed, river->fallsIntoSea(), true, true, QVector4D(1, 1, 0, 1));
        nurbGuid = nurb->getGuid();

        finalRiverbeds[this] = std::move(mainRiverbed);
    }

    float TerrainMod<ETerrainMod::River>::calculateRiverOriginDisplacement(const TerrainMeshVertex& origin) const
    {
        // TODO: Make a reliable solution
        return 0;
    }

    QVector3D TerrainMod<ETerrainMod::River>::findRiverBoundPoint(const GVector2D& edgePoint, float refH)
    {
        auto pred = [&](auto&& a, auto&& b) { return std::abs(refH - a.y()) < std::abs(refH - b.y()); };
        auto candidates = Generation::Utils::castPointTo3D(edgePoint, pred);
        return candidates.empty() ? QVector3D(edgePoint) : *candidates.begin();
    }

    std::vector<RiverNurbAxisPoint> TerrainMod<ETerrainMod::River>::generateRiverAxes(const std::vector<QVector3D>& riverPts, const std::vector<RiverArc>& arcs, const std::vector<RiverSegmentData>& segments)
    {
        std::vector<RiverNurbAxisPoint> axis;
        axis <<= RiverNurbAxisPoint{ riverPts[0], segments[0].avgWidth, segments[0].avgDepth, segments[0].type };
        auto pred = [&](auto&& a, auto&& b) { return std::abs(axis.back().pos.y() - a.y()) < std::abs(axis.back().pos.y() - b.y()); };

        // Make a bezier curve for each arc
        for (int arcIdx = 0; arcIdx < arcs.size(); ++arcIdx)
        {
            auto&& [range, side] = arcs[arcIdx];

            Segment2D shortcut(riverPts[range[0]], riverPts[range[1]]);
            float shortcutLength = shortcut.length();

            // Find segment
            const RiverSegmentData* segment = nullptr;
            for(auto&& s : segments)
                if (arcIdx >= s.arcRange[0] && arcIdx <= s.arcRange[1])
                {
                    segment = &s;
                    break;
                }

            auto [sinMin, sinMax] = ERiverTypeConstexpr::UseIn<EAC::GetRiverTraits>(segment->type).sinusoity;
            float sinusoity = std::uniform_real_distribution<float>(sinMin, sinMax)(gRandomEngine);

            // Lots of math here.
            float arcRadius = std::sin(acosf(1.0f / sinusoity)) * shortcutLength * sinusoity * 0.5f;

            int focus = hybrid_int_distribution<int>(range[0], range[1], 0.5, 0.5)(gRandomEngine);
            auto [shortcutPoint, d] = distance(shortcut, riverPts[focus]);
            auto dir = (GVector2D(riverPts[focus]) - shortcutPoint).normalized();

            // Curve points
            std::vector<gte::Vector<2, float>> curvePts =
            {
                GtoV2(riverPts[range[0]]),
                GtoV2(shortcutPoint + dir * arcRadius),
                GtoV2(riverPts[range[1]])
            };

            // Smoothing
            if (arcIdx > 0)
            {
                auto&& prev = (axis.rbegin() + 1)->pos;
                dir = GVector2D(riverPts[range[0]] - prev).normalized();
                curvePts.insert(curvePts.begin() + 1, GtoV2(riverPts[range[0]] + dir * arcRadius));
            }

            // Construct curve
            gte::BezierCurve<2, float> path(curvePts.size() - 1, curvePts.data());
            float curveLength = shortcutLength * sinusoity;
            int curveSteps = std::ceil(curveLength / DRiverMarker::step);

            // Simplifying river end.
            if (arcIdx == arcs.size() - 1)
                curveSteps = 10;

            // Create final axis
            for (int s = 1; s <= curveSteps; ++s)
            {
                gte::Vector<2, float> p;
                path.Evaluate(float(s) / float(curveSteps), 0, &p);
                if (auto pts = Utils::castPointTo3D(VtoG2(p), pred); !pts.empty())
                    axis <<= RiverNurbAxisPoint{ pts[0], segment->avgWidth, segment->avgDepth, segment->type };
                else
                    axis <<= RiverNurbAxisPoint{ QVector3D(VtoG2(p)) + QVector3D(0, axis.back().pos.y(), 0), segment->avgWidth, segment->avgDepth, segment->type };
            }
        }

        // Ensure river isn't going up
        for (int i = 1; i < axis.size(); ++i)
            axis[i].pos.setY(std::min(axis[i - 1].pos.y(), axis[i].pos.y()));

        return axis;
    }

    void removeSelfIntersectionsNoSkip(std::vector<RiverNurbBoundPoint>* inPts)
    {
        auto& pts = *inPts;
        for (int i = 1; i < pts.size(); ++i)
        {
            std::array<GVector2D, 2> earlySegment = { pts[i - 1].pos, pts[i].pos };
            for (int c = i + 2; c < pts.size(); ++c)
            {
                std::array<GVector2D, 2> lateSegment = { pts[c - 1].pos, pts[c].pos };
                if (auto [p0, p1, d] = distance(earlySegment, lateSegment); d < 1.f)
                {
                    // Snap to intersection point
                    auto&& [s0, s1] = earlySegment;
                    float f = distance(s0, p0) / distance(s0, s1);
                    pts[i].pos = std::lerp(pts[i - 1].pos, pts[i].pos, f);
                    pts[i].axisPoint = std::lerp(pts[i - 1].axisPoint, pts[i].axisPoint, f);
                    pts[i].width = std::lerp(pts[i - 1].width, pts[i].width, f);
                    pts[i].depth = std::lerp(pts[i - 1].depth, pts[i].depth, f);

                    // Remove all points between i and c
                    pts.erase(pts.begin() + i + 1, pts.begin() + c);

                    // Done for this segment
                    break;
                }
            }
        }
    }

    std::array<std::vector<RiverNurbBoundPoint>, 2> TerrainMod<ETerrainMod::River>::refineFloodBounds(const std::vector<RiverNurbAxisPoint>& axis, const QSharedPointer<DRiverMarker>& river)
    {
        auto&& [lBound, rBound] = river->getRiverBounds();
        std::array<std::vector<RiverNurbBoundPoint>, 2> nurbBounds;

        for (int i = 0; i < axis.size(); ++i)
        {
            // Forward dir
            GVector2D forwardDir;
            if (i == 0)
            {
                forwardDir = axis[1].pos - axis[0].pos;
            }
            else if (i == axis.size() - 1)
            {
                forwardDir = axis.back().pos - (axis.rbegin() + 1)->pos;
            }
            else
            {
                forwardDir = axis[i].pos - axis[i - 1].pos;
            }
            forwardDir.normalize();

            // Left
            GVector2D leftDir = forwardDir.rotatedLeft90();

            auto&& p = axis[i].pos;
            auto [lP, lDist, lIdx] = directionalBoundDistance(lBound, p);
            auto [rP, rDist, rIdx] = directionalBoundDistance(rBound, p);

            float finalWidth = std::min(axis[i].width, lDist + rDist);
            QVector3D leftPoint = p + leftDir * finalWidth * 0.5f;
            QVector3D rightPoint = p - leftDir * finalWidth * 0.5f;

            nurbBounds[0] <<= RiverNurbBoundPoint{ rightPoint, p, axis[i].width, axis[i].depth, axis[i].type };
            nurbBounds[1] <<= RiverNurbBoundPoint{ leftPoint, p, axis[i].width, axis[i].depth, axis[i].type };
        }

        // Sculpt width at source
        Q_ASSERT(nurbBounds[0].size() == nurbBounds[1].size());
        int interpolationEnd = std::min(8, int(nurbBounds[0].size()));
        for (int i = 0; i < interpolationEnd; ++i)
        {
            float f = float(i + 1) / float(interpolationEnd + 1);
            nurbBounds[0][i].pos = std::lerp(nurbBounds[0][i].axisPoint, nurbBounds[0][i].pos, f);
            nurbBounds[1][i].pos = std::lerp(nurbBounds[1][i].axisPoint, nurbBounds[1][i].pos, f);
        }

        removeSelfIntersectionsNoSkip(&nurbBounds[0]);
        removeSelfIntersectionsNoSkip(&nurbBounds[1]);

        std::vector<QVector3D> b0(nurbBounds[0].size()), b1(nurbBounds[1].size());
        std::ranges::transform(nurbBounds[0], b0.begin(), [](auto&& bp) {return bp.pos; });
        std::ranges::transform(nurbBounds[1], b1.begin(), [](auto&& bp) {return bp.pos; });

        // Remove awkward back-directed parts at the ends
        auto [rp, rd, rIdx] = directionalBoundDistance(b0, axis.back().pos);
        nurbBounds[0].resize(rIdx + 1);
        b0.resize(rIdx + 1);

        auto [lp, ld, lIdx] = directionalBoundDistance(b1, axis.back().pos);
        nurbBounds[1].resize(lIdx + 1);
        b1.resize(lIdx + 1);
        
        //spawn<DLineMarker>(b0, Colors::azure);
        //spawn<DLineMarker>(b1, Colors::azure);
        return nurbBounds;
    }

    std::tuple<GVector2D, float, int> directionalBoundDistanceBacksort(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared = false)
    {
        if (bounds.empty())
            return { GVector2D(), -1.0f, -1 };

        float minD = std::numeric_limits<float>::max();
        std::vector<std::tuple<GVector2D, int>> nearest;

        for (int i = 0; i < int(bounds.size()) - 1; ++i)
        {
            auto [v1, d] = distance({ bounds[i], bounds[i + 1] }, p, true);
            if (d < minD)
            {
                minD = d;
                nearest = { {v1, i} };
            }
            else if (d == minD)
            {
                nearest.push_back({ v1, i });
            }
        }

        std::sort(nearest.begin(), nearest.end());
        std::ranges::reverse(nearest);

        return { std::get<GVector2D>(nearest.front()), returnSquared ? minD : sqrt(minD), std::get<int>(nearest.front()) };
    }

    std::vector<RiverRowInfo> TerrainMod<ETerrainMod::River>::createMainRiverbed(const QSharedPointer<DRiverMarker>& river, const std::vector<RiverNurbAxisPoint>& axis, const std::array<std::vector<RiverNurbBoundPoint>, 2>& bounds)
    {
        std::vector<QVector3D> rBound(bounds[0].size());
        std::vector<QVector3D> lBound(bounds[1].size());
        std::ranges::transform(bounds[0], rBound.begin(), [](auto&& bp) {return bp.pos; });
        std::ranges::transform(bounds[1], lBound.begin(), [](auto&& bp) {return bp.pos; });

        auto&& [lFloodBound, rFloodBound] = river->getRiverBounds();

        std::vector<Riverbed> riverSegmentBeds;
        Riverbed currentBed;
        currentBed.type = axis[0].type;

        auto expandRiverbed = [&](QVector3D rPoint, QVector3D lPoint, const RiverNurbAxisPoint& axisPoint)
        {
            // Ensure bounds stick to terrain where possible
                //auto predR = [&](auto&& a, auto&& b) { return std::abs(rPos.y() - a.y()) < std::abs(rPos.y() - b.y()); };
            auto rCandidates = Utils::castPointTo3D(rPoint/*, predR */);
            if (!rCandidates.empty() && rCandidates[0].y() < rPoint.y())
                rPoint.setY(rCandidates[0].y());

            //auto predL = [&](auto&& a, auto&& b) { return std::abs(lPos.y() - a.y()) < std::abs(lPos.y() - b.y()); };
            auto lCandidates = Utils::castPointTo3D(lPoint/*, predL*/);
            if (!lCandidates.empty() && lCandidates[0].y() < lPoint.y())
                lPoint.setY(lCandidates[0].y());

            float commonMin = std::min(rPoint.y(), lPoint.y());
            rPoint.setY(commonMin);
            lPoint.setY(commonMin);

            currentBed.rows <<= RiverSimpleRow(rPoint, lPoint, axisPoint);

            if (currentBed.type != axisPoint.type)
            {
                riverSegmentBeds << currentBed;
                currentBed.type = axisPoint.type;
                currentBed.rows = { currentBed.rows.back() };
            }
        };

        float lastRightIdx = -1.f;
        float lastLeftIdx = -1.f;
        for (int i = 0; i < axis.size(); ++i)
        {
            // Find own bounds.
            auto [rP, rD, rIdx] = directionalBoundDistance(rBound, axis[i].pos);
            float rightIdx = rIdx + distance(rBound[rIdx], rP) / distance(rBound[rIdx], rBound[rIdx + 1]);

            auto [lP, lD, lIdx] = directionalBoundDistance(lBound, axis[i].pos);
            float leftIdx = lIdx + distance(lBound[lIdx], lP) / distance(lBound[lIdx], lBound[lIdx + 1]);

            if (rightIdx > lastRightIdx && leftIdx > lastLeftIdx)
            {
                // Find flood bounds
                auto [rPF, rDF, rIdxF] = directionalBoundDistance(rFloodBound, GVector2D(axis[i].pos));
                auto [lPF, lDF, lIdxF] = directionalBoundDistance(lFloodBound, GVector2D(axis[i].pos));

                // Check for collision & use closer bounds
                if (rDF < rD)
                    if (auto [p1, p2, d] = line2LineDistance({ rPF, GVector2D(axis[i].pos) }, rBound); d < 1.0f)
                        rP = rPF;

                if (lDF < lD)
                    if (auto [p1, p2, d] = line2LineDistance({ lPF, GVector2D(axis[i].pos) }, lBound); d < 1.0f)
                        lP = lPF;

                // Create additional steps to stick to terrain
                if (currentBed.rows.empty())
                {
                    expandRiverbed(rP, lP, axis[i]);
                }
                else
                {
                    auto lastRow = currentBed.rows.back(); // intentional copy!
                    float outerDistance = std::max(distance(GVector2D(lastRow.rightBound), GVector2D(rP)), distance(GVector2D(lastRow.leftBound), GVector2D(lP)));
                    int steps = int(std::ceilf(outerDistance / gMinTriangleSideLength));
                    for (int step = 1; step <= steps; ++step)
                    {
                        float f = std::clamp(float(step) / float(steps), 0.0f, 1.0f);
                        QVector3D midR = std::lerp(lastRow.rightBound, rP, f);
                        QVector3D midL = std::lerp(lastRow.leftBound, lP, f);
                        QVector3D midAxisPos = std::lerp(lastRow.riverPt, axis[i].pos, f);
                        float midAxisWidth = std::lerp(distance(GVector2D(lastRow.rightBound), GVector2D(lastRow.leftBound)), axis[i].width, f);
                        float midAxisDepth = std::lerp(lastRow.depth, axis[i].depth, f);

                        //if (step > 1)
                        //    spawn<DLineMarker>(midR, midL, Colors::cyan);

                        expandRiverbed(midR, midL, RiverNurbAxisPoint{ midAxisPos, midAxisWidth, midAxisDepth, currentBed.type });
                    }
                }

                lastRightIdx = rightIdx;
                lastLeftIdx = leftIdx;
            }
        }

        // Register last segment
        riverSegmentBeds << currentBed;

        // Build final riverbed
        std::vector<RiverRowInfo> result;

        std::vector<std::vector<QVector3D>> firstSegmentRows(riverSegmentBeds.size());

        // First rows
        for (int i = 0; i < riverSegmentBeds.size(); ++i)
        {
            riverSegmentBeds[i].initialize();
            firstSegmentRows[i] = riverSegmentBeds[i].generateFirstRow();
        }

        // Final meshes
        for (int i = 0; i < riverSegmentBeds.size(); ++i)
        {
            // Remove row being duplicated
            if (!result.empty())
                result.pop_back();

            result << riverSegmentBeds[i].generateMesh((i < riverSegmentBeds.size() - 1) ? &firstSegmentRows[i + 1] : nullptr);
        }

        return result;
    }

    void TerrainMod<ETerrainMod::River>::submitAll(ModAlterationsList* mal) const
    {
        if (nurb.isNull())
            return;

		auto&& clusterMap = Data::get()->getTerrainClustersMap();
        QSet<QSharedPointer<TerrainBlockClusterBase>> clusters;
        for (int blockId : getArea())
            clusters << clusterMap[blockId];

        auto&& dem = Data::get()->getDEM();
        QVector3D dir = dem->heightData.sampleNormal(nurb->getOrigin());

        for (auto&& cluster : clusters)
        {
            OmniProfile("River submissions");
            auto verts = cluster->section->getVertices();

            auto submitVertexAlteration = [&](IndexType vIdx, float height)
            {
                TerrainMeshVertex prop = verts[vIdx];

                // Sculpt riverbed
                prop.position.setY(height);
                // Extremely wet inside the river
                prop.humidity = 1.0f;
                // Riverbed is pure rock textures
                prop.biomeTexWeights = 0;
                // Disable vegetation
                setPackParam(&prop.packParams, 1, 0.0f);

                (*mal)[cluster->keyCell][vIdx] << prop;
            };

            // Fully contained vertex adjustments
            std::vector<std::optional<float>> vertexHeights(verts.size());
            for (IndexType i = 0; i < verts.size(); ++i)
                if (nurb->getPolygon().contains(verts[i].position))
                    if (auto maybeH = nurb->sampleHeight(verts[i].position); maybeH)
                    {
                        vertexHeights[i] = *maybeH;
                        submitVertexAlteration(i, *vertexHeights[i]);
                    }

            // Adjust vertices belonging to affected triangles
            auto&& tris = cluster->section->getIndices();
            IndexType vertexOffset = cluster->section->getVertexBufferOffset();
            for (IndexType i = 0; i < tris.size(); i += 3)
            {
                IndexType i0 = tris[i + 0];
                IndexType i1 = tris[i + 1];
                IndexType i2 = tris[i + 2];

                int insideCount = bool(vertexHeights[i0 - vertexOffset]) + bool(vertexHeights[i1- vertexOffset]) + bool(vertexHeights[i2 - vertexOffset]);
                if (!insideCount || insideCount == 3)
                    continue;

                float outsideHeight = 0.0f;
                for (int off = 0; off < 3; ++off)
                    if (IndexType vIdx = tris[i + off] - vertexOffset; vertexHeights[vIdx])
                        outsideHeight += *vertexHeights[vIdx];

                outsideHeight /= float(insideCount);
                for (int off = 0; off < 3; ++off)
                    if (IndexType vIdx = tris[i + off] - vertexOffset; vertexHeights[vIdx])
                        submitVertexAlteration(vIdx, outsideHeight);
            }
        }

        auto&& cells = Data::get()->getTerrainCells()->getCells();

        auto pairBounds = [&cells](auto&& modifiedBounds, auto&& pairedBounds)
        {
            for (auto&& mBound : modifiedBounds)
            {
                float minD = std::numeric_limits<float>::max();
                QSharedPointer<EnvBound> pairedBound;

                for (auto&& pBound : pairedBounds)
                {
                    auto [v1, d, idx] = directionalBoundDistance(pBound->line, mBound->line[mBound->line.size() / 2], true);
                    if (d < minD)
                    {
                        minD = d;
                        pairedBound = pBound;
                    }
                }

                if (pairedBound)
                {
                    if (!containsIf(mBound->pairedBounds, [&](auto ptr) {return pairedBound.data() == ptr.data(); }))
                        mBound->pairedBounds << pairedBound;
                    if (!containsIf(pairedBound->pairedBounds, [&](auto ptr) {return mBound.data() == ptr.data(); }))
                        pairedBound->pairedBounds << mBound;

                    //Data::get()->createMarker<DLineMarker>(mBound->line[mBound->line.size() / 2], pairedBound->line[pairedBound->line.size() / 2], QVector4D(0.5, 1, 0, 1));
                }

                Data::get()->addEnviroBound(mBound);

                //Data::get()->createMarker<DLineMarker>(mBound->line);
                //Data::get()->createMarker<DLineMarker>(mBound->line.front(), mBound->line.back(), QVector4D(0.5,1,0,1));
                //Data::get()->createMarker<DLineMarker>(cells[mBound->cellIdx]->getCenter(), mBound->line.front(), QVector4D(1,0,0,1));
            }
        };

        auto [rBounds, lBounds] = computeRawEnvBounds();

        pairBounds(rBounds, lBounds);
        pairBounds(lBounds, rBounds);
    }

    QVector4D TerrainMod<ETerrainMod::River>::getDebugColor() const
    {
        static auto color = QVector4D(0, 0.3, 0.7, 1);
        return color;
    }

    std::tuple<QVector3D, float, int> boundDistanceBacksort(const std::vector<QVector3D>& bounds, const QVector3D& p, bool returnSquared /*= false*/)
    {
        float minD = std::numeric_limits<float>::max();
        int endI = -1;
        QVector3D closest;

        for (int i=1; i<bounds.size(); ++i)
        {
            auto [v1, v2, d] = distance(std::array{ bounds[i], bounds[i - 1] }, { p, p});
            if (d <= minD)
            {
                minD = d;
                endI = i;
                closest = v1;
            }
        }

        return { closest, minD, endI };
    }

    std::vector<RiverArc> TerrainMod<ETerrainMod::River>::detectArcs(const QSharedPointer<DRiverMarker>& river)
    {
        auto&& bounds = river->getRiverBounds();
        auto&& riverPts = river->getControlPoints();

        // Divide segment into arcs
        std::vector<RiverArc> arcs;
        RiverArc currentArc = { 0, 0 };

        GVector2D lastDir;
        for (int i = 0; i < riverPts.size() - 1; ++i)
        {
            GVector2D currentDir = (riverPts[i + 1] - riverPts[i]).normalized();
            if (lastDir.isNull())
            {
                // River source, go straight
                lastDir = currentDir;
                continue;
            }

            float angle = angle360(lastDir, currentDir);
            switch (currentArc.side)
            {
            case ERiverSide::None:
                // River hasn't turned yet
                currentArc.range[1] = i;
                if (angle < 180.0f)
                    currentArc.side = ERiverSide::Left;
                else if (angle > 180.0f)
                    currentArc.side = ERiverSide::Right;
                break;
            case ERiverSide::Left:
                if (angle < 180.0f)
                {
                    currentArc.range[1] = i;
                }
                else
                {
                    // End of arc
                    if (currentArc.range[1] != currentArc.range[0])
                        arcs <<= currentArc;

                    currentArc = { {i-1, i}, ERiverSide::Right };
                }
                break;
            case ERiverSide::Right:
                if (angle > 180.0f)
                {
                    currentArc.range[1] = i;
                }
                else
                {
                    // End of arc
                    if (currentArc.range[1] != currentArc.range[0])
                        arcs <<= currentArc;

                    currentArc = { {i-1, i}, ERiverSide::Left };
                }
                break;
            }

            lastDir = currentDir;
        }

        // Save last arc which most likely has not ended.
        currentArc.range[1] = riverPts.size() - 1;
        if (currentArc.range[1] != currentArc.range[0])
            arcs <<= currentArc;
        else
            arcs.back().range[1] = currentArc.range[1];

        //for (auto&& [arange, aside] : arcs)
        //    spawn<DLineMarker>(riverPts[arange[0]], riverPts[arange[1]], aside == ESide::Left ? Colors::green : Colors::red, 100);

        return arcs;
    }
}

void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::River>& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << static_cast<const Generation::TerrainModBase&>(object);
	omniBin << object.riverGuid;
	omniBin << object.parentModGuid;
    omniBin << object.nurbGuid;
    omniBin << object.name;
}

void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::River>& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> static_cast<Generation::TerrainModBase&>(object);
	omniBin >> object.riverGuid;
	omniBin >> object.parentModGuid;
    omniBin >> object.nurbGuid;
    omniBin >> object.name;
}
