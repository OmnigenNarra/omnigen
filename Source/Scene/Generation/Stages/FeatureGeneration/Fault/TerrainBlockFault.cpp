#include "stdafx.h"

#include "TerrainBlockFault.h"
#include "Omnigen.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/CircularVectorView.h"
#include "Utils/Interpolation.h"
#include <Editor/Sections/Profiler/OmnigenProfiler.h>
#include <noise/noise.h>
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#define DEBUG_FAULT_LINES 0

namespace Generation
{
    using FaultCluster = TerrainBlockCluster<ETerrainBlock::Fault>;

    float FaultCluster::chance(const BlockChanceData& data)
    {
        if (data.steepness >= 0.1f && data.steepness <= 0.4f)
            return 0.75f - std::abs(data.steepness - 0.2f);

        return 0.0f;
    }

    void FaultCluster::meshCluster(MeshConnector* meshConnector, std::unordered_set<GVector2D>* verticalSectionVertices)
    {
        static auto rotateRight90 = QQuaternion::fromEulerAngles(0, -90, 0);

        Polygon2D detailedArea(clusterBorderPoints);

       // Flat areas
        auto [bottom, bottomLeftovers] = splitPolygonByFittedMultiLine(detailedArea, faultSegments.front());
        auto [topLeftovers, top] = splitPolygonByFittedMultiLine(detailedArea, faultSegments.back());

        GeometryData<GVector2D> botGeom;
        auto& botVertices = botGeom.vertices;
        auto& botIndices = botGeom.indices;
        IndexType botOuterCount;

        GeometryData<GVector2D> topGeom;
        auto& topVertices = topGeom.vertices;
        auto& topIndices = topGeom.indices;
        IndexType topOuterCount;

        if (bottom.getPts().size() > 2)
            std::tie(botGeom, botOuterCount) = meshPolygon2(bottom.getPts());

        if (top.getPts().size() > 2)
            std::tie(topGeom, topOuterCount) = meshPolygon2(top.getPts());

        std::vector<QVector3D> generatedPoints;
        std::vector<IndexType> generatedTriangles;
        generatedPoints.reserve(botVertices.size() + topVertices.size());
        generatedTriangles.reserve(botIndices.size() + topIndices.size());

        for (int i = 0; i < botVertices.size(); ++i)
        {
            auto&& p = botVertices[i];
            generatedPoints << QVector3D{ p.x, botH, p.z };
        }
        for (int i = 0; i < topVertices.size(); ++i)
        {
            auto&& p = topVertices[i];
            generatedPoints << QVector3D{ p.x, topH, p.z };
        }

        generatedTriangles << botIndices;
        for (int i : topIndices)
            generatedTriangles << botVertices.size() + i;

        std::array<std::array<IndexType, 2>, 2> outers;
        outers[0][0] = 0;
        outers[0][1] = botOuterCount;
        outers[1][0] = botVertices.size();
        outers[1][1] = botVertices.size() + topOuterCount;

        // Vertical segments
        if (botVertices.size() > 0 && topVertices.size() > 0)
            meshVerticalSection(faultSegments, outers, &generatedPoints, &generatedTriangles, verticalSectionVertices);

        // Enviro bounds
        auto lower = QSharedPointer<EnvBound>::create(keyCell);
        auto upper = QSharedPointer<EnvBound>::create(keyCell);
        lower->line = { faultSegments.front().begin(), faultSegments.front().end() };
        upper->line = { faultSegments.back().begin(), faultSegments.back().end() };
        for (int i = 0; i < lower->line.size(); ++i)
        {
            lower->line[i].setY(botH);
            upper->line[i].setY(topH);
        }

        lower->pairedBounds << upper;
        upper->pairedBounds << lower;
        upper->flipDir = true;

        auto frontSide = QSharedPointer<EnvBound>::create(keyCell);
        auto backSide = QSharedPointer<EnvBound>::create(keyCell);
        frontSide->line = { *upper->line.begin(), *lower->line.begin() };
        backSide->line = { *lower->line.rbegin(), *upper->line.rbegin() };

        Data::get()->addEnviroBound(lower);
        Data::get()->addEnviroBound(upper);
        Data::get()->addEnviroBound(frontSide);
        Data::get()->addEnviroBound(backSide);

        meshConnector->addMesh3D(generatedPoints, generatedTriangles);
    }

    std::optional<std::pair<IndexType, IndexType>> findIndexesAfterDistance(const std::vector<GVector2D> line, float desiredDistance)
    {
        IndexType start = 0;
        IndexType end = line.size() - 1;
        float currentDistance = 0;

        while (true)
        {
            if (currentDistance >= desiredDistance)
                break;
            else if (start > end)
                return std::nullopt;

            start++;
            currentDistance += line[start].dist(line[start - 1]);
        }
        currentDistance = 0;
        while (true)
        {
            if (currentDistance >= desiredDistance)
                break;
            else if (end < 0)
                return std::nullopt;

            end--;
            currentDistance += line[end].dist(line[end + 1]);
        }

        return std::pair(start, end);
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> FaultCluster::generateMesh()
    {
        if (faultSegments.empty())
        {
            auto&& dem = Data::get()->getDEM();
            auto [geom2D, unused] = meshPolygon2(calculatePolygon().getPts());
            auto& verts = geom2D.vertices;
            auto& indices = geom2D.indices;

            GeometryData<TerrainMeshVertex> geometry;
            geometry.vertices.reserve(verts.size());
            for (auto&& vert : verts)
                geometry.vertices.push_back({ {vert.x, dem->heightData.sample(vert), vert.z}, {}, *this });
            geometry.indices = std::move(indices);
            return spawnBatched(std::move(geometry), makeBatchParams());
        }

        MeshConnector meshConnector;
        std::unordered_set<GVector2D> verticalSectionVertices;
        verticalSectionVertices.reserve(100);

        meshCluster(&meshConnector, &verticalSectionVertices);

        meshConnector.indices.shrink_to_fit();
        GeometryData<TerrainMeshVertex> geometry;
        geometry.indices = std::move(meshConnector.indices);
        geometry.vertices.reserve(meshConnector.vertices.size());

        auto botIndexes = findIndexesAfterDistance(faultSegments.front(), 100);
        auto topIndexes = findIndexesAfterDistance(faultSegments.back(), 100);
        bool applySmoothingMultiplier = botIndexes && topIndexes && botIndexes->second > botIndexes->first && topIndexes->second > topIndexes->first;

        Polygon2D faultBot;
        Polygon2D faultTop;
        Polygon2D faultPolygon;

        if (applySmoothingMultiplier)
        {
            faultBot.setPoints(std::vector<GVector2D>(faultSegments.front().begin() + botIndexes->first, faultSegments.front().begin() + botIndexes->second));
            faultTop.setPoints(std::vector<GVector2D>(faultSegments.back().begin() + topIndexes->first, faultSegments.back().begin() + topIndexes->second));

            std::vector<GVector2D> faultPolygonPts = faultBot.getPts();
            faultPolygonPts.insert(faultPolygonPts.end(), faultTop.getPts().rbegin(), faultTop.getPts().rend());
            faultPolygon.setPoints(faultPolygonPts);
        }

        for (const auto& pt : meshConnector.vertices)
        {
            TerrainMeshVertex tmv{ pt, {}, *this };
            geometry.vertices <<= std::move(tmv);
            if (verticalSectionVertices.contains((GVector2D)pt))
                geometry.vertices.back().biomeTexWeights = 0;

            if (applySmoothingMultiplier)
            {
                float multiplier = 0;

                if (pt.y() == botH)
                    multiplier = std::max(1.0f - faultBot.getRadiusOfInscribedCircleAtPoint(pt) / 100.0f, 0.0f);
                else if (pt.y() == topH)
                    multiplier = std::max(1.0f - faultTop.getRadiusOfInscribedCircleAtPoint(pt) / 100.0f, 0.0f);
                else if (faultPolygon.containsConcave(pt))
                    multiplier = 1.0f;
                else
                    multiplier = std::max(1.0f - faultPolygon.getRadiusOfInscribedCircleAtPoint(pt) / 100.0f, 0.0f);

                if (multiplier > 0.0f)
                    smoothingMultiplierMap[pt] = multiplier;
            }
        }

        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    void FaultCluster::meshVerticalSection(const std::vector<std::vector<GVector2D>>& lines, const std::array<std::array<IndexType, 2>, 2>& outers,
        std::vector<QVector3D>* generatedPoints, std::vector<IndexType>* generatedTriangles, std::unordered_set<GVector2D>* verticalSectionVertices)
    {
        OmniProfile("Vertical mesh");

        auto&& botLine = lines.front();
        auto&& topLine = lines.back();

        std::vector<std::vector<IndexType>> lineIndices;
        lineIndices.reserve(lines.size());
        for (auto&& line : lines)
            lineIndices << std::vector<IndexType>(line.size(), -1);

        for (int i = 0; i < botLine.size(); ++i)
        {
            for (int pIdx = outers[0][0]; pIdx < outers[0][1]; ++pIdx)
                if (vEq(GVector2D((*generatedPoints)[pIdx]), botLine[i]))
                {
                    lineIndices.front()[i] = pIdx;
                    break;
                }

            for (int pIdx = outers[1][0]; pIdx < outers[1][1]; ++pIdx)
                if (vEq(GVector2D((*generatedPoints)[pIdx]), topLine[i]))
                {
                    lineIndices.back()[i] = pIdx;
                    break;
                }

            Q_ASSERT(lineIndices.front()[i] < generatedPoints->size());
            Q_ASSERT(lineIndices.back()[i] < generatedPoints->size());
        }

        // Add intermediate lines' points
        for (int l = 1; l < int(lines.size()) - 1; ++l)
        {
            for (int pIdx = 0; pIdx < lines[l].size(); ++pIdx)
            {
                float botH = (*generatedPoints)[lineIndices.front()[pIdx]].y();
                float topH = (*generatedPoints)[lineIndices.back()[pIdx]].y();
                float h = std::lerp(botH, topH, float(l) / float(int(lines.size()) - 1));

                auto&& p = lines[l][pIdx];
                lineIndices[l][pIdx] = generatedPoints->size();
                generatedPoints->push_back({ p.x, h, p.z });
                verticalSectionVertices->insert((GVector2D)generatedPoints->back());
            }
        }

        // Add quads
        for (int l = 1; l < lines.size(); ++l)
            for (int i = 1; i < botLine.size(); ++i)
            {
                quint32 BL = lineIndices[l - 1][i - 1];
                quint32 TL = lineIndices[l][i - 1];
                quint32 TR = lineIndices[l][i];
                quint32 BR = lineIndices[l - 1][i];

                // 2 triangles
                (*generatedTriangles) << BL << TL << TR;
                (*generatedTriangles) << BL << TR << BR;
            }
    }

    GVector2D TerrainBlockCluster<ETerrainBlock::Fault>::snapPointToBlockBorderPoint(const GVector2D& p, const std::vector<GVector2D>& singleBlockBorderPoints, float threshold)
    {
        float minD = std::numeric_limits<float>::max();
        GVector2D result;

        for (auto&& bp : singleBlockBorderPoints)
            if (float d = distanceSquared(p, bp); d < minD)
            {
                minD = d;
                result = bp;
            }

        if (minD < threshold)
            return result;
        else
            return p;
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::initialize()
    {
        clusterPolygon = calculatePolygon(true);

        // Border points / block
        computeClusterBorderPoints();
        clusterPolygon = Polygon2D::inflatePolygon(clusterPolygon, -50);

        computeFaultParameters();
        computeFaultSegments();

        smoothingParams.weight = 1.0f;
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::clear()
    {
        TerrainBlockClusterBase::clear();

        inOutPtsTop.clear();
        inOutPtsBottom.clear();
        clusterBorderPoints.clear();
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::computeClusterBorderPoints()
    {
        if (!clusterBorderPoints.empty())
            return;

        // Compute own border points
        auto&& cPts = clusterPolygon.getCPts();
        for (int i = 0; i < cPts.getSize(); ++i)
        {
            int i2 = cPts.findIdx(i, 1);
            clusterBorderPoints << splitSegment(Segment2D{ cPts[i], cPts[i2] }, FFirstLastPolicy::First, true);
        }
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::computeFaultParameters()
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& dem = Data::get()->getDEM();

        // Compute bot and top heights
        float maxH = std::numeric_limits<float>::min();
        float minH = std::numeric_limits<float>::max();
        for (auto&& p : clusterBorderPoints)
        {
            float h = dem->heightData.sample(p);
            minH = std::min(h, minH);
            maxH = std::max(h, maxH);
        }

        static std::uniform_real_distribution<float> heightRangeDist(0.4, 0.6);
        float faultHeight = (maxH - minH) * heightRangeDist(gRandomEngine);
        float midH = (maxH + minH) * 0.5f;
        botH = midH - faultHeight * 0.5f;
        topH = midH + faultHeight * 0.5f;
    }

    std::optional<GVector2D> TerrainBlockCluster<ETerrainBlock::Fault>::findHeightMidPoint()
    {
        auto&& heightData = Data::get()->getDEM()->heightData;

        float minH = std::numeric_limits<float>::max(), maxH = std::numeric_limits<float>::min();
        GVector2D minPt;
        GVector2D maxPt;

        for (auto&& pt : clusterBorderPoints)
        {
            auto height = heightData.sample(pt);

            if (minH > height)
            {
                minH = height;
                minPt = pt;
            }
            if (maxH < height)
            {
                maxH = height;
                maxPt = pt;
            }
        }

        if (minPt == maxPt)
            return std::nullopt;

        std::optional<GVector2D> midPoint;
        float biggestHorizontalDist = 0.0f;

        float maxDistance = minPt.dist(maxPt);
        GVector2D toMaxDir = (maxPt - minPt).normalized();
        GVector2D nextPt = minPt + toMaxDir * 100.f;

        // look for mid point with most space to spread
        for (float d = 0.0f; d < maxDistance; d += 100.0f)
        {
            auto&& gradient = heightData.sampleGradient(nextPt).normalized();
            gradient = gradient != GVector2D(0, 0) ? gradient : toMaxDir;

            auto ptForRay = nextPt + gradient.rotatedLeft90() * 100000.0f;
            auto&& intersections = clusterPolygon.rayIntersections({ ptForRay, ptForRay + gradient.rotatedRight90() * 1000000.0f });
            auto&& keys = intersections.keys();

            for (int i = 0; i < keys.size() - 1; i++)
            {
                auto dist = keys[i + 1] - keys[i];
                auto midPt = (std::get<GVector2D>(intersections[keys[i]]) + std::get<GVector2D>(intersections[keys[i + 1]])) * 0.5f;
                auto isZeroGradient = heightData.sampleGradient(midPt) == GVector2D(0, 0);
                auto tooCloseToPolygon = clusterPolygon.getRadiusOfInscribedCircleAtPoint(midPt) < 100.0f;

                if (biggestHorizontalDist < dist && clusterPolygon.containsConcave(midPt) && !isZeroGradient && !tooCloseToPolygon)
                {
                    biggestHorizontalDist = dist;
                    midPoint = midPt;
                }
            }

            nextPt += toMaxDir * 100.f;
        }

        return midPoint;
    }

    std::vector<GVector2D> TerrainBlockCluster<ETerrainBlock::Fault>::calculateFaultMainLine()
    {
        auto&& heightData = Data::get()->getDEM()->heightData;

        auto&& midPoint = findHeightMidPoint();

        if (!midPoint)
            return {};

        auto findGradientPathInsidePolygon = [&](const GVector2D& startPoint, bool rotateLeft)
        {
            std::vector<GVector2D> path;
            std::vector<float> angleDelta;
            const float moveDist = 10.0f;
            const float angleDistControl = 1000.0f;
            const float angleValueControl = 20.0f;

            GVector2D nextPoint = startPoint;
            GVector2D gradient = heightData.sampleGradient(nextPoint).normalized();
            gradient = rotateLeft ? gradient.rotatedLeft90() : gradient.rotatedRight90();
            GVector2D prevGradient = gradient;

            while (true)
            {
                nextPoint += gradient * moveDist;
                auto&& nextGradient = heightData.sampleGradient(nextPoint).normalized();
                nextGradient = rotateLeft ? nextGradient.rotatedLeft90() : nextGradient.rotatedRight90();
                nextGradient = nextGradient != GVector2D(0, 0) ? nextGradient : prevGradient;

                // adjust angle by amount of previous deltas to not pass control value
                auto angle = angle180S(gradient, nextGradient);

                int fromDelta = std::max((int)((angleDelta.size() * moveDist - angleDistControl) / moveDist), 0);
                auto accAngle = std::accumulate(angleDelta.begin() + fromDelta, angleDelta.end(), 0.0f);
                if (std::abs(accAngle + angle) > angleValueControl)
                    angle = (accAngle > 0 ? 1.0f : -1.0f) * angleValueControl - accAngle;

                gradient = GVector2D::rotateDegrees(gradient, angle);

                // in case of path moving close and in direction of cocave point, end path (better solution might be discovered)
                for(int i = 0; i < clusterPolygon.getPts().size(); i++)
                    if (clusterPolygon.isConcaveVertex(i) && clusterPolygon[i].dist(nextPoint) < 100)
                    {
                        auto angleToConcave = angle180S(gradient, (clusterPolygon[i] - nextPoint).normalized());
                        if (std::abs(angleToConcave) < 60)
                            return path;
                        
                    }

                // end path on intersection / if point is outside of polygon
                if (auto&& intersections = clusterPolygon.rayIntersections({ path.empty() ? *midPoint : path.back(), nextPoint }); !intersections.empty() || !clusterPolygon.containsConcave(nextPoint))
                    break;

                path << nextPoint;
                angleDelta << angle;
                prevGradient = gradient;
            }

            return path;
        };

        std::vector<GVector2D> leftSide = findGradientPathInsidePolygon(*midPoint, true);
        std::vector<GVector2D> rightSide = findGradientPathInsidePolygon(*midPoint, false);
        std::reverse(leftSide.begin(), leftSide.end());

        return leftSide << *midPoint << rightSide;
    }

    std::optional<GVector2D> TerrainBlockCluster<ETerrainBlock::Fault>::lookForConcavePoint(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& direction)
    {
        if (idx == 0 || idx == mainLine.size() - 1)
            return std::nullopt;

        auto findPolygonPartForSearch = [&]()
        {
            auto&& betweenIntersections = clusterPolygon.rayIntersections({ mainLine[idx], mainLine[idx] + direction * 100000.0f });
            Q_ASSERT(!betweenIntersections.empty());

            auto&& startIntersections = clusterPolygon.rayIntersections({ mainLine[0], mainLine[0] + direction * 100000.0f });
            Q_ASSERT(!startIntersections.empty());

            auto&& endIntersections = clusterPolygon.rayIntersections({ mainLine[mainLine.size() - 1], mainLine[mainLine.size() - 1] + direction * 100000.0f });
            Q_ASSERT(!endIntersections.empty());

            auto&& [betweenPt, _] = betweenIntersections.begin().value();
            auto&& [startPt, startIndexes] = startIntersections.begin().value();
            auto&& [endPt, endIndexes] = endIntersections.begin().value();

            auto&& cPts = clusterPolygon.getCPts();
            int winding = 0;

            auto prevPt = startPt;
            for (int i = 0; i < cPts.getSize() + 1; i++)
            {
                auto nextPt = cPts[cPts.findIdx(startIndexes[1], i)];

                Segment2D testSeg(prevPt, nextPt);
                auto distToEnd = prevPt.dist(endPt);
                auto distToBetween = prevPt.dist(betweenPt);

                if (testSeg.dist(endPt) < 1.0f && !(testSeg.dist(betweenPt) < 1.0f && distToBetween < distToEnd))
                {
                    winding = -1;
                    break;
                }
                else if (testSeg.dist(betweenPt) < 1.0f)
                {
                    winding = 1;
                    break;
                }

                prevPt = nextPt;
            }

            Q_ASSERT(winding != 0);
            int start = winding > 0 ? startIndexes[0] : endIndexes[0];
            int end = winding > 0 ? endIndexes[1] : startIndexes[1];

            return std::pair<int, int>{ start, end };
        };

        auto&& cPts = clusterPolygon.getCPts();
        auto&& [startIdx, endIdx] = findPolygonPartForSearch();

        std::optional<GVector2D> concavePoint;
        float closestConcave = std::numeric_limits<float>::max();

        int nextIdx = startIdx;
        while (true)
        {
            auto&& dist = mainLine[idx].dist(clusterPolygon[nextIdx]);
            if (clusterPolygon.isConcaveVertex(nextIdx) && closestConcave > dist)
            {
                concavePoint = clusterPolygon[nextIdx];
                closestConcave = dist;
            }

            if (nextIdx == endIdx)
                break;

            nextIdx = cPts.findIdx(nextIdx, 1);
        }

        return concavePoint;
    }

    std::optional<GVector2D> TerrainBlockCluster<ETerrainBlock::Fault>::findIntersectionWithConcaveSegment(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& direction)
    {
        if (auto&& concavePoint = lookForConcavePoint(mainLine, idx, direction); concavePoint)
        {
            auto&& lineDirection = (mainLine[idx - 1] - mainLine[idx + 1]).normalized();
            Segment2D segment(*concavePoint + lineDirection * 100000.0f, *concavePoint - lineDirection * 100000.0f);

            return segment.getIntersectionPoint({ mainLine[idx], mainLine[idx] + direction * 100000.0f });
        }

        return std::nullopt;
    }

    std::pair<int, int> TerrainBlockCluster<ETerrainBlock::Fault>::fixSnappedLine(std::vector<GVector2D>* line, float acceptableAngle)
    {
        // Find index where line direction and line to snap point are within acceptable angle
        std::pair<int, int> indexesToSnap;

        for (int i = 0; i < line->size() - 2; i++)
        {
            auto&& toEndDir = (line->front() - (*line)[i + 1]).normalized();
            auto&& lineDir = ((*line)[i + 1] - (*line)[i + 2]).normalized();

            if (angle180(lineDir, toEndDir) < acceptableAngle)
            {
                indexesToSnap.first = i + 1;
                break;
            }
        }

        for (int i = line->size() - 1; i > 1; i--)
        {
            auto&& toEndDir = (line->back() - (*line)[i - 1]).normalized();
            auto&& lineDir = ((*line)[i - 1] - (*line)[i - 2]).normalized();

            if (angle180(lineDir, toEndDir) < acceptableAngle)
            {
                indexesToSnap.second = i - 1;
                break;
            }
        }

        auto straightenLine = [](std::vector<GVector2D>* line, int from, int to)
        {
            auto&& dir = ((*line)[to] - (*line)[from]).normalized();
            auto&& dist = (*line)[from].dist((*line)[to]);
            int diff = to - from;

            for (int i = 1; i < diff; i++)
                (*line)[from + i] = (*line)[from] + dir * dist * (i / (float)diff);
        };

        straightenLine(line, 0, indexesToSnap.first);
        straightenLine(line, indexesToSnap.second, line->size() - 1);
        return indexesToSnap;
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::snapToBorderPoints(std::vector<GVector2D>* botLine, std::vector<GVector2D>* topLine, const std::vector<GVector2D>& mainLine)
    {
        Polygon2D borderPolygon(clusterBorderPoints);

        auto findPointsToSnapTo = [&](const GVector2D& point)
        {
            auto&& edges = borderPolygon.getClosestEdges(point);
            auto&& [i1, i2, pt] = edges.front();

            return std::pair(borderPolygon[i1], borderPolygon[i2]);
        };

        std::tie(topLine->front(), botLine->front()) = findPointsToSnapTo(mainLine.front());
        std::tie(botLine->back(), topLine->back()) = findPointsToSnapTo(mainLine.back());
    }

    float getNoise(float x, float z)
    {
        static std::mt19937 randomEngine;
        static noise::module::RidgedMulti noiseSource;
        static noise::model::Plane noiseModel;
        static std::atomic_bool isInited = false;

        if (!isInited)
        {
            static std::mutex guard;
            std::scoped_lock lock(guard);
            noiseSource.SetSeed(randomEngine());
            noiseSource.SetFrequency(2e-3f);
            noiseSource.SetOctaveCount(2);
            noiseModel.SetModule(noiseSource);
            isInited = true;
        }

        return noiseModel.GetValue(x, z);
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::applyNoise(std::vector<GVector2D>* line, int from, int to, const std::vector<GVector2D>& mainLine)
    {
        // Reduce noise on edges of line
        auto edgeNoiseReduction = [](int i, int size, float modifier)
        {
            return std::clamp((1.0f - std::abs(size * 0.5f - i) / (size * 0.5f)) / modifier, 0.0f, 1.0f);
        };

        for (int i = from; i < to; i++)
        {
            auto dir = (mainLine[i] - (*line)[i]).normalized();
            auto dist = mainLine[i].dist((*line)[i]);
            (*line)[i] += dir * dist * edgeNoiseReduction(i - from, to, 0.5f) * std::lerp(0.15f, 0.35f, std::abs(getNoise((*line)[i].x, (*line)[i].z)));
        }
    }

    void TerrainBlockCluster<ETerrainBlock::Fault>::computeFaultSegments()
    {
        OmniProfile("Fault segments");

        auto&& cells = Data::get()->getTerrainCells()->getCells();
        auto&& DEM = Data::get()->getDEM();
        auto&& heightData = Data::get()->getDEM()->heightData;

        auto&& mainLine = calculateFaultMainLine();
        std::reverse(mainLine.begin(), mainLine.end());

        // remove if line is too small
        float mainLineDist = 0.0f;
        for (int i = 0; i < (int)mainLine.size() - 1; i++)
            mainLineDist += mainLine[i].dist(mainLine[i + 1]);

        if (mainLineDist < 200.0f)
            return;
        
        bool isOverhang = std::bernoulli_distribution(0.5f)(Generation::gRandomEngine);
        bool isFlatWall = std::bernoulli_distribution(0.25f)(Generation::gRandomEngine);
        float maxFaultWidth = 40.0f * std::max((topH - botH) / 50.0f, 1.0f);
        maxFaultWidth = isFlatWall ? std::uniform_real_distribution<float>(1.0f, 5.0f)(Generation::gRandomEngine) : maxFaultWidth;

        auto calculateFaultLine = [&](bool towardGradient)
        {
            std::vector<GVector2D> faultLine;

            auto&& p = mainLine[mainLine.size() * 0.5f];
            auto gradient = heightData.sampleGradient(p);
            if (gradient.isNull())
                gradient = DEM->getCellElevationData(*cells[Utils::findCell(p)]).gradient;

            // Overhang shall always stop being overhang near ends of line for better merging purpose
            auto overhangLimitIndexes = findIndexesAfterDistance(mainLine, 50.0f);

            for (int i = 0; i < mainLine.size(); i++)
            {
                float distToEdge = 0.0f;

                bool applyOverHang = (i >= overhangLimitIndexes->first && i <= overhangLimitIndexes->second) ? isOverhang : false;
                GVector2D gradientDirection = gradient * (towardGradient ? 1.0f : -1.0f) * (applyOverHang ? -1.0 : 1.0);

                auto&& polygonIntersection = clusterPolygon.rayIntersections({ mainLine[i], mainLine[i] + gradientDirection * 100000.0f });
                Q_ASSERT(!polygonIntersection.empty());

                // Intention here is to avoid case where fault line is behind concave point
                if (auto&& concaveIntersection = findIntersectionWithConcaveSegment(mainLine, i, gradientDirection); concaveIntersection)
                    distToEdge = std::min(mainLine[i].dist(*concaveIntersection), polygonIntersection.firstKey());
                else
                    distToEdge = polygonIntersection.firstKey();

                auto&& t = (float)i / mainLine.size();
                auto distToMove = std::min(maxFaultWidth * (1.0f - std::abs(t - 0.5f)), distToEdge * 0.5f);
                faultLine.push_back(mainLine[i] + gradientDirection * distToMove);
            }

            return faultLine;
        };

        std::vector<GVector2D> botLine = calculateFaultLine(false);
        std::vector<GVector2D> topLine = calculateFaultLine(true);

        snapToBorderPoints(&botLine, &topLine, mainLine);
        auto&& [botFrontIdx, botBackIdx] = fixSnappedLine(&botLine, 50.0f);
        auto&& [topFrontIdx, topBackIdx] = fixSnappedLine(&topLine, 50.0f);

        botLine = BezierCurve2D(botLine).getPoints(botLine.size() - 1);
        topLine = BezierCurve2D(topLine).getPoints(topLine.size() - 1);

        applyNoise(&botLine, botFrontIdx, botBackIdx, mainLine);
        applyNoise(&topLine, topFrontIdx, topBackIdx, mainLine);

        // Generate between lines (wall lines)
        std::vector<std::vector<GVector2D>> betweenLines;

        // Create character of wall lines
        int amountOfLines = 6;
        float bottomCurveOffset = std::uniform_real_distribution<float>(0.05f, 0.95f)(Generation::gRandomEngine);
        float topCurveOffset = std::uniform_real_distribution<float>(0.05f, 0.95f)(Generation::gRandomEngine);
        BezierCurve2D verticalCurveCharacter(std::vector<GVector2D>{ {0, 0}, { bottomCurveOffset, 0.25f }, { topCurveOffset, 0.75f }, { 1, 1 }});

        for (int line = 1; line < amountOfLines; line++)
        {
            std::vector<GVector2D> betweenLine;

            auto t = verticalCurveCharacter.evaluate((float)line / amountOfLines).x;

            for (int i = 0; i < mainLine.size(); i++)
            {
                auto&& dist = botLine[i].dist(topLine[i]);
                auto&& dir = botLine[i] + (topLine[i] - botLine[i]) * t;

                betweenLine.push_back(dir);
            }

            betweenLine.front() = topLine.front();
            betweenLine.back() = topLine.back();
            betweenLines << BezierCurve2D(betweenLine).getPoints(betweenLine.size() - 1);
        }

        faultSegments << botLine << betweenLines << topLine;

#if DEBUG_FAULT_LINES
        {
            std::vector<QVector3D> line;
            for (auto&& pt : mainLine)
                line << QVector3D(pt.x, heightData.sample(pt), pt.z);

            spawn<DLineMarker>(line, QVector4D(1, 0, 0, 1));
        }

        {
            std::vector<QVector3D> line;
            for (auto&& pt : faultSegments.front())
                line << QVector3D(pt.x, botH, pt.z);

            spawn<DLineMarker>(line, QVector4D(0,1,0,1));
        }
        {
            std::vector<QVector3D> line;
            for (auto&& pt : faultSegments.back())
                line << QVector3D(pt.x, topH, pt.z);

            spawn<DLineMarker>(line, QVector4D(0, 0, 1, 1));
        }

        {
            for (int i = 1; i < faultSegments.size() - 2; i++)
            {
                std::vector<QVector3D> line;
                for (auto&& pt : faultSegments[i])
                    line << QVector3D(pt.x, botH + (topH - botH) * i/(faultSegments.size() - 2), pt.z);

                spawn<DLineMarker>(line, QVector4D(1, 0, 1, 1));
            }
        }
#endif
    }

    ClusterData<ETerrainBlock::Fault>::ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Fault>* metaCluster, int id)
        : ClusterDataBase(id)
        , cellGraph{ id }
    {
        auto&& cell = Data::get()->getTerrainCells()->getCellAt(id);
        leftEnd = cell->getCenter();
        rightEnd = leftEnd;

        baseDir = Data::get()->getDEM()->heightData.sampleGradient(cell->getCenter()).rotatedRight90();
        if (baseDir.isNull())
        {
            CellElevationData ced = Data::get()->getDEM()->getCellElevationData(*cell);
            baseDir = ced.gradient;
        }
    }

    std::unordered_set<int> ClusterData<ETerrainBlock::Fault>::customGrow(const std::unordered_set<int>& candidates)
    {
        static const float maxTurnAngle = 60.0f;
        static const float maxDeviationFromGradient = 30.0f;

        auto&& diagram = Data::get()->getTerrainCells();
        auto&& leftCell = diagram->getCellAt(cellGraph.front());
        auto&& rightCell = diagram->getCellAt(cellGraph.back());
        float bestLeftAngle = 180.0f;
        float bestRightAngle = 180.0f;
        std::optional<int> bestLeftExtension;
        std::optional<int> bestRightExtension;

        for (int nid : candidates)
        {
            auto&& cell = diagram->getCellAt(nid);
            GVector2D center = cell->getCenter();
            auto&& neighbors = cell.getNeighbors();
            CellElevationData ced = Data::get()->getDEM()->getCellElevationData(*cell);
            //auto gradient = Data::get()->getDEM()->sampleGradient(center).normalized();

            // Is neighbor of start
            if (neighbors.contains(cellGraph.front()))
            {
                GVector2D dir = (center - leftEnd).normalized();
                float griadientDeviation = angle180(ced.gradient.rotatedRight90(), dir);
                if (griadientDeviation > maxDeviationFromGradient)
                    continue;

                if (cellGraph.size() > 1)
                {
                    auto [a, b] = getEdgeBetweenNeighbors(cellGraph[0], cellGraph[1]);
                    GVector2D currentDir = diagram->getCellAt(cellGraph[0])->getCenter() - (a + b) * 0.5;

                    float turnAngle = angle180(currentDir.normalized(), dir);
                    if (turnAngle > maxTurnAngle)
                        continue;

                    if (turnAngle < bestLeftAngle)
                    {
                        bestLeftAngle = turnAngle;
                        bestLeftExtension = nid;
                    }
                }
                else
                {
                    float turnAngle = angle180(baseDir.normalized(), dir);
                    if (turnAngle > maxTurnAngle)
                        continue;

                    if (turnAngle < bestLeftAngle)
                    {
                        bestLeftAngle = turnAngle;
                        bestLeftExtension = nid;
                    }
                }
            }

            // Is neighbor of end
            if (neighbors.contains(cellGraph.back()))
            {
                GVector2D dir = (rightEnd - center).normalized();
                float griadientDeviation = angle180(ced.gradient.rotatedRight90(), dir);
                if (griadientDeviation > maxDeviationFromGradient)
                    continue;

                if (cellGraph.size() > 1)
                {
                    auto [a, b] = getEdgeBetweenNeighbors(cellGraph.back(), *(cellGraph.rbegin() + 1));
                    GVector2D currentDir = diagram->getCellAt(cellGraph.back())->getCenter() - (a + b) * 0.5;

                    float turnAngle = angle180(currentDir.normalized(), dir);
                    if (turnAngle > maxTurnAngle)
                        continue;

                    if (turnAngle < bestRightAngle)
                    {
                        bestRightAngle = turnAngle;
                        bestRightExtension = nid;
                    }
                }
                else
                {
                    float turnAngle = angle180(baseDir.normalized(), dir);
                    if (turnAngle > maxTurnAngle)
                        continue;

                    if (turnAngle < bestRightAngle)
                    {
                        bestRightAngle = turnAngle;
                        bestRightExtension = nid;
                    }
                }
            }
        }

        std::unordered_set<int> results;

        if (bestRightExtension)
            if (auto&& added = ClusterDataBase::customGrow({ *bestRightExtension }); added.contains(*bestRightExtension))
            {
                cellGraph.insert(cellGraph.end(), *bestRightExtension);
                rightEnd = diagram->getCellAt(*bestRightExtension)->getCenter();
                results += *bestRightExtension;
            }
        if (bestLeftExtension)
            if (auto&& added = ClusterDataBase::customGrow({ *bestLeftExtension }); added.contains(*bestLeftExtension))
            {
                cellGraph.insert(cellGraph.begin(), *bestLeftExtension);
                leftEnd = diagram->getCellAt(*bestLeftExtension)->getCenter();
                results += *bestLeftExtension;
            }

        return results;
    }

    std::array<Segment2D, 2> ClusterData<ETerrainBlock::Fault>::completeFaultline(std::vector<GVector2D>* line, bool replaceEnds) const
    {
        static const float rayLookupRange = GRID_SEGMENT_WIDTH * 10.0f;
        std::array<Segment2D, 2> results;

        auto&& diagram = Data::get()->getTerrainCells();

        // First cell
        Segment2D firstRay = { (*line)[0], (*line)[0] + ((*line)[0] - (*line)[1]).normalized() * rayLookupRange };
        {
            auto&& area = diagram->getCellAt(cellGraph.front()).getPolygon();
            Q_ASSERT(area.contains(firstRay.first));
            auto intersections = area.rayIntersections(firstRay);
            Q_ASSERT(!intersections.isEmpty());

            std::array<int, 2> seg;
            std::tie(std::ignore, seg) = intersections.last();
            results[0] = { area[seg[0]], area[seg[1]] };

            if (replaceEnds)
                (*line).front() = results[0].midpoint();
            else
                line->insert(line->begin(), results[0].midpoint());
        }

        // Last cell
        Segment2D lastRay = { (*line).back(), (*line).back() + ((*line).back() - *((*line).rbegin() + 1)).normalized() * rayLookupRange };
        {
            auto&& area = diagram->getCellAt(cellGraph.back()).getPolygon();
            Q_ASSERT(area.contains(lastRay.first));
            auto intersections = area.rayIntersections(lastRay);
            Q_ASSERT(!intersections.isEmpty());

            std::array<int, 2> seg;
            std::tie(std::ignore, seg) = intersections.last();
            results[1] = { area[seg[0]], area[seg[1]] };

            if (replaceEnds)
                (*line).back() = results[1].midpoint();
            else
                line->push_back(results[1].midpoint());
        }

        return results;
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Fault>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
    omniBin << object.botH;
    omniBin << object.topH;
    omniBin << object.verticalSegmentCount;
    omniBin << object.faultWidthVariance;
    omniBin << object.inOutPtsTop;
    omniBin << object.inOutPtsBottom;
    omniBin << object.clusterBorderPoints;
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Fault>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
    omniBin >> object.botH;
    omniBin >> object.topH;
    omniBin >> object.verticalSegmentCount;
    omniBin >> object.faultWidthVariance;
    omniBin >> object.inOutPtsTop;
    omniBin >> object.inOutPtsBottom;
    omniBin >> object.clusterBorderPoints;
}
