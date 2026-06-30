#include "stdafx.h"
#include "Triangulation.h"
#include "delabella.h"
#include <unordered_map>
#include <functional>
#include <algorithm>
#include "Utils/poly2tri/poly2tri.h"

#define TRIANGULATION_VALIDATION 0
#define CONSTRAINT_TRIANGULATION_DEBUG 0
#define TRIANGLE_RECURSIVE_DEBUG 0

#if CONSTRAINT_TRIANGULATION_DEBUG || TRIANGLE_RECURSIVE_DEBUG
#include "Scene/Generation/OmnigenGenerationData.h"
#endif

#include "Scene/Generation/OmnigenGenerationData.h"

std::vector<IndexType> triangulate2D(const std::vector<GVector2D>& pts, const Polygon2D& bounds)
{
    static const float oneThird = 1.0 / 3.0;
    bool hasBounds = !bounds.getPts().empty();

    auto* idb = IDelaBella::Create();
    int verts = idb->Triangulate(pts.size(), &pts[0].x, &pts[0].z, sizeof(GVector2D));

    // Width = 0
    if (verts < 0)
        return {};

    Q_ASSERT(verts > 0 && (verts % 3 == 0));

    std::vector<IndexType> triangles;
    triangles.reserve(verts);

    int faces = verts / 3;
    auto* faceIt = idb->GetFirstDelaunayTriangle();
    
    for (int fIdx = 0; fIdx < faces; ++fIdx, faceIt = faceIt->next)
    {
        if (hasBounds)
        {
            GVector2D center;
            for (int vIdx = 2; vIdx >= 0; --vIdx)
                center += pts[faceIt->v[vIdx]->i];

            if (!bounds.contains(center * oneThird))
                continue;
        }

        for (int vIdx = 2; vIdx >= 0; --vIdx)
            triangles << faceIt->v[vIdx]->i;
    }

    idb->Destroy();

    triangles.shrink_to_fit();
    return std::move(triangles);
}

using triangulation::Edge;

struct TriangleInd: public std::array<IndexType, 3>
{
    IndexType getRemainingIndex(IndexType i1, IndexType i2)
    {
        std::unordered_set<IndexType> indices{at(0), at(1), at(2)};
        indices.erase(i1);
        indices.erase(i2);
        return *indices.begin();
    }

    IndexType getRemainingIndex(const Edge& edge)
    {
        return getRemainingIndex(edge.start, edge.end);
    }
};


static float trianglePerimeter(float a, float b, float c)
{
    return a + b + c;
}


static float triangleArea(float a, float b, float c, bool returnSquared = false)
{
    const float p = 0.5f * (a + b + c);
    const float areaSq = p * (p - a) * (p - b) * (p - c);
    return (returnSquared ? areaSq : sqrtf(areaSq));
}

// Rating of triangle, where the most perfect has rating 1.0 - it's equilateral
// And the worst is degenerated triangle - 0.0
static float getTriangleRating(float a, float b, float c)
{
    constexpr float maxRatio = 0.04811252243f; // equilateral case sqrt(3) / 36
    return triangleArea(a, b, c) / (trianglePerimeter(a, b, c) * trianglePerimeter(a, b, c) * maxRatio);
}


static void refineTriangulation(std::vector<TriangleInd>& triangles, const std::vector<GVector2D>& pts)
{
    std::vector<IndexType> result;
    result.reserve(triangles.size() * 3);

    std::unordered_map<Edge, IndexType> allEdgesToTriangleMap;
    std::unordered_map<Edge, std::array<IndexType, 2>> innerEdgesToTrianglesMap;
    allEdgesToTriangleMap.reserve(triangles.size() * 3);
    innerEdgesToTrianglesMap.reserve(triangles.size());

    // Collect all inner edges ( diagonals in polygon) and links to triangles
    for (IndexType i = 0; i < triangles.size(); ++i)
    {
        const auto& triangle = triangles[i];
        for (IndexType j = 0; j < 3; ++j)
        {
            IndexType next = j == 2 ? 0 : j + 1;
            const Edge edge(triangle[j], triangle[next]);
            const auto iter = allEdgesToTriangleMap.find(edge);
            // if edge is duplicated, that means that it's inner edge
            if (iter == allEdgesToTriangleMap.end())
                allEdgesToTriangleMap[edge] = i;
            else
                innerEdgesToTrianglesMap[edge] = {iter->second, i};
        }
    }

    // change diagonals if needed and possible
    for (const auto& [edge, trianglesIdxArray]: innerEdgesToTrianglesMap)
    {
        const GVector2D edgePts[2] = {pts[edge.start], pts[edge.end]};
        const float edgeLengthSq = (edgePts[0] - edgePts[1]).lengthSquared();
        TriangleInd& triangle0 = triangles[trianglesIdxArray[0]];
        TriangleInd& triangle1 = triangles[trianglesIdxArray[1]];
        const IndexType vi[2] = { triangle0.getRemainingIndex(edge), triangle1.getRemainingIndex(edge) };
        const GVector2D v[2] = { pts[triangle0.getRemainingIndex(edge)], pts[triangle1.getRemainingIndex(edge)] };
        const float otherDiagonalLengthSq = (v[0] - v[1]).lengthSquared();
        const float thresholdFactor = 1.2f;
        const Polygon2D quad(std::vector{v[0], edgePts[0], v[1], edgePts[1]});

        if (quad.isConvexPolygon())
        {
            const float quadSideLengths[4] =
            {
                (edgePts[0] - v[0]).length(),
                (v[1] - edgePts[0]).length(),
                (edgePts[1] - v[1]).length(),
                (v[0] - edgePts[1]).length()
            };

            const float currentEdgeLength = (edgePts[1] - edgePts[0]).length();
            const float otherEdgeLength  = (v[1] - v[0]).length();

            const float currentMinTriangleRating = std::min(
                getTriangleRating(quadSideLengths[0], quadSideLengths[3], currentEdgeLength),
                getTriangleRating(quadSideLengths[1], quadSideLengths[2], currentEdgeLength));

            const float otherMinTriangleRating = std::min(
                getTriangleRating(quadSideLengths[0], quadSideLengths[1], otherEdgeLength),
                getTriangleRating(quadSideLengths[2], quadSideLengths[3], otherEdgeLength));

            if (otherMinTriangleRating > currentMinTriangleRating)
            {
                // Change links to edges, because of changed triangles
                Edge edge0to1 = Edge(vi[0], edge.end);
                Edge edge1to0 = Edge(vi[1], edge.start);

                auto iter = innerEdgesToTrianglesMap.find(edge0to1);
                if (iter != innerEdgesToTrianglesMap.end())
                {
                    if (iter->second[0] == trianglesIdxArray[0])
                        iter->second[0] = trianglesIdxArray[1];
                    else
                        iter->second[1] = trianglesIdxArray[1];
                }

                iter = innerEdgesToTrianglesMap.find(edge1to0);
                if (iter != innerEdgesToTrianglesMap.end())
                {
                    if (iter->second[0] == trianglesIdxArray[1])
                        iter->second[0] = trianglesIdxArray[0];
                    else
                        iter->second[1] = trianglesIdxArray[0];
                }

                // swap diagonals in quad - change triangles with respect to switched diagonal
                // triangle0
                triangle0[0] = vi[0];
                triangle0[1] = vi[1];
                triangle0[2] = edge.start;
                // triangle1
                triangle1[0] = vi[0];
                triangle1[1] = vi[1];
                triangle1[2] = edge.end;
            }
        }
    } // for
}


std::tuple<std::vector<GVector2D>, std::vector<IndexType>> constrainedTriangulation2D(const std::vector<GVector2D>& pts, bool addCenter, bool needRefinement,
    const std::vector<GVector2D>& additionalPoints, bool needDuplicateAdditionalPoints)
{
    std::vector<GVector2D> result = pts;
    //result.insert(result.end(), additionalPoints.begin(), additionalPoints.end());
    result.reserve(result.size() + 1 + additionalPoints.size() * (needDuplicateAdditionalPoints ? 2 : 1));

    if (result.size() == 3)
        return { result, {0, 1, 2}};

    std::vector<p2t::Point> sourceLine(pts.size());
    std::vector<p2t::Point*> polyline(pts.size());
    std::unordered_map<GVector2D, IndexType> indexMap;
    std::vector<IndexType> indices;
    indexMap.reserve(result.size());
    indices.reserve(result.size() * 2);

    for (int i = 0; i < result.size(); ++i)
    {
        const auto& pt = result[i];
        sourceLine[i] = p2t::Point(pt.x, pt.z);
        polyline[i] = &sourceLine[i];
    }

    for (int i = 0; i < result.size(); ++i)
        indexMap[result[i]] = i;

    p2t::CDT cdt(std::move(polyline));

    std::vector<p2t::Point> additionalSourceLine(additionalPoints.size());
    for (int i = 0; i < additionalPoints.size(); ++i)
    {
        const auto& pt = additionalPoints[i];
        additionalSourceLine[i] = p2t::Point(pt.x, pt.z);
        cdt.AddPoint(&additionalSourceLine[i]);
    }

    p2t::Point centerPt;
    if (addCenter)
    {
        const Polygon2D polygon(pts);
        const GVector2D center = polygon.getCenter();
        if (polygon.contains(center, false))
        {
            centerPt.x = center.x;
            centerPt.y = center.z;
            cdt.AddPoint(&centerPt);
        }
    }

    cdt.Triangulate();

    auto triangles = cdt.GetTriangles();
    std::vector<TriangleInd> trianglesI;
    if (needRefinement)
        trianglesI.reserve(triangles.size());

    const auto addPointAndGetIndex = [&indexMap, &result, needDuplicateAdditionalPoints](p2t::Point* pt2t) -> int //  &duplicatedPoints,
    {
        const GVector2D pt = GVector2D(pt2t->x, pt2t->y);
        const auto iter = indexMap.find(pt);
        if (iter == indexMap.end()) // additional point
        {
            result << pt;
            if (!needDuplicateAdditionalPoints)
                indexMap[pt] = result.size() - 1;

            // debug additional points
            //spawn<DLineMarker>((QVector3D)pt, 420.f, Colors::springGreen);

            return result.size() - 1;
        }
        return iter->second;
    };

    for (p2t::Triangle* triangle: triangles)
    {
        TriangleInd currTriangle;

        currTriangle[0] = addPointAndGetIndex(triangle->GetPoint(0));
        currTriangle[1] = addPointAndGetIndex(triangle->GetPoint(1));
        currTriangle[2] = addPointAndGetIndex(triangle->GetPoint(2));

        if (needRefinement)
            trianglesI <<= std::move(currTriangle);
        else
            indices << currTriangle[0] << currTriangle[1] << currTriangle[2];
    }

    if (needRefinement)
    {
        refineTriangulation(trianglesI, pts);
        for (const auto& triangleInd: trianglesI)
            indices << triangleInd[0] << triangleInd[1] << triangleInd[2];
    }

#if CONSTRAINT_TRIANGULATION_DEBUG
    for (int i = 0; i < indices.size() / 3; ++i)
    {
        std::vector<QVector3D> qptsRes;
        qptsRes << QVector3D(pts[indices[i*3 + 0]]);
        qptsRes << QVector3D(pts[indices[i*3 + 1]]);
        qptsRes << QVector3D(pts[indices[i*3 + 2]]);

       spawn<DLineMarker>(qptsRes, Colors::red, true, 20.f);
    }
#endif

#if TRIANGULATION_VALIDATION
    std::vector<bool> validationArray(pts.size());
    std::fill(validationArray.begin(), validationArray.end(), false);
    for (IndexType index: indices)
        validationArray[index] = true;

    for (bool isVertexUsed: validationArray)
        if (!isVertexUsed)
            Q_ASSERT_X(false, "Mesh", "Triangulation Error! Vertex is not included in any result triangles");
#endif

    indices.shrink_to_fit();
    result.shrink_to_fit();
    return { result, indices };
}


std::vector<GVector2D> filterDuplicatedPoints(const std::vector<GVector2D>& pts)
{
    Q_ASSERT(pts.size() >= 3);
    std::vector<GVector2D> res;
    res.reserve(pts.size());
    res << pts.front();
    for (int i = 1; i < pts.size() - 1; ++i)
    {
        if (pts[i] != pts[i - 1])
            res << pts[i];
    }
    if (pts.back() != pts.front() && pts.back() != pts[pts.size() - 2])
        res << pts.back();
    res.shrink_to_fit();
    return std::move(res);
}


bool operator==(const triangulation::Edge& v1, const triangulation::Edge& v2)
{ 
    return  (v1.start == v2.start && v1.end == v2.end) || (v1.start == v2.end && v1.end == v2.start);
}
