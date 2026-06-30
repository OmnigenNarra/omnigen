#include "stdafx.h"
#include "TerrainBlockPrecipice.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Utils/Geom3DUtils.h"
#include "Utils/Interpolation.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include <random>
#include <Editor/Sections/Profiler/OmnigenProfiler.h>
#include "Utils/Triangulation/Triangulation.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include <atomic>
#include <noise/noise.h>
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

#define DEBUG_PRECIPICE 0


namespace Generation
{
    using PrecipiceCluster = TerrainBlockCluster<ETerrainBlock::Precipice>;

    // Block ===================================================================================================================================================

    float TerrainBlockCluster<ETerrainBlock::Precipice>::chance(const BlockChanceData& data)
    {
        // temporary disabled
        return 0.f;

        if (data.steepness >= 2.f)
            return 1.2f * data.steepness;
        else if (data.steepness >= 1.f)
            return 0.1f * data.steepness;

        return 0.f;
    }

    // Helper Functions ===================================================================================================================================================

    static QVector3D getIntersectionBetweenPlaneAndVerticalLine(const QVector3D& plane_pt, const QVector3D& plane_normal, const GVector2D& vertical_line_pt)
    {
        if (isZero(plane_normal.y()))
            return vertical_line_pt; //  this is incorrect, but this case of vertical plane must not apeared

        const float D = - (plane_normal.x() * plane_pt.x() + plane_normal.y() * plane_pt.y() + plane_normal.z() * plane_pt.z());
        const float y = - (D + plane_normal.z() * vertical_line_pt.z + plane_normal.x() * vertical_line_pt.x) / plane_normal.y();
        return QVector3D{ vertical_line_pt.x, y, vertical_line_pt.z};
    }


    static GVector2D getNormalVector(const GVector2D& vec, const GVector2D& point_on_line, const GVector2D& point_on_half_space)
    {
        const GVector2D res = vec.rotatedLeft90().normalized();
        if (GVector2D::dotProduct(point_on_half_space - point_on_line, res) < 0.f)
            return -res;
        return res;
    }


    static std::vector<QVector3D> split3dBorder(const Segment3D& seg, FFirstLastPolicy inclusionPolicy)
    {
        const int count = getMeshSegments(distance((GVector2D)seg.first, (GVector2D)seg.second));
        std::vector<QVector3D> res = splitSegment<QVector3D>(seg, inclusionPolicy, true, count);
        return res;
    }


    static float getRandomValue(float mean, float deviation, float min, float max)
    {
        std::normal_distribution<> normalDistr{mean, deviation};
        return std::clamp((float)normalDistr(Generation::gRandomEngine), min, max);
    }


    static GVector2D getRandomOffsetInDirection(const GVector2D& direction, float mean, float deviation, float min, float max)
    {
        return direction * getRandomValue(mean, deviation, min, max);
    }


    static float getNoise(float x, float z)
    {
        static noise::module::RidgedMulti noiseSource;
        static noise::model::Plane noiseModel;
        static std::atomic_bool isInited = false;

        if (!isInited)
        {
            static std::mutex guard;
            std::scoped_lock lock(guard);
            noiseSource.SetSeed(Generation::gRandomEngine());
            noiseSource.SetFrequency(0.02f);
            noiseSource.SetOctaveCount(4);
            noiseModel.SetModule(noiseSource);
            isInited = true;
        }

        return noiseModel.GetValue(x, z);
    }


    static void addVerticalNoiseToPoint(QVector3D& pt, float factor)
    {
        const GVector2D pc = { pt.x() * 0.1f, pt.z() * 0.1f };
        float dh = getNoise(pc.x, pc.z) * factor;
        pt.setY(pt.y() + dh);
    }


    // inflate concave angles and trying to morph polygon to become convex
    static Polygon2D smoothConcavePolygon(const Polygon2D& polygon, const std::unordered_set<int>& vertices_to_ignore, float factor = 0.2f)
    {
        if (!polygon.isConcavePolygon())
            return polygon;

        const auto& cpts = polygon.getCPts();
        std::vector<GVector2D> res;
        res.resize(cpts.getSize());

        Segment2D goalEdge;

        for (int i = 0; i < cpts.getSize(); ++i)
        {
            if (polygon.isConcaveVertex(i) && !vertices_to_ignore.contains(i))
            {
                // search for next convex edge to create goal edge
                int endIndex = cpts.findIdx(i, 1);
                for (endIndex; !polygon.isConvexVertex(endIndex) && !vertices_to_ignore.contains(endIndex); endIndex = cpts.findIdx(endIndex, 1)) {}
                int startIndex = cpts.findIdx(i, -1);
                for (startIndex; !polygon.isConvexVertex(startIndex) && !vertices_to_ignore.contains(startIndex); startIndex = cpts.findIdx(startIndex, -1)) {}

                goalEdge.first  = cpts[startIndex];
                goalEdge.second = cpts[endIndex];

                const float partsCount = cpts.distCW(startIndex, endIndex);
                Q_ASSERT(partsCount > 1);

                int partCounter = 1;
                for (int j = cpts.findIdx(startIndex, 1); j != endIndex; j = cpts.findIdx(j, 1))
                {
                    const GVector2D currentGoal = std::lerp(goalEdge.first, goalEdge.second, (float)partCounter / partsCount);
                    res[j] = std::lerp(cpts[j], currentGoal, factor);
                    ++partCounter;
                }

                if (endIndex < i) // circle iteration crossed last index
                    break;
                i = endIndex - 1;
            }
            else // convex or collinear
            {
                res[i] = cpts[i];
            }
        }

        return Polygon2D(std::move(res));
    }


    static float getDemHeight(const GVector2D& pt)
    {
        return Data::get()->getDEM()->heightData.sample(pt);
    }


    static QVector3D getPoint3D(const GVector2D& pt)
    {
        return QVector3D(pt.x, getDemHeight(pt), pt.z);
    }


    static int getMinHeightIndex(const Polygon2D& polygon)
    {
        const auto& pts = polygon.getPts();
        Q_ASSERT(!pts.empty());

        int minHeightIndex = 0;
        float minHeight = std::numeric_limits<float>::max();

        for (int i = 0; i < pts.size(); ++i)
        {
            const float currHeight = getDemHeight(pts[i]);
            if (currHeight < minHeight)
            {
                minHeight = currHeight;
                minHeightIndex = i;
            }
        }

        return minHeightIndex;
    }


    // Cluster Data ==============================================================================================================================================

    ClusterData<ETerrainBlock::Precipice>::ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Precipice>* metaCluster, int id)
        : ClusterDataBase(id)
    {
    }

    // Cluster ===================================================================================================================================================

    std::optional<std::tuple<Polygon2D, Polygon2D, int, int, bool>>
        TerrainBlockCluster<ETerrainBlock::Precipice>::dividePolygonIntoTopAndBottom(const Polygon2D& polygon, const std::vector<float>& heights, int minHeightIndex, float heightThreshold)
    {
        int edgeIndex[2] = {0, 0};

        const auto&& cpts = polygon.getCPts();
        Q_ASSERT(cpts.getSize() > 0);
        Q_ASSERT(cpts.getSize() == heights.size());

        cpts.forRangeLonger(minHeightIndex, cpts.findIdx(minHeightIndex, -1),
            [&heights, &edgeIndex, heightThreshold](int j)
        {
            if (heights[j] >= heightThreshold)
            {
                edgeIndex[0] = j;
                return;
            }
        });

        cpts.forRangeLonger(minHeightIndex, cpts.findIdx(minHeightIndex, 1),
            [&heights, &edgeIndex, heightThreshold](int j)
        {
            if (heights[j] >= heightThreshold)
            {
                edgeIndex[1] = j;
                return;
            }
        });

        if (polygon.isDiagonalIntersected(edgeIndex[0], edgeIndex[1]))
            return std::nullopt;

        bool isDirClockwise = false;
        cpts.forRangeCW(edgeIndex[0], edgeIndex[1], [&isDirClockwise, minHeightIndex](int j) mutable
        {
            if (j == minHeightIndex)
            {
                isDirClockwise = true;
                return;
            }
        });

        const int sizeBottom = cpts.dist(edgeIndex[0], edgeIndex[1], isDirClockwise)  + 1;
        const int sizeTop    = cpts.dist(edgeIndex[0], edgeIndex[1], !isDirClockwise) + 1;

        std::vector<GVector2D> topPts;
        std::vector<GVector2D> bottomPts;

        topPts.reserve(sizeTop);
        bottomPts.reserve(sizeBottom);

        const auto fillTopPolygon    = [&cpts, &topPts]   (int j){ topPts    << cpts[j]; };
        const auto fillBottomPolygon = [&cpts, &bottomPts](int j){ bottomPts << cpts[j]; };

        cpts.forRange(edgeIndex[0], edgeIndex[1], isDirClockwise,  fillBottomPolygon);
        cpts.forRange(edgeIndex[0], edgeIndex[1], !isDirClockwise, fillTopPolygon);

        return {{ Polygon2D(std::move(bottomPts)), Polygon2D(std::move(topPts)), edgeIndex[0], edgeIndex[1], isDirClockwise }};
    }


    // check for very small angles in polygon
    static bool isPolygonAcceptable(const Polygon2D& polygon)
    {
        const auto cPts = polygon.getCPts();
        std::vector<GVector2D> edges(polygon.getPts().size());
        for (int i = 0; i < cPts.getSize(); ++i)
        {
            const GVector2D& curr = cPts[i];
            const GVector2D& next = cPts.getNext(i);
            edges[i] = (next - curr).normalized();
        }

        constexpr float cosAngleThreshold = 0.86f; // min angle 30 degrees

        const auto cEdges = asCircular(edges);
        for (int i = 0; i < cPts.getSize(); ++i)
        {
            const GVector2D& currVec = -cEdges[i];
            const GVector2D& nextVec = cEdges.get(i, 1);
            const float currentAngleCos = GVector2D::dotProduct(currVec, nextVec);
            if (currentAngleCos > cosAngleThreshold)
                return false;
        }

        return true;
    }


    std::optional<std::vector<Polygon2D>> TerrainBlockCluster<ETerrainBlock::Precipice>::cutBottom(const Polygon2D& polygon, float minHeightThreshold)
    {
        const auto&& cpts = polygon.getCPts();
        Q_ASSERT(cpts.getSize() > 0);

        std::vector<Polygon2D> result;
        result.reserve(3);
        result.resize(1); // reserved for main polygon

        std::vector<GVector2D> main;
        std::vector<GVector2D> remaining;
        main.reserve(cpts.getSize());
        remaining.reserve(cpts.getSize());

        bool isBelow = false;
        int edgeIndex[2] = {0, 0};

        for (int i = 0; i < cpts.getSize(); ++i)
        {
            const GVector2D& currPt = cpts[i];
            const float currentHeight = getDemHeight(currPt);
            const bool isCurrentPointBelow = currentHeight <= minHeightThreshold;
            if (isBelow)
            {
                if (isCurrentPointBelow)
                {
                    remaining << currPt;
                }
                else
                {
                    if (remaining.size() >= 2)
                    {
                        edgeIndex[1] = i;
                        remaining << currPt;

                        // diagonal can have self-intersections
                        Polygon2D remainingPolygon = Polygon2D(std::move(remaining));
                        if (remainingPolygon.isDegeneratedPolygon())
                        {
                            // return points back to main polygon
                            main.insert(main.end(), remainingPolygon.getPts().begin() + 1, remainingPolygon.getPts().end());
                        }
                        else if (polygon.isDiagonalIntersected(edgeIndex[0], edgeIndex[1]))
                        {
                            return std::nullopt; // main polygon is divided, it is not acceptable
                        }
                        else
                        {
                            result <<= std::move(remainingPolygon);
                            main << splitSegment(Segment2D{main.back(), cpts[edgeIndex[1]]}, FFirstLastPolicy::Last, true);
                        }
                    }
                    remaining.clear();
                }
            }
            else
            {
                if (isCurrentPointBelow)
                {
                    edgeIndex[0] = i - 1;
                    remaining << cpts[i - 1];
                    remaining << currPt;
                }
                else
                {
                    main << currPt;
                }
            }
            isBelow = isCurrentPointBelow;
        }

        result.front().setPoints(std::move(main));
        if (result.front().isDegeneratedPolygon() || !isPolygonAcceptable(result.front())) // main polygon must be correct (concave or convex, but not degenerated) and has no too small angles
            return std::nullopt;

#if false
        // validation of result
        for (int i = 0; i < polygon.getPts().size(); ++i)
        {
            const auto& pt = polygon[i];
            bool contains = false;
            for (const auto& resP: result)
            {
                Q_ASSERT(resP.getPts().size() >= 3);
                for (const auto& ppt: resP)
                {
                    if (ppt == pt)
                    {
                        contains = true;
                        break;
                    }
                }
                if (contains)
                    break;
            }
            Q_ASSERT_X(contains, "cutBottom error", "point is not in any result polygon");
        }
#endif

        return {result};
    }


    // void TerrainBlockCluster<ETerrainBlock::Precipice>::initialize()
    // {
        // // Prepare data for frame
        // auto&& dem = Data::get()->getDEM();

        // auto&& polygon = getSegmentedPolygon(boundingAreaPolygon);

        // const auto&& cpts = polygon.getCPts();
        // Q_ASSERT(cpts->size() > 0);

        // // Define heights of all polygon points and select minimum and maximum --------------------------------------------------------------------------------------

        // int maxHeightIndex = 0;
        // int minHeightIndex = 0;
        // std::vector<float> tempHeights(cpts->size());
        // tempHeights.resize(cpts->size());

        // for (int i = 0; i < tempHeights.size(); ++i)
        // {
        //     tempHeights[i] = dem->heightData.sample(cpts[i]);
        //     if (tempHeights[i] > maxHeight)
        //     {
        //         maxHeight = tempHeights[i];
        //         maxHeightIndex = i;
        //     }
        //     else if (tempHeights[i] < minHeight)
        //     {
        //         minHeight = tempHeights[i];
        //         minHeightIndex = i;
        //     }
        // }

        // // Divide polygon into two parts - upper and bottom and get division diagonal -------------------------------------------------------------------------------
        // // Idea is to get division edge as high, as possible, but this edge must not intersects with polygon edges
        // // So, if it not possible for current height, we decrease height a little bit and trying one more time

        // int edgeIndex[2] = {0, 0};
        // bool isDirClockwise = false;

        // float heightFactor = 0.95f;
        // float heightThreshold = 0.f;
        // while (true)
        // {
        //     heightThreshold = minHeight + (maxHeight - minHeight) * heightFactor;
        //     auto result = dividePolygonIntoTopAndBottom(polygon, tempHeights, minHeightIndex, heightThreshold);
        //     const int minimumVertexCount = 5;
        //     if (!result || std::get<0>(*result).getPts().size() < minimumVertexCount)
        //     {
        //         heightFactor -= 0.1f;
        //         if (heightFactor <= 0.f)
        //         {
        //             needFallback = true;
        //             return;
        //             // Q_ASSERT_X(false, "Precipice block", "Incorrect cluster polygon");
        //             // break;
        //         }
        //         continue;
        //     }

        //     bottomPolygon = std::move(std::get<0>(*result));
        //     topPolygon = std::move(std::get<1>(*result));
        //     edgeIndex[0] = std::get<2>(*result);
        //     edgeIndex[1] = std::get<3>(*result);
        //     isDirClockwise = std::get<4>(*result);
        //     break;
        // }

        // // maxHeight now will be min height of edge points
        // maxHeight = std::min(tempHeights[edgeIndex[0]], tempHeights[edgeIndex[1]]);
        // const float maxDistanceOfClusterPolygon = getMaximumDistFromEdge(bottomPolygon);

        // // Cut bottom part, which is below bottomHeightThreshold -------------------------------------------------------------------------------------------------------

        // const float deltaH = maxHeight - minHeight;
        // float bottomHeightThresholdFactor = getRandomValue(0.2f, 1.f, 0.05f, 0.6f);

        // while (true)
        // {
        //     const float heightThreshold = minHeight + deltaH * bottomHeightThresholdFactor;
        //     auto polygons = cutBottom(bottomPolygon, heightThreshold);
        //     if (!polygons)
        //     {
        //         bottomHeightThresholdFactor -= 0.1f;
        //         if (bottomHeightThresholdFactor <= 0.f)
        //             break;
        //         continue;
        //     }

        //     bottomPolygon = std::move((*polygons).front());
        //     if ((*polygons).size() > 1)
        //     {
        //         remainingPolygons.reserve(remainingPolygons.size() + (*polygons).size() - 1);
        //         remainingPolygons.insert(remainingPolygons.end(), ++(*polygons).begin(), (*polygons).end());
        //     }
        //     minHeight = heightThreshold;
        //     break;
        // }

        // // Calculate max distance from 3d vertices to edge -------------------------------------------------------------------------------------------------------------
        // const GVector2D& v1 = bottomPolygon.getPts().front();
        // const GVector2D& v2 = bottomPolygon.getPts().back();
        // edgeSegment.first  = QVector3D(v1.x, tempHeights[edgeIndex[0]], v1.z);
        // edgeSegment.second = QVector3D(v2.x, tempHeights[edgeIndex[1]], v2.z);

        // maxDistToLine = getMaximumDistFromEdge(bottomPolygon);
        // maxOverhangFactor = maxDistanceOfClusterPolygon / maxDistToLine - 1.f;

        // // initialize normal -------------------------------------------------------------------------------------------------------------------------------------------
        // normalVecToEdge2D = getNormalVector(GVector2D(edgeSegment.second - edgeSegment.first), GVector2D(edgeSegment.first), polygon[minHeightIndex]);
    // }


    static int iteratePointInDirection(GVector2D& pt, const GVector2D& direction, float step, float distanceToBounds, const Polygon2D& areaPolygon, int iterationsLimit)
    {
        const GVector2D start = pt;
        constexpr float stepDeviation = 0.42f;
        int iterationsCount = 0;

        while (true)
        {
            const GVector2D temp = pt + direction * step * getRandomFloat(1.f - stepDeviation, 1.f + stepDeviation);
            if (areaPolygon.contains(temp) && !areaPolygon.intersects({start, pt}, true) && areaPolygon.getRadiusOfInscribedCircleAtPoint(temp) >= distanceToBounds)
            {
                pt = temp;
                ++iterationsCount;
                if (iterationsCount >= iterationsLimit)
                    break;
                continue;
            }
            break;
        }

        return iterationsCount;
    }

    // left point, right point, left iterations, right iterations
    static std::tuple<GVector2D, GVector2D, int, int> getBoundingPoints(const GVector2D& start, const GVector2D& direction, float step, float distanceToBounds, const Polygon2D& areaPolygon, int iterationsLimit)
    {
        GVector2D left = start;
        GVector2D right = start;
        const int leftIterationsCount  = iteratePointInDirection(left,   direction, step, distanceToBounds, areaPolygon, iterationsLimit);
        const int rightIterationsCount = iteratePointInDirection(right, -direction, step, distanceToBounds, areaPolygon, iterationsLimit);
        return { left, right, leftIterationsCount, rightIterationsCount };
    }


    float PrecipiceCluster::getOffset(int frameLevelIndex) const
    {
        return frameLevelIndex * 0.1f;
    }


    void PrecipiceCluster::init()
    {
        auto&& dem = Data::get()->getDEM();

        const int minIndex = getMinHeightIndex(boundingAreaPolygon);
        const GVector2D bottomPt = boundingAreaPolygon[minIndex];
        const QVector3D bPt3D = { bottomPt.x, dem->heightData.sample(bottomPt), bottomPt.z };
        const GVector2D gradientVec = dem->heightData.sampleGradient(bottomPt);

        const GVector2D testPt = bottomPt + gradientVec * 250.f;
        const QVector3D testPt3D = { testPt.x, dem->heightData.sample(testPt), testPt.z };

        spawn<DLineMarker>(bPt3D, testPt3D, Colors::yellow, 0.f, ELineDecorator::Arrow);

        constexpr float stepTransverse = 60.f;
        constexpr float stepLongitudinal = 30.f;
        constexpr float radiusLimit = 70.f;
        GVector2D longitudinalDir = gradientVec.normalized();
        GVector2D transverseDir = longitudinalDir.rotatedLeft90();

        std::vector<GVector2D> leftPts;
        std::vector<GVector2D> rightPts;
        leftPts.reserve(20);
        rightPts.reserve(20);

        std::vector<std::vector<QVector3D>> frame;
        frame.reserve(20);

        vertices.reserve(200);
        frameIndices.reserve(20);

        GVector2D currCenter = bottomPt + longitudinalDir * stepLongitudinal * 2.f;
        if (!boundingAreaPolygon.contains(currCenter))
        {
            longitudinalDir = -boundingAreaPolygon.getNormal(minIndex);
            transverseDir = longitudinalDir.rotatedLeft90();
            currCenter = bottomPt + longitudinalDir * stepLongitudinal * 2.f;

            spawn<DLineMarker>(bPt3D, getPoint3D(currCenter), Colors::cyan, 0.f, ELineDecorator::Arrow);
        }

        int iterationsLimit = 5;
        float prevHeight = std::numeric_limits<float>::min();
        while (true)
        {
            if (!boundingAreaPolygon.contains(currCenter))
                break;

            // add new points
            const auto [left, right, leftIterations, rightIterations] = getBoundingPoints(currCenter, transverseDir, stepTransverse, radiusLimit, boundingAreaPolygon, iterationsLimit);
            if (leftIterations + rightIterations > 0)
            {
                const float currentHeight = (getDemHeight(left) + getDemHeight(right)) * 0.5f;
                if (currentHeight < prevHeight)
                    break;
                prevHeight = currentHeight;

                leftPts << left;
                rightPts << right;

                // add frame
                std::vector<QVector3D> left3d;
                std::vector<QVector3D> right3d;
                left3d.reserve(leftPts.size());
                right3d.reserve(rightPts.size());

                const float offset = getOffset(frameIndices.size());
                const QVector3D offset3d(offset, 0.f, offset);

                for (int i = 0; i < leftPts.size() - 1; ++i)
                {
                    const auto& pt = leftPts[i];
                    left3d <<= QVector3D(pt.x, currentHeight, pt.z) + offset3d;
                }
                left3d <<= getPoint3D(leftPts.back()) + offset3d;
                for (int i = 0; i < rightPts.size() - 1; ++i)
                {
                    const auto& pt = rightPts[i];
                    right3d <<= QVector3D(pt.x, currentHeight, pt.z) + offset3d;
                }
                right3d <<= getPoint3D(rightPts.back()) + offset3d;

                std::reverse(left3d.begin(), left3d.end());
                left3d << right3d;
                frame << left3d;

                const int startIndex = vertices.size();
                vertices << left3d;
                std::vector<int> currIndices(left3d.size());
                std::iota(currIndices.begin(), currIndices.end(), startIndex);
                frameIndices <<= std::move(currIndices);

                iterationsLimit = (leftIterations + rightIterations) / 2 + 3;
            }

            // next iteration
            currCenter = (left + right) * 0.5f;
            currCenter += longitudinalDir * stepLongitudinal;
            if (!leftPts.empty() && (boundingAreaPolygon.intersects({currCenter, leftPts.back()}, true) || boundingAreaPolygon.intersects({currCenter, rightPts.back()}, true)))
                break;
        }

        // remove offset from edge points
        const float topOffset = getOffset(frameIndices.size() - 1);
        const QVector3D topOffset3d(topOffset, 0.f, topOffset);
        vertices[frameIndices.back().front()] -= topOffset3d;
        vertices[frameIndices.back().back()] -= topOffset3d;

        // Debug draw frame -----------------------------------------------------------------
#if DEBUG_PRECIPICE

        std::vector<std::vector<QVector3D>> verticalLines;
        verticalLines.reserve(frame.size() * frame.back().size());

        for (int i = 0; i < frame.size() - 1; ++i)
        {
            const auto& bottom = frame[i];
            const auto& top = frame[i + 1];

            // vertical lines
            for (int j = 1; j < top.size() - 1; ++j)
                verticalLines <<= std::vector<QVector3D>{bottom[j - 1], top[j]};
        }

        spawn<DMultiLineMarker>(frame, Colors::springGreen);
        spawn<DMultiLineMarker>(verticalLines, Colors::green);
#endif

        // Bounding Polygon ------------------------------------------------------------------
        std::reverse(rightPts.begin(), rightPts.end());
        leftPts << rightPts;

        std::vector<QVector3D> testPolygon3D;
        testPolygon3D.reserve(leftPts.size());

        for (auto& pt: leftPts)
            testPolygon3D <<= getPoint3D(pt);

        precipiceBoundingPolygon = Polygon2D(std::move(leftPts));

        spawn<DLineMarker>(testPolygon3D, Colors::rose, true);
    }


    float TerrainBlockCluster<ETerrainBlock::Precipice>::distanceFromPointToEdge(const QVector3D& pt)
    {
        const auto [dist, _] = edgeSegment.distFromPointToInfiniteLine(pt);
        return dist;
    }


    float TerrainBlockCluster<ETerrainBlock::Precipice>::getMaximumDistFromEdge(const Polygon2D& polygon)
    {
        float maxDistSq= 0.f;
        for (const auto& pt: polygon)
        {
            const QVector3D currPt(pt.x, getDemHeight(pt), pt.z);
            auto [distSq, _] = edgeSegment.distFromPointToInfiniteLine(currPt, true);
            if (distSq > maxDistSq)
                maxDistSq = distSq;
        }

        Q_ASSERT(!isZero(maxDistSq));
        return sqrtf(maxDistSq);
    }


    void TerrainBlockCluster<ETerrainBlock::Precipice>::generate()
    {
        OmniProfile("Precipice cluster geometry");

        computeBorderPoints();
        boundingAreaPolygon = Utils::makeBoundingPolygon(cells).front();
        initialize();

        // new approach - in debug mode
        init();

        // if (needFallback)
        // {
        //     MeshConnector meshConnector;
        //     const auto create3dPointFunc = [](const QVector3D& v) { return QVector3D(v.x(), Data::get()->getDEM()->heightData.sample((GVector2D)v), v.z()); };
        //     const auto [vertsP, indP, _] = meshPolygon(boundingAreaPolygon, true, false);
        //     meshConnector.addMesh(vertsP, indP, create3dPointFunc);
        //     meshConnector.indices.shrink_to_fit();
        //     GeometryData<TerrainMeshVertex> geometry;
        //     geometry.indices = std::move(meshConnector.indices);
        //     geometry.vertices.reserve(meshConnector.vertices.size());
        //     for (const auto& pt : meshConnector.vertices)
        //     {
        //         TerrainMeshVertex tmv{ pt, {}, *this };
        //         geometry.vertices <<= std::move(tmv);
        //     }
        // }
        // else
        section = generateMesh();
    }

    struct VerticalSurfacePoint
    {
        int index;
        GVector2D normalDirection;
    };

    static void applyOffsetToVerticalSurface(std::vector<std::vector<VerticalSurfacePoint>>& verticalSurface,
        std::vector<QVector3D>& vertices)
    {
        // Noise
        noise::module::Voronoi noiseSource;
        noise::model::Plane noiseModel;
        noiseSource.SetSeed(Generation::gRandomEngine());
        noiseSource.SetFrequency(getRandomFloat(0.3f, 2.f));
        noiseSource.SetDisplacement(getRandomFloat(1.f, 2.f));
        float noiseAmplitudeFactor = getRandomFloat(0.3f, 0.5f);
        noiseModel.SetModule(noiseSource);

        const float columnCount = (float)verticalSurface.size() - 1.f;

        for (int i = 0; i < verticalSurface.size() - 1; ++i)
        {
            auto& rowVec = verticalSurface[i];
            const float rowElementsCount = (float)rowVec.size() - 1.f;
            if (rowVec.size() <= 1)
                continue;
            const float y = i / 10.f;
            for (int j = 0; j < rowVec.size(); ++j)
            {
                const float x = j / 10.f;
                auto& vsPoint = rowVec[j];
                // apply offset
                const float noiseValue = noiseModel.GetValue(x, y) * noiseAmplitudeFactor;
                vertices[vsPoint.index] += (QVector3D)(vsPoint.normalDirection * noiseValue);
            }
        }
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> TerrainBlockCluster<ETerrainBlock::Precipice>::generateMesh()
    {
        // fallback in case of degenerate polygon
        if (precipiceBoundingPolygon.getPts().size() < 3)
        {
            auto [geom, outerCount] = meshPolygon2(boundingAreaPolygon.getPts());
            GeometryData<TerrainMeshVertex> geometry;
            auto& verts = geom.vertices;
            geometry.indices = std::move(geom.indices);
            geometry.vertices.reserve(verts.size());
            for (const auto& pt : verts)
            {
                TerrainMeshVertex tmv{ getPoint3D(pt), {}, *this };
                geometry.vertices <<= std::move(tmv);
            }
            return spawnBatched(std::move(geometry), makeBatchParams());
        }

        const std::vector<Polygon2D> surroundingPolygons = triangulatePolygonWithHole(boundingAreaPolygon, precipiceBoundingPolygon);

        MeshConnector meshConnector;
        const auto create3dPointFunc = [](const QVector3D& v) { return getPoint3D((GVector2D)v); };

        // Mesh surrounding polygons -------------------------------------------------------------------------------------------------
        for (const auto& sP: surroundingPolygons)
        {
            const auto [geom, unused] = meshPolygon2(sP.getPts());
            meshConnector.addMesh(geom.vertices, geom.indices, create3dPointFunc);
        }

        // Vertical surface ----------------------------------------------------------------------------------------------------------
        std::vector<IndexType> indices;
        indices.reserve(vertices.size() * 2);

        for (int i = 0; i < frameIndices.size() - 1; ++i)
        {
            const auto& bottomI = frameIndices[i];
            const auto& topI = frameIndices[i + 1];

            indices << topI.front() << bottomI.front() << topI[1];
            for (int j = 1; j < topI.size() - 2; ++j)
            {
                indices << topI[j] << bottomI[j - 1] << bottomI[j];
                indices << topI[j] << bottomI[j] << topI[j + 1];
            }
            indices << bottomI.back() << topI.back() << topI[topI.size() - 2];
        }

        meshConnector.addMesh3D(vertices, indices);

        // Top surface ---------------------------------------------------------------------------------------------------------------
        {
            std::vector<GVector2D> topPts;
            topPts.reserve(frameIndices.back().size());
            for (int index: frameIndices.back())
                topPts << (GVector2D)vertices[index];
            Polygon2D topPolygon(std::move(topPts));
            const int topHeight = vertices[frameIndices.back()[frameIndices.back().size() / 2]].y();
            const auto [geom, unused] = meshPolygon2(topPolygon.getPts());
            const auto top3dFunc = [topHeight](const QVector3D& v) {
                return QVector3D(v.x(), topHeight, v.z());
            };
            meshConnector.addMesh(geom.vertices, geom.indices, top3dFunc);
        }

        // Prepare results -----------------------------------------------------------------------------------------------------------
        meshConnector.indices.shrink_to_fit();
        GeometryData<TerrainMeshVertex> geometry;
        geometry.indices = std::move(meshConnector.indices);
        geometry.vertices.reserve(meshConnector.vertices.size());
        for (const auto& pt : meshConnector.vertices)
        {
            TerrainMeshVertex tmv{ pt, {}, *this };
            geometry.vertices <<= std::move(tmv);
        }

        return spawnBatched(std::move(geometry), makeBatchParams());
    }


    // MetaCluster ========================================================================================================

    void TerrainBlockMetaCluster<ETerrainBlock::Precipice>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        TerrainBlockMetaClusterBase::computePackParams(lithoCluster, biomeDomain, averageIHLevel);
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Precipice>::spawnClusters()
    {
        spawnBigClusters();
    }


} // namespace Generation



void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Precipice>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Precipice>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
}


