#include "stdafx.h"
#include <Editor/Sections/Profiler/OmnigenProfiler.h>
#include "TerrainBlockBase.h"
#include "Utils/Voronoi/Voronoi.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Common/Markers/BatchingLineMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include <set>
#include <noise/noise.h>
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Utils/poly2tri/poly2tri.h"
#include "Utils/Interpolation.h"

#define MESH_DUBLICATES_VALIDATION 0

#if MESH_DUBLICATES_VALIDATION
#include <QString>
#endif

#define DEBUG_NEW_TRIANGULATION 1

using namespace Generation;

int getMeshSegments(float edgeLength)
{
    return edgeLength / gMaxTriangleSideLength + 1;
}

int getMeshSegmentsAdv(const GVector2D& p1, const GVector2D& p2)
{
    return getMeshSegments(distance(p1, p2));
}

int getMeshSegments3D(const GVector2D& p1, const GVector2D& p2)
{
    auto&& dem = Data::get()->getDEM();
    QVector3D P1 = { p1.x, dem->heightData.sample(p1), p1.z };
    QVector3D P2 = { p2.x, dem->heightData.sample(p2), p2.z };
    const float dist = distance(P1, P2);
    return dist / gMaxTriangleSideLength + 1;
}

static std::vector<GVector2D> splitSegmentWrapper(const GVector2D& v1, const GVector2D& v2, FFirstLastPolicy policy)
{
    return splitSegment(Segment2D{ v1, v2 }, policy, true);
}

static void validateArrayForDublicates(const std::vector<GVector2D>& vertices)
{
#if MESH_DUBLICATES_VALIDATION
    std::unordered_map<GVector2D, int> testMap;
    testMap.reserve(vertices.size());
    for (int i = 0; i < vertices.size(); ++i)
    {
        const auto& vert = vertices[i];
        const auto iter = testMap.find(vert);
        if (iter != testMap.end())
        {
            const QString errorText = QString("Dublicated points in output vertex array! index %1 and original index %2 vertex[%3, %4]")
                .arg(i).arg(iter->second).arg(vert.x).arg(vert.x);
            Q_ASSERT_X(false, "Meshing validation", errorText.toStdString().c_str());
        }
        testMap[vert] = i;
    }
#endif
}

const MeshingParams& getDefaultMeshingParams()
{
    static auto meshingParams = []()
    {
        MeshingParams params;
        params.innerSplitFunc = &splitSegmentWrapper;
        params.outerSplitFunc = &splitSegmentWrapper;
        params.splitOuter = true;
        return params;
    }();
    
    return meshingParams;
}

std::vector<GVector2D> makeDetailedPolygon(const std::vector<GVector2D>& inPolygon, const MeshingParams& meshingParams)
{
    auto cPts = asCircular(inPolygon);

    std::vector<GVector2D> result;
    for (int i = 0; i < cPts.getSize(); ++i)
    {
        auto detailedSegment = meshingParams.outerSplitFunc(cPts[i], cPts[cPts.findIdx(i, 1)], FFirstLastPolicy::First);

        result.reserve(result.size() + detailedSegment.size());
        for (auto&& p : detailedSegment)
            result.push_back(std::move(p));
    }

    return result;
}

struct PolygonInfo
{
    struct PerVertex
    {
        IndexType idx;
        float angle;
        GVector2D dir;
        float isolation;
    };

    std::vector<PerVertex> pts;
    std::vector<PolygonInfo> parent;
};

float angle360CW(const GVector2D& v1, const GVector2D& v2)
{
    // [0-180]
    float result = std::acosf(std::clamp(double(GVector2D::dotProduct(v1, v2)), -1.0, 1.0)) * 180.0 / std::numbers::pi;
    if (result == 0.0f)
        return 0.0f;

    // expand to [0-360]
    if (GVector2D::crossProduct(v1, v2) > 0)
        return 360.0 - result;
    else
        return result;
}

void printSrcPolygon(const std::vector<GVector2D>& pts)
{
    QString result = "Source: ";
    for (const auto& pt : pts)
    {
        result.append(QString("(%1, %2), ").arg(pt.x).arg(pt.z));
    }
    auto&& fp = pts[0];
    result.append(QString("(%1, %2)").arg(fp.x).arg(fp.z));

    OmniLog() <<= result;
}

void dbgPrint(const PolygonInfo& poly, GeometryData<GVector2D>& data, bool crash = true)
{
    QString result;
    for (const auto& vp : poly.pts)
    {
        auto& pt = data.vertices[vp.idx];
        result.append(QString("(%1, %2), ").arg(pt.x).arg(pt.z));
    }
    auto&& fp = data.vertices[poly.pts[0].idx];
    result.append(QString("(%1, %2)").arg(fp.x).arg(fp.z));

    OmniLog() <<= result;
    Q_ASSERT(!crash);
}

std::array<IndexType, 2> findBestDiagonal(const std::vector<GVector2D>& srcPolygon, const PolygonInfo& poly, GeometryData<GVector2D>& data)
{
    auto cPts = asCircular(poly.pts);

    std::vector<GVector2D> hardPts;
    hardPts.reserve(poly.pts.size());
    for (auto&& vp : poly.pts)
        hardPts.push_back(data.vertices[vp.idx]);

// #if DEBUG_NEW_TRIANGULATION
//     Polygon2D test(hardPts);
//     Q_ASSERT(test.isCW());
// #endif

    // Find best point using angle criteria
    std::unordered_set<IndexType> failedPoints;

TRY_ANOTHER_POINT:

    float bestAngle = 0.0f;
    float avgIsolation = 0.0f;
    IndexType bestIdx;
    for (IndexType i = 0; i < cPts.getSize(); ++i)
    {
        avgIsolation += cPts[i].isolation;
        if (!failedPoints.contains(i) && cPts[i].angle > bestAngle)
        {
            bestAngle = cPts[i].angle;
            bestIdx = i;
        }
    }
    avgIsolation /= float(cPts.getSize());

    bool isConvex = (bestAngle < 181.0f);
    auto& bestPoint = cPts[bestIdx];
    auto&& bestVertex = data.vertices[bestPoint.idx];
    GVector2D forwardDir = (data.vertices[cPts.getNext(bestIdx).idx] - bestVertex).normalized();
    GVector2D backDir = (data.vertices[cPts.getPrev(bestIdx).idx] - bestVertex).normalized();
    GVector2D outDir = cPts[bestIdx].dir.rotatedRight90();

    auto checkIsDiagonal = [&](const GVector2D& vertex, int otherIdx)
    {
        GVector2D diagonalDir = (vertex - bestVertex).normalized();
        auto v1 = bestVertex + diagonalDir * 3.0f;
        auto v2 = vertex - diagonalDir * 3.0f;

        if (float d = distance(v1, v2); d < 6.0f)
            return std::lerp(v1, v2, 0.5).isInsidePolygon(hardPts);

        if (!v1.isInsidePolygon(hardPts) || !v2.isInsidePolygon(hardPts))
            return false;

        // Raycast
        std::array<GVector2D, 2> diagonalSegment = { v1, v2 };
        for (int i = 0; i < cPts.getSize(); ++i)
        {
            if (i == otherIdx || i == bestIdx)
                continue;

            int i2 = cPts.findIdx(i, 1);
            if (i2 == otherIdx || i2 == bestIdx)
                continue;

            auto& s1 = cPts.get(i, 0);
            auto& s2 = cPts.get(i, 1);

            if (std::get<float>(distance(diagonalSegment, std::array{ data.vertices[s1.idx], data.vertices[s2.idx] })) < 1.0f)
                return false;
        }

        return true;
    };

    float minD = std::numeric_limits<float>::max();
    std::optional<IndexType> bestOtherIdx;
    static const auto smoothstep = Interpolation::getInterpolation01(EInterpolation01::Smoothstep, 2);

    auto processPoint = [&](int i, const PolygonInfo::PerVertex& p)
    {
        if (float angle = angle180(bestPoint.dir, p.dir); angle > 90.0f) // need to be at least slightly opposed
        {
            float isolationFactor = avgIsolation / p.isolation; // prefer more isolated vertices
            float angleFactor = std::pow(1.0f - (*smoothstep)((angle - 90.0f) / 90.0f), 1); // prefer angle bisectors to others
            GVector2D& vertex = data.vertices[p.idx];
            if (float weightedDistance = distance(bestVertex, vertex) * angleFactor * isolationFactor; weightedDistance < minD) // get the closest one
                if (isConvex || checkIsDiagonal(vertex, i)) // ensure the diagonal is inside the polygon
                {
                    minD = weightedDistance;
                    bestOtherIdx = i;
                }
        }
    };

    // Find best opposite point: Pass #1 (no sharp angles)
    int iBegin = cPts.findIdx(bestIdx, 2);
    int iEnd = cPts.findIdx(bestIdx, -1);
    for (int i = iBegin; i != iEnd; i = cPts.findIdx(i, 1))
        if (auto& p = cPts[i]; p.angle >= 180.0f)
            processPoint(i, p);

    // Find best opposite point: Pass #2 (allow sharp angles)
    if (!bestOtherIdx)
        for (int i = iBegin; i != iEnd; i = cPts.findIdx(i, 1))
            processPoint(i, cPts[i]);

    if (!bestOtherIdx) [[unlikely]]
    {
        failedPoints.insert(bestIdx);
        goto TRY_ANOTHER_POINT;
    }
    
#if DEBUG_NEW_TRIANGULATION
    if (!bestOtherIdx)
    {
        auto polygon = poly;
        while (true)
        {
            dbgPrint(polygon, data, false);

            if (polygon.parent.empty())
                break;

            polygon = polygon.parent[0];
        }

        printSrcPolygon(srcPolygon);
    }
#endif
    return { bestIdx, *bestOtherIdx };
}

int findNearestPoint(const GVector2D& p, PolygonInfo& poly, GeometryData<GVector2D>& data)
{
    float minD = std::numeric_limits<float>::max();
    int result;

    for (auto& vp : poly.pts)
        if (float d = distance(data.vertices[vp.idx], p); d < minD)
        {
            minD = d;
            result = vp.idx;
        }

    return result;
}

std::vector<PolygonInfo> splitPolygonImpl(PolygonInfo& poly, GeometryData<GVector2D>& data, int da, int db, const std::vector<GVector2D>& diagonalPoints)
{
    auto&& dpa = data.vertices[poly.pts[da].idx];
    auto&& dpb = data.vertices[poly.pts[db].idx];

    GVector2D diagonalDir = (dpb - dpa).normalized();
    float diagonalPointsIsolation = distance(dpa, dpb) / (diagonalPoints.size() + 1);

    // Add new points to triangulation result
    IndexType offset = data.vertices.size();
    data.vertices.reserve(data.vertices.size() + diagonalPoints.size());
    for (auto&& dp : diagonalPoints)
        data.vertices.push_back(dp);

    // Construct virtual polygons
    auto cPts = asCircular(poly.pts);
    std::vector<PolygonInfo> results(2);

    // Build cw
    {
        PolygonInfo& newPoly = results[0];
        newPoly.pts.reserve(diagonalPoints.size() + cPts.distCW(da, db) + 1);

        // Add diagonal points
        for (IndexType i = 0; i < diagonalPoints.size(); ++i)
        {
            PolygonInfo::PerVertex v;
            v.idx = offset + i;
            v.angle = 180.0f;
            v.dir = diagonalDir;
            v.isolation = diagonalPointsIsolation;

            newPoly.pts.push_back(std::move(v));
        }

        // Add b, recompute angle and dir
        {
            auto p1 = cPts[db];
            auto& p2 = cPts[cPts.findIdx(db, 1)];

            const GVector2D& pos0 = diagonalPoints.empty() ? data.vertices[cPts[da].idx] : diagonalPoints.back();
            GVector2D& pos1 = data.vertices[p1.idx];
            GVector2D& pos2 = data.vertices[p2.idx];

            GVector2D v10 = (pos0 - pos1).normalized();
            GVector2D v12 = (pos2 - pos1).normalized();
            p1.angle = angle360CW(v10, v12);
            p1.dir = (v12 - v10).normalized();
            p1.isolation = std::min(distance(pos0, pos1), distance(pos1, pos2));
            Q_ASSERT(p1.isolation > 0.0f);

            if (p1.angle == 0.0f) [[unlikely]]
            {
                return {};
            }

            newPoly.pts.push_back(std::move(p1));
        }

        // Copy unchanged part from the parent poly, go from b to a
        for (int i = cPts.findIdx(db, 1); i != da; i = cPts.findIdx(i, 1))
            newPoly.pts.push_back(cPts[i]);

        // Add a, recompute angle and dir
        {
            auto& p0 = cPts[cPts.findIdx(da, -1)];
            auto p1 = cPts[da];

            GVector2D& pos0 = data.vertices[p0.idx];
            GVector2D& pos1 = data.vertices[p1.idx];
            const GVector2D& pos2 = diagonalPoints.empty() ? data.vertices[cPts[db].idx] : diagonalPoints[0];

            GVector2D v10 = (pos0 - pos1).normalized();
            GVector2D v12 = (pos2 - pos1).normalized();
            p1.angle = angle360CW(v10, v12);
            p1.dir = (v12 - v10).normalized();
            p1.isolation = std::min(distance(pos0, pos1), distance(pos1, pos2));
            Q_ASSERT(p1.isolation > 0.0f);

            if (p1.angle == 0.0f) [[unlikely]]
            {
                return {};
            }

            newPoly.pts.push_back(std::move(p1));
        }
    };

    // Build ccw
    diagonalDir = -diagonalDir;
    {
        PolygonInfo& newPoly = results[1];
        newPoly.pts.reserve(diagonalPoints.size() + cPts.distCW(db, da) + 1);

        // Add diagonal points
        for (int i = diagonalPoints.size() - 1; i >= 0; --i)
        {
            PolygonInfo::PerVertex v;
            v.idx = offset + i;
            v.angle = 180.0f;
            v.dir = diagonalDir;
            v.isolation = diagonalPointsIsolation;

            newPoly.pts.push_back(std::move(v));
        }

        // Add a, recompute angle and dir
        {
            auto p1 = cPts[da];
            auto& p2 = cPts[cPts.findIdx(da, 1)];

            const GVector2D& pos0 = diagonalPoints.empty() ? data.vertices[cPts[db].idx] : diagonalPoints.back();
            GVector2D& pos1 = data.vertices[p1.idx];
            GVector2D& pos2 = data.vertices[p2.idx];

            GVector2D v10 = (pos0 - pos1).normalized();
            GVector2D v12 = (pos2 - pos1).normalized();
            p1.angle = angle360CW(v10, v12);
            p1.dir = (v12 - v10).normalized();
            p1.isolation = std::min(distance(pos0, pos1), distance(pos1, pos2));
            Q_ASSERT(p1.isolation > 0.0f);

            if (p1.angle == 0.0f) [[unlikely]]
            {
                return {};
            }

            newPoly.pts.push_back(std::move(p1));
        }

        // Copy unchanged part from the parent poly, go from a to b
        for (int i = cPts.findIdx(da, 1); i != db; i = cPts.findIdx(i, 1))
            newPoly.pts.push_back(cPts[i]);

        // Add b, recompute angle and dir
        {
            auto& p0 = cPts[cPts.findIdx(db, -1)];
            auto p1 = cPts[db];

            GVector2D& pos0 = data.vertices[p0.idx];
            GVector2D& pos1 = data.vertices[p1.idx];
            const GVector2D& pos2 = diagonalPoints.empty() ? data.vertices[cPts[da].idx] : diagonalPoints[0];

            GVector2D v10 = (pos0 - pos1).normalized();
            GVector2D v12 = (pos2 - pos1).normalized();
            p1.angle = angle360CW(v10, v12);
            p1.dir = (v12 - v10).normalized();
            p1.isolation = std::min(distance(pos0, pos1), distance(pos1, pos2));
            Q_ASSERT(p1.isolation > 0.0f);

            if (p1.angle == 0.0f) [[unlikely]]
            {
                return {};
            }

            newPoly.pts.push_back(std::move(p1));
        }
    };

    return results;
}

std::vector<PolygonInfo> splitPolygon(PolygonInfo& poly, GeometryData<GVector2D>& data, const std::vector<GVector2D>& diagonal, const MeshingParams& meshingParams)
{
    // Find nearest points for diagonal
    int da = findNearestPoint(*diagonal.begin(), poly, data);
    int db = findNearestPoint(*diagonal.rbegin(), poly, data);
    Q_ASSERT(da != db);

    auto&& dpa = data.vertices[poly.pts[da].idx];
    auto&& dpb = data.vertices[poly.pts[db].idx];

    GVector2D diagonalDir = (dpb - dpa).normalized();

    std::vector<GVector2D> diagonalPoints;
    diagonalPoints.push_back(diagonal[0]);

    for (int i = 1; i < diagonal.size(); ++i)
    {
        auto pts = meshingParams.innerSplitFunc(diagonal[i-1], diagonal[i], FFirstLastPolicy::Last);
        diagonalPoints.insert(diagonalPoints.end(), pts.begin(), pts.end());
    }

    return splitPolygonImpl(poly, data, da, db, diagonalPoints);
}

std::vector<PolygonInfo> splitPolygon(const std::vector<GVector2D>& srcPolygon, PolygonInfo& poly, GeometryData<GVector2D>& data, const MeshingParams& meshingParams)
{
    auto [da, db] = findBestDiagonal(srcPolygon, poly, data);
    auto&& dpa = data.vertices[poly.pts[da].idx];
    auto&& dpb = data.vertices[poly.pts[db].idx];

    std::vector<GVector2D> diagonalPoints = meshingParams.innerSplitFunc(dpa, dpb, FFirstLastPolicy::None);

    return splitPolygonImpl(poly, data, da, db, diagonalPoints);
}

std::tuple<GeometryData<GVector2D>, IndexType /*outer count*/>
meshPolygon2(const std::vector<GVector2D>& inPolygon, const MeshingParams& meshingParams /*= getDefaultMeshingParams()*/, std::optional<std::vector<GVector2D>> forcedDiagonal /*= {}*/)
{
    OmniProfile("Polygon meshing 2");

    // All outer vertices will be included
    Polygon2D mainPoly(meshingParams.splitOuter ? makeDetailedPolygon(inPolygon, meshingParams) : inPolygon, true);

    GeometryData<GVector2D> result;
    result.vertices = mainPoly.getPts();
    IndexType outerCount = result.vertices.size();

    // Initialize virtual polygon list
    std::vector<PolygonInfo> remainingPolygons(1);
    auto& outerPoly = remainingPolygons[0];
    auto outerPts = asCircular(result.vertices);

    // Construct first polygon
    outerPoly.pts.resize(result.vertices.size());
    for (IndexType i = 0; i < result.vertices.size(); ++i)
    {
        auto& p0 = outerPts.get(i, -1);
        auto& p1 = outerPts.get(i, 0);
        auto& p2 = outerPts.get(i, 1);

        PolygonInfo::PerVertex& v = outerPoly.pts[i];
        v.idx = i;

        GVector2D v10 = (p0 - p1).normalized();
        GVector2D v12 = (p2 - p1).normalized();
        v.angle = angle360CW(v10, v12);
        v.dir = (v12 - v10).normalized();
        v.isolation = std::min(distance(p0, p1), distance(p1, p2));
    }

#if DEBUG_NEW_TRIANGULATION
    for (int i = 0; i < outerPoly.pts.size(); ++i)
        Q_ASSERT(outerPoly.pts[i].isolation > 0.0f);
#endif

    if (forcedDiagonal)
    {
        // Pop 1 polygon
        auto polygon = std::move(remainingPolygons.back());
        remainingPolygons.pop_back();

        // Final triangle
        if (polygon.pts.size() == 3)
        {
            result.indices << polygon.pts[2].idx << polygon.pts[1].idx << polygon.pts[0].idx;
        }

        auto newPolygons = splitPolygon(polygon, result, *forcedDiagonal, meshingParams);
        for (auto&& poly : newPolygons)
            remainingPolygons.push_back(std::move(poly));
    }

    while (!remainingPolygons.empty())
    {
        // Pop 1 polygon
        auto polygon = std::move(remainingPolygons.back());
        remainingPolygons.pop_back();

        // Final triangle
        if (polygon.pts.size() == 3)
        {
            result.indices << polygon.pts[2].idx << polygon.pts[1].idx << polygon.pts[0].idx;
            continue;
        }

        auto newPolygons = splitPolygon(inPolygon, polygon, result, meshingParams);

#if DEBUG_NEW_TRIANGULATION
        for (auto& newPoly : newPolygons)
            newPoly.parent.push_back(polygon);

        if (newPolygons.empty())
        {
            while (true)
            {
                dbgPrint(polygon, result, false);

                if (polygon.parent.empty())
                    break;

                polygon = polygon.parent[0];
            }

            printSrcPolygon(inPolygon);
        }
#endif

        for (auto&& poly : newPolygons)
            remainingPolygons.push_back(std::move(poly));
    }

    return { result, outerCount };
}

Polygon2D getSegmentedPolygon(const Polygon2D& polygon)
{
    const auto& cpts = polygon.getCPts();
    std::vector<GVector2D> resPts;
    resPts.reserve(cpts.getSize() * 10);

    for (int i = 0; i < cpts.getSize(); ++i)
        resPts << splitSegment(Segment2D{cpts[i], cpts.getNext(i)}, FFirstLastPolicy::First, true);

    resPts.shrink_to_fit();
    return Polygon2D(std::move(resPts));
}

std::vector<Polygon2D> triangulatePolygonWithHole(const Polygon2D& area, const Polygon2D& hole)
{
    const auto& holePts = hole.getPts();
    const auto& areaPts = area.getPts();

    std::vector<Polygon2D> resultPolygons;
    resultPolygons.reserve(areaPts.size());

    std::vector<p2t::Point> pointsArray(areaPts.size());
    std::vector<p2t::Point*> polyline(areaPts.size());
    for (int i = 0; i < areaPts.size(); ++i)
    {
        pointsArray[i] = p2t::Point(areaPts[i].x, areaPts[i].z);
        polyline[i] = &pointsArray[i];
    }

    std::vector<p2t::Point> holeArray(holePts.size());
    std::vector<p2t::Point*> holePolyline(holePts.size());
    for (int i = 0; i < holePts.size(); ++i)
    {
        holeArray[i] = p2t::Point(holePts[i].x, holePts[i].z);
        holePolyline[i] = &holeArray[i];
    }

    p2t::CDT cdt(std::move(polyline));
    cdt.AddHole(std::move(holePolyline));
    cdt.Triangulate();

    const auto triangles = cdt.GetTriangles();
    for (p2t::Triangle* triangle : triangles)
    {
        std::vector<GVector2D> res(3);
        for (int i = 0; i < 3; ++i)
        {
            const p2t::Point* trPt = triangle->GetPoint(i);
            res[i] = GVector2D(trPt->x, trPt->y);
        }
        resultPolygons <<= Polygon2D(std::move(res));
    }

    return resultPolygons;
}

std::array<std::vector<Polygon2D>, 2> splitPolygonByMultiLine(const Polygon2D& area, const std::vector<GVector2D>& line)
{
    // Find intersection points sorted by line coord
    std::map<float, GVector2D> iPts;
    auto cArea = area.getCPts();

    for (int i = 1; i < line.size(); ++i)
    {
        std::array lineSegment{ line[i - 1], line[i] };
        
        for (int p = 0; p < cArea.getSize(); ++p)
        {
            int p2 = cArea.findIdx(p, 1);
            auto [unused, intersectionPoint, d] = distance(lineSegment, { cArea[p], cArea[p2] });
            if (d > 1.0f)
                continue;

            float adv = distance(lineSegment[0], intersectionPoint) / distance(lineSegment[0], lineSegment[1]);
            adv = std::clamp(adv, 0.0f, 1.0f);

            float coord = float(i - 1) + adv;
            if (int(coord) == cArea.getSize())
                coord = 0;

            iPts[coord] = intersectionPoint;
        }
    }

    if (iPts.size() < 2)
        if (getLineSide(line, area.getCenter()) == -1)
        {
            //area.debugPlot(Colors::red, -50);
            return { std::vector{area}, {} };
        }
        else
        {
            //area.debugPlot(Colors::green);
            return { std::vector<Polygon2D>{}, {area} };
        }

    // Restructure
    std::vector<std::vector<GVector2D>> intersectionLines;
    std::vector<GVector2D> verificationPts;

    intersectionLines.reserve(iPts.size() - 1);
    verificationPts.reserve(intersectionLines.capacity());

    std::vector<GVector2D> currentLine;
    float lastCoord = -1.0f;
    for (auto& [coord, point] : iPts)
        if (currentLine.empty())
        {
            currentLine.push_back(point);
            lastCoord = coord;
        }
        else
        {
            currentLine = { currentLine.back() };
            
            // Add all line points at full indices before this point
            for (int i = std::floor(lastCoord); i < std::ceil(coord); ++i)
                if (area.contains(line[i]))
                    currentLine << line[i];

            currentLine << point;
            intersectionLines.push_back(currentLine);
            lastCoord = coord;
        }

    for (auto&& line : intersectionLines)
        verificationPts.push_back(std::lerp(line.front(), line.back(), 0.5));

    std::vector<Polygon2D> polys(1, area);
    for (int i = 0; i < intersectionLines.size(); ++i)
    {
        // Find poly containing verification point and split it
        int polyToSplit = -1;
        for (int polyIdx = 0; polyIdx < polys.size(); ++polyIdx)
        {
            if (polys[polyIdx].contains(verificationPts[i]))
            {
                polyToSplit = polyIdx;
                break;
            }
        }

        if (polyToSplit == -1)
            continue;

        polys.emplace_back(Polygon2D());
        std::tie(polys[polyToSplit], polys.back()) = splitPolygonByFittedMultiLine(polys[polyToSplit], intersectionLines[i]);
        Q_ASSERT(!polys.back().getPts().empty());
    }

    std::array<std::vector<Polygon2D>, 2> results;
    for (auto&& poly : polys)
        if (getLineSide(line, poly.getCenter()) == -1)
        {
            //poly.debugPlot(Colors::red, -50);
            results[0] <<= std::move(poly);
        }
        else
        {
            //poly.debugPlot(Colors::green);
            results[1] <<= std::move(poly);
        }

    return results;
}

std::optional<std::array<int, 2>> findSegment(const Polygon2D& poly, const GVector2D& p)
{
    std::map<float, std::array<int, 2>> results;

    auto cPts = poly.getCPts();
    for (int i = 0; i < cPts.getSize(); ++i)
    {
        int i2 = cPts.findIdx(i, 1);
        auto [closestPoint, d, idx] = directionalBoundDistance({ cPts[i], cPts[i2] }, p);
        results[d] = { i, i2 };
    }

    if (results.begin()->first < 1.0f)
        return results.begin()->second;

    return {};
}

std::tuple<Polygon2D, Polygon2D> splitPolygonByFittedMultiLine(const Polygon2D& area, const std::vector<GVector2D>& line)
{
    auto segment1 = *findSegment(area, line.front());
    auto segment2 = *findSegment(area, line.back());

    auto cPts = area.getCPts();

    if (cPts.closerDir(segment1.front(), segment1.back()) == -1)
        std::swap(segment1.front(), segment1.back());

    if (cPts.closerDir(segment2.front(), segment2.back()) == -1)
        std::swap(segment2.front(), segment2.back());

    std::vector<GVector2D> pts1, pts2;

    cPts.forRangeCCW(segment2.front(), segment1.back(), [&](int i) { pts1 << cPts[i]; });
    if (!pts1.empty() && pts1.back() == line.front())
        pts1.pop_back();
    if (!pts1.empty() && pts1.front() == line.back())
        pts1.erase(pts1.begin());
    pts1 << line;

    cPts.forRangeCW(segment2.back(), segment1.front(), [&](int i) { pts2 << cPts[i]; });
    if (!pts2.empty() && pts2.back() == line.front())
        pts2.pop_back();
    if (!pts2.empty() && pts2.front() == line.back())
        pts2.erase(pts2.begin());
    pts2 << line;

    return { pts1, pts2 };
}


std::tuple<Polygon2D, Polygon2D> splitPolygonByPointFittedMultiLine(const Polygon2D& area, const std::vector<GVector2D>& line)
{
    int p0 = indexOf(area.getPts(), line.front());
    int p1 = indexOf(area.getPts(), line.back());
    if (p0 == -1 || p1 == -1)
    {
        //area.debugPlot();
        //spawn<DLineMarker>(line, Colors::red);
        return {};
    }

    std::vector<GVector2D> ptsCW = line; 
    std::vector<GVector2D> ptsCCW = line; 
    auto cPts = area.getCPts();

    for (int i = cPts.findIdx(p1, 1); i != p0; i = cPts.findIdx(i, 1))
        ptsCW << cPts[i];

    for (int i = cPts.findIdx(p1, -1); i != p0; i = cPts.findIdx(i, -1))
        ptsCCW << cPts[i];

    return { Polygon2D(ptsCW, true), Polygon2D(ptsCCW, true) };
}

std::tuple<const GVector2D&, const GVector2D&> getEdgeBetweenNeighbors(int n1, int n2)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();

    auto&& cell = diagram->getCellAt(n1);
    auto&& vertexIndices = cell.getNeighbors()[n2];
    auto&& pts = *cell;
    return { pts[vertexIndices[0]], pts[vertexIndices[1]] };
}

// Multi-purpose noise geter function
// There is a different noise pattern for each Usage
double getGlobalNoiseValue(float x, float z, ENoiseUsage nu)
{

    static std::mutex guard;
    static std::map<ENoiseUsage, noise::module::Perlin> noiseGens;
    if (std::scoped_lock lock(guard); !noiseGens.contains(nu))
    {
        auto&& source = noiseGens[nu];
        source.SetSeed(gRandomEngine());
        source.SetFrequency(1.0f / gMinTriangleSideLength);
    }

    return std::clamp(noiseGens[nu].GetValue(x, 0.0, z), 0.0, 1.0);
}

static void addTriangle(std::vector<IndexType>& indices, IndexType i1, IndexType i2, IndexType i3, bool inverse_direction)
{
    if (inverse_direction)
        indices << i1 << i3 << i2; // cache friendly
    else
        indices << i1 << i2 << i3;
}
