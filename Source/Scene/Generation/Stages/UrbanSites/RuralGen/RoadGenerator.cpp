#include "stdafx.h"
#include "RoadGenerator.h"

#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"


void RoadPainter::paintRoad(const QSharedPointer<DRoadMarker>& road)
{
    const auto mods = road->getVertexModifications();
    for (auto&& [v, weight] : mods)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& cluster = clusterMap[v.first];
        auto&& vertex = cluster->section->mainBuffer->vertices[v.second];

        if (paintMap.contains(v))
            if (paintMap[v].second >= weight)
                continue;

        paintMap[v] = paintVertex(&vertex, weight);
    }
}

void RoadPainter::revertRoadPainting()
{
    auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
    for (auto&& [vertex, values] : paintMap)
    {
        auto&& cluster = clusterMap[vertex.first];
        auto&& v = cluster->section->mainBuffer->vertices[vertex.second];

        v.packParams = Generation::compilePackParams({
            values.first,
            Generation::getPackParam(v.packParams, 1),
            0.f,
            Generation::getPackParam(v.packParams, 3) });

        //Generation::Data::get()->createMarker<DLineMarker>(v.pos, 5'000.f, Colors::red);
    }

    paintMap.clear();
}

std::pair<float, float> RoadPainter::paintVertex(TerrainMeshVertex* v, const float weight) const
{
    

    const float previousPackParam = Generation::getPackParam(v->packParams, 0);

    //TODO: Support negative values
    Generation::setPackParam(&v->packParams, 0, std::clamp(previousPackParam + weight, 0.f, 1.f));
    Generation::setPackParam(&v->packParams, 1, 0.0f);
    Generation::setPackParam(&v->packParams, 2, weight);
    Generation::setPackParam(&v->packParams, 3, 1.f);

    return std::make_pair(previousPackParam, weight);
}

LotExtractor::LotExtractor(const std::vector<Polygon2D>& inBlocks, const float desiredLotSize)
    : blocks(inBlocks), lotSize(desiredLotSize)
{

}

std::vector<BuildingLotInfo> LotExtractor::getLots() const
{
    std::vector<BuildingLotInfo> vecToReturn;
    for (auto&& block : blocks)
    {
        vecToReturn << extract(block);
    }

    return vecToReturn;
}

std::vector<BuildingLotInfo> LotExtractor::extract(const Polygon2D& inBlock) const
{
    if (inBlock.getArea() <= lotSize)
        return { { inBlock, inBlock, false, false } };

    std::vector<GVector2D> internalSeeds;

    auto&& enclosingBB = inBlock.getEnclosingBB();

    const float minZ = enclosingBB.nbl.z() - enclosingBB.sizes.z() * 1.5;
    const float minX = enclosingBB.nbl.x() - enclosingBB.sizes.x() * 1.5;
    const float maxZ = enclosingBB.nbl.z() + enclosingBB.sizes.z() * 1.5;
    const float maxX = enclosingBB.nbl.x() + enclosingBB.sizes.x() * 1.5;

    const float angleToPoint = GVector2D{ 0, -1 }.angle(inBlock.getCenter());

    GVector2D currentPos{ minX, minZ };

    std::mt19937& eng = Generation::gRandomEngine;

    const float densityX = lotSize;
    while (true)
    {
        std::uniform_real_distribution<float> gridDistribution(lotSize * 0.85f, lotSize * 1.15f);

        const float densityZ = gridDistribution(eng);

        if (currentPos.z > maxZ)
            break;

        while (currentPos.x < maxX)
        {
            const auto pt = inBlock.getCenter() +
                GVector2D::rotateDegrees({ inBlock.getCenter() - currentPos }, angleToPoint);

            if (inBlock.containsConcave(pt, false) && !contains(internalSeeds, pt))
                internalSeeds.emplace_back(pt);

            currentPos.x += densityX;
        }

        currentPos.x = minX;

        currentPos.z += densityZ;
    }

    if (internalSeeds.empty())
        return { { inBlock, inBlock, false, false } };

    const Voronoi::BoxDiagram diagram = Voronoi::BoxDiagram(internalSeeds, enclosingBB);

    std::vector<BuildingLotInfo> lots;

    for (auto&& poly : diagram.clipDiagramToPolygon(inBlock))
    {
        if (poly)
        {
            bool isEnclosed = true;

            for (auto&& pt : *poly)
            {
                if (inBlock.findPointOnIndexedEdge(pt))
                {
                    lots << BuildingLotInfo{ *poly, inBlock, false, true };
                    isEnclosed = false;
                    break;
                }
            }

            if (isEnclosed)
                lots << BuildingLotInfo{ *poly, inBlock, true, true };
        }
    }

    return lots;
}

GVector2D LotExtractor::getDeviatedMidpoint(const Segment2D& forEdge)
{
    std::mt19937& eng = Generation::gRandomEngine;
    std::uniform_real_distribution<float> deviation(0.45f, 0.65f);

    GVector2D vec = forEdge.second - forEdge.first;

    vec = vec * deviation(eng);

    return forEdge.first + vec;
}
