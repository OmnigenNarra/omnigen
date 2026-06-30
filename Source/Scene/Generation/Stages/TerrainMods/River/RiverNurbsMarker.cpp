#include "stdafx.h"
#include "RiverNurbsMarker.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "RiverSurfaceMarker.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"

#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/spin_mutex.h>
#include <Mathematics/IntrRay3Triangle3.h>

DRiverNurbsMarker::DRiverNurbsMarker(const std::vector<Generation::RiverRowInfo>& riverbed, bool fallsIntoSea, bool clampEnd, bool clampStart, const QVector4D& debugColor)
    : DNurbsMarker(createSurfaceFromRiver(riverbed, fallsIntoSea, clampEnd, clampStart), riverbed.size(), columnCount, debugColor)
{
    calculateRiverOrigin();
    calculateRiverBounds(riverbed);
}

std::optional<float> DRiverNurbsMarker::sampleHeight(const QVector3D& pos) const
{
    OmniProfile("Nurbs sampling");

    using namespace gte;

    auto& quads = getActiveGeometry()->indices;
    auto& verts = getActiveGeometry()->vertices;
    QVector3D rayOrigin(pos.x(), WORLD_LOWEST_Y, pos.z());

    auto h = tbb::parallel_reduce(tbb::blocked_range<int>(0, int(quads.size() / 4)), std::optional<float>{},
        [&](tbb::blocked_range<int> r, std::optional<float> running_h) -> std::optional<float>
        {
            if (running_h)
                return running_h;

            for (int i = r.begin(); i < r.end(); ++i)
            {
                int qi = i * 4;

                FIQuery<float, Ray3<float>, Triangle3<float>> query;
                auto result = query(Ray3<float>{ QtoV3(rayOrigin), { 0,1,0 } }, Triangle3<float>{ QtoV3(verts[quads[qi + 2]]), QtoV3(verts[quads[qi + 1]]), QtoV3(verts[quads[qi]]) });
                if (result.intersect)
                {
                    return result.point[1];
                }
                else
                {
                    result = query(Ray3<float>{ QtoV3(rayOrigin), { 0,1,0 } }, Triangle3<float>{ QtoV3(verts[quads[qi + 3]]), QtoV3(verts[quads[qi + 2]]), QtoV3(verts[quads[qi]]) });
                    if (result.intersect)
                    {
                        return result.point[1];
                    }
                }
            }

            return {};

        }, [](auto A, auto B) { return A ? A : B; });
        
    return h;
}

std::vector<gte::UniqueKnot<float>> calculateKnots(int degree, int CPnum, bool clampStart, bool clampEnd)
{
    using namespace gte;

    int n = degree + 1 + CPnum;
    float step = 1.0f / (n - 1);
    int start = 0, end = n - 1;

    std::vector<UniqueKnot<float>> knots;

    if (clampStart)
    {
        knots << UniqueKnot<float>{0, degree + 1};
        start += degree + 1;
    }
    if (clampEnd)
    {
        end -= degree + 1;
    }

    for (int c = start; c <= end; ++c)
    {
        float val = step * c;
        knots << UniqueKnot<float>{val, 1};
    }

    if (clampEnd)
    {
        knots << UniqueKnot<float>{1, 4};
    }

    return knots;
};

SerializableNurbs<3, float> DRiverNurbsMarker::createSurfaceFromRiver(const std::vector<Generation::RiverRowInfo>& riverbed, bool fallsIntoSea, bool clampEnd, bool clampStart)
{
    using namespace gte;

    int degU = 3;
    int degV = 3;
    int u = riverbed.size();
    int v = riverbed[0].CP.size();

    SerializableNurbs<3, float>::Input surface;
    surface.uInput = gte::BasisFunctionInput<float>(u, 3);
    surface.uInput.uniform = true;
    surface.vInput = gte::BasisFunctionInput<float>(v, 3);
    surface.vInput.uniform = false;
    surface.weights.resize(u * v);
    surface.mControls.resize(u * v);
    for (int i = 0; i < surface.weights.size(); ++i)
        surface.weights[i] = 1.0f;

    std::vector<float> knots_u;
    std::vector<float> knots_v;

    surface.uInput.uniqueKnots = calculateKnots(degU, u, clampStart, clampEnd); // true false for influents
    surface.uInput.numUniqueKnots = surface.uInput.uniqueKnots.size();

    auto waterSurface = surface;
    static float waterLevel = 0.7f;
    float lastH = fallsIntoSea ? 0.0f : std::numeric_limits<float>::lowest();

    for (int i = u - 1; i >= 0; --i)
    {
        // Find local depth
        float top = std::min(riverbed[i].CP.front().y(), riverbed[i].CP.back().y());
        float bot = std::numeric_limits<float>::max();
        for (auto&& cp : riverbed[i].CP)
            bot = std::min(bot, cp.y());

        float waterSurfaceOffsetFromTop = (top - bot) * (1.0f - waterLevel);
        float h = std::max(lastH, top - waterSurfaceOffsetFromTop);
        lastH = h;

        for (int j = 0; j < v; j++)
        {
            surface.mControls[j * u + i] = QtoV3(riverbed[i].CP[j]);
            waterSurface.mControls[j * u + i] = QtoV3(riverbed[i].CP[j]);
            waterSurface.mControls[j * u + i][1] = h;
        }
    }

    spawn<DRiverSurfaceMarker>(waterSurface, riverbed.size(), 4);
    return surface;
}

void DRiverNurbsMarker::calculateRiverOrigin()
{
    gte::Vector<3, float> p;
    nurbSurface.Evaluate(1.f / rows, 1.f / columns * (columns / 2), 0, &p);
    origin = QVector3D(p[0], p[1], p[2]);
}

void DRiverNurbsMarker::calculateRiverBounds(const std::vector<Generation::RiverRowInfo>& rowData)
{
    auto& vertices = getGeometry(ELOD::Last)->vertices;

    auto addEdgeData = [&](int edge, int finalRow)
    {
        int i = finalRow * (columns + 1) + edge * columns;
        int rowDataIdx = finalRow / 4;
        if (rowDataIdx == rowData.size() - 1)
        {
            auto v = vertices[i];
            v.setY(rowData[rowDataIdx].CP[0].y());
            nurbsEdges[edge] << v;
            //spawn<DLineMarker>(vertices[i], rowData[rowDataIdx].riverPt, QVector4D(1,1,1,1), 0, ELineDecorator::Arrow);
        }
        else
        {
            float t = float(finalRow % 4) / 4.0f;
            float riverH = std::lerp(rowData[rowDataIdx].CP[0].y(), rowData[rowDataIdx + 1].CP[0].y(), t);
            auto v = vertices[i];
            v.setY(riverH);
            nurbsEdges[edge] << v;
            //spawn<DLineMarker>(vertices[i], std::lerp(rowData[rowDataIdx].riverPt, rowData[rowDataIdx + 1].riverPt, t));
        }
    };

    for (int row = 0; row <= rows; ++row)
    {
        addEdgeData(0, row);
        addEdgeData(1, row);
    }

    std::reverse(nurbsEdges[1].begin(), nurbsEdges[1].end());

    std::vector<GVector2D> nurbsEdgePts;
    std::ranges::transform(nurbsEdges[0], std::back_inserter(nurbsEdgePts), [](auto&& p) { return GVector2D(p); });
    std::ranges::transform(nurbsEdges[1], std::back_inserter(nurbsEdgePts), [](auto&& p) { return GVector2D(p); });
    nurbsPolygon = Polygon2D(nurbsEdgePts);
}

void omniSave(const DRiverNurbsMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DNurbsMarker&>(object);
    omniBin << object.nurbsPolygon;
    omniBin << object.nurbsEdges;
    omniBin << object.origin;
}

void omniLoad(DRiverNurbsMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DNurbsMarker&>(object);
    omniBin >> object.nurbsPolygon;
    omniBin >> object.nurbsEdges;
    omniBin >> object.origin;
}
