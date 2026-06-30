#include "stdafx.h"
#include "RoadMarker.h"

#include <Mathematics/Vector2.h>

#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/UrbanSites/RuralGen/RoadGenerator.h"

DRoadMarker::DRoadMarker(const std::vector<QVector3D>& inControlPoints, Segment2D inEdge)
    : DLineMarker(inControlPoints, Colors::green, false, 60.0f), parentEdge(std::move(inEdge))
{

    auto [l, r] = getGeometryBounds();

    if (l.size() <= 1 || r.size() <= 1)
        return;

    if (r.size() < 3)
    {
        std::vector<GVector2D> newR;
        newR << r[0];
        newR << Segment2D(r[0], r[1]).midpoint();
        newR << r[1];

        r = newR;
    }

    if (l.size() < 3)
    {
        std::vector<GVector2D> newL;
        newL << l[0];
        newL << Segment2D(l[0], l[1]).midpoint();
        newL << l[1];

        l = newL;
    }

    splines = std::make_pair(getCurve(l), getCurve(r));
    lines = std::make_pair(sampleNurbs(splines.first, imax), sampleNurbs(splines.second, imax));

    std::vector<GVector2D> pts;
    pts.insert(pts.end(), lines.first.begin(), lines.first.end());
    pts.insert(pts.end(), lines.second.rbegin(), lines.second.rend());

    bounds = Polygon2D(pts);

    const auto bb = bounds.getEnclosingBB();

    boundQTree = std::make_shared<tml::qtree<float, IndexType>>(bb.nbl.x(),
        bb.nbl.z() + bb.sizes.z(), bb.nbl.x() + bb.sizes.x(), bb.nbl.z());

    for (auto i = 0; i < lines.first.size(); i++)
        boundQTree->add_node(lines.first[i].x(), lines.first[i].z(), i);
    for (auto i = 0; i < lines.second.size(); i++)
        boundQTree->add_node(lines.second[i].x(), lines.second[i].z(), i);

    nurbsMarkersIds[0] = Generation::Data::get()->createMarker<DLineMarker>(lines.first, Colors::yellow)->getGuid();
    nurbsMarkersIds[1] = Generation::Data::get()->createMarker<DLineMarker>(lines.second, Colors::yellow)->getGuid();

    cacheTerrainGeometry();
}

DRoadMarker::~DRoadMarker()
{
    Generation::Data::get()->clearSingleExactMarker<DLineMarker>(nurbsMarkersIds[0]);
    Generation::Data::get()->clearSingleExactMarker<DLineMarker>(nurbsMarkersIds[1]);
}

float DRoadMarker::getNodeHeightAverage()
{
    auto&& vertices = getControlPoints();

    if (computedAverageHeightsNum != -1 && computedAverageHeightsNum == vertices.size())
        return heightAverage;

    float heights = 0.f;
    for (auto&& pt : vertices)
        heights += pt.y();

    computedAverageHeightsNum = vertices.size();
    heightAverage = (heights / vertices.size());

    return heightAverage;
}

std::vector<std::pair<TerrainVertexData, float>> DRoadMarker::getVertexModifications() const
{
    std::vector<std::pair<TerrainVertexData, float>> vToReturn;

    for (auto&& v : terrainVs)
    {
        vToReturn << std::make_pair(v, getVertexWeight(v).second);
    }

    return vToReturn;
}

std::vector<QVector3D> DRoadMarker::sampleNurbs(const RoadNurbs& spline, const int sampleAmount)
{
    std::vector<QVector3D> pts;
    float t;
    gte::Vector2<float> position;

    GVector2D lastPosition{ -1, -1 };
    float lastY = 0.f;
    constexpr float minDistanceForHeightQuery = 100.f;

    auto&& startPos = spline->GetControl(0);
    pts << UrbanUtils::heightQuery(GVector2D(startPos[0], startPos[1]));

    float const invIMax = 1.0f / static_cast<float>(sampleAmount);
    for (int i = 0; i <= sampleAmount; ++i)
    {
        t = static_cast<float>(i) * invIMax;

        if (lastPosition.x < 0 || qAbs((GVector2D(position[0], position[1]) - lastPosition).length()) > minDistanceForHeightQuery)
        {
            position = spline->GetPosition(t);
            pts << UrbanUtils::heightQuery(GVector2D(position[0], position[1]));

            lastPosition = GVector2D(position[0], position[1]);
            lastY = pts.back().y();
        }
        else
        {
            position = spline->GetPosition(t);
            pts << QVector3D(position[0], lastY, position[1]);

            lastPosition = GVector2D(position[0], position[1]);
        }
    }

    auto&& endPos = spline->GetControl(spline->GetNumControls() - 1);
    pts << UrbanUtils::heightQuery(GVector2D(endPos[0], endPos[1]));

    return pts;
}

QVector3D DRoadMarker::getClosestPoint(const QVector3D& fromPt) const
{
    float maxDist = std::numeric_limits<float>::max();
    int closestPtIdx = -1;
    auto&& nodesArray = getControlPoints();

    for (auto i = 0; i < nodesArray.size(); i++)
    {
        auto&& v = nodesArray[i];
        if (const auto d = distanceSquared2D(v, fromPt); d < maxDist)
        {
            maxDist = d;
            closestPtIdx = i;
        }
    }

    return getControlPoints()[closestPtIdx];
}

void DRoadMarker::cacheTerrainGeometry()
{
    OmniProfile("Road Geometry Caching", true);

    static std::mutex insertGuard;

    auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

    auto&& roadSize = UrbanUtils::getRoadWidthFromEnum(desiredWidth) + 1.f;
    auto&& pts = getControlPoints();

    for (auto&& pt : pts)
    {
        std::unordered_map<int, std::unordered_set<IndexType>> geometryPoints;

        auto&& blockTree = Generation::Data::get()->getBlockQuadTree();
        auto&& clusters = Generation::Data::get()->getTerrainClustersMap();

        std::unordered_set<int> results;
        float r = 200.f;
        float maxR = Generation::Data::get()->getLargestVoronoiCellRadius();
        while (true)
        {
            auto node = Generation::Data::get()->getBlockQuadTree()->find_nearest(
                pt.x(), pt.z(), r);

            if (node)
                results.insert(node->data);

            if (!results.empty() || r > maxR)
                break;

            r *= 2.f;
        }

        for (auto&& blockId : results)
        {
            auto&& cluster = clusterMap[blockId];
            auto vertices = cluster->section->getVertices();
            auto&& foundGeometryPoints = cluster->getVertexQuadTree().find_all_nearest(pt.x(), pt.z(), roadSize);

            for (auto&& vertex : foundGeometryPoints)
            {
                if (getBounds().containsConcave(vertices[vertex->data].position))
                {
                    spawn<DLineMarker>(vertices[vertex->data].position, 4'000.f, Colors::red);
                    terrainVs << TerrainVertexData(cluster->keyCell, vertex->data);
                }
                    
            }
        }
    }
}

std::tuple<std::vector<GVector2D>, std::vector<GVector2D>> DRoadMarker::getGeometryBounds() const
{
    auto&& pts = getControlPoints();
    return UrbanUtils::getPointsAtOffset(std::vector<GVector2D>(pts.begin(), pts.end()), desiredWidth);
}

RoadNurbs DRoadMarker::getCurve(const std::vector<GVector2D>& pts) const
{
    std::vector<gte::Vector2<float>> cp;
    for (auto&& p : pts)
        cp.push_back({ p.x, p.z });

    std::vector<float> weights(cp.size());
    std::ranges::fill(weights, 1.f);

    gte::BasisFunctionInput<float> input1;
    input1.numControls = cp.size();
    input1.degree = 2;
    input1.uniform = true;
    input1.periodic = true;
    input1.numUniqueKnots = input1.numControls + input1.degree + 1;
    input1.uniqueKnots.resize(input1.numUniqueKnots);
    float invNmD = 1.0f / static_cast<float>(input1.numControls - input1.degree);
    for (int i = 0; i < input1.numUniqueKnots; ++i)
    {
        input1.uniqueKnots[i].t = static_cast<float>(i - input1.degree) * invNmD;
        input1.uniqueKnots[i].multiplicity = 1;
    }

    return std::make_shared<gte::NURBSCurve<2, float>>(input1, cp.data(), weights.data());
}

std::pair<TerrainVertexData, float> DRoadMarker::getVertexWeight(const TerrainVertexData& vert) const
{
    return { std::make_pair(vert, 1.f) };

    auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
    auto&& cluster = clusterMap[vert.first];
    auto vertices = cluster->section->getVertices();
    auto&& vertex = vertices[vert.second];

    auto&& pos = vertex.position;

    const auto closestPt = boundQTree->find_nearest(pos.x(), pos.z(), 600.f);
    if (!closestPt)
    {
        Generation::Data::get()->createMarker<DLineMarker>(pos, 10'000.f, Colors::azure);
        return { std::make_pair(vert, 0.f) };
    }
    //Q_ASSERT(closestPt);

    const Segment2D lineSeg = Segment2D(lines.first[closestPt->data], lines.second[closestPt->data]);
    const auto closestPtInSeg = lineSeg.closestPoint(pos);

    const float maxLength = lineSeg.length();
    const float ptLength = Segment2D(lineSeg.first, closestPtInSeg).length();

    const float weight = ptLength / maxLength;

    return std::make_pair(vert, weight);
}
