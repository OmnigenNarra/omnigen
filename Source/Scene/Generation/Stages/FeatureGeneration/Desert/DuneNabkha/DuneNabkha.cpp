#include "stdafx.h"
#include "DuneNabkha.h"
#include "../DuneGraph.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

namespace Generation
{
    DesertClusterSubData<EDesertBlockSubtype::DuneNabkha>::DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData)
        : DesertClusterSubDataBase(inBaseData)
    {

    }

    std::unordered_set<int> DesertClusterSubData<EDesertBlockSubtype::DuneNabkha>::customGrow(const std::unordered_set<int>& candidates)
    {
        // Expand only once
        if (baseData->cells.size() > 1)
            return {};

        auto&& diagram = Data::get()->getTerrainCells();
        auto&& dem = Data::get()->getDEM();

        const auto& clusterCell = diagram->getCellAt(*baseData->cells.begin());
        auto clusterCellPoly = clusterCell.getPolygon();
        auto&& points = clusterCellPoly.getPts();

        std::unordered_set<int> newCandidates;
        for (int nid : candidates)
        {
            auto&& neighborPoly = diagram->getCellAt(nid).getPolygon();
            auto closestEdges = clusterCellPoly.getClosestEdges(neighborPoly.getCenter());

            for (auto&& edge : closestEdges)
            {
                auto&& [a, b, c] = edge;
                float size = (clusterCellPoly.getArea() / Segment2D(points[a], points[b]).length());
                if (size < 500.f)
                    newCandidates += nid;
            }
        }

        baseData->cells += newCandidates;
        return newCandidates;
    }

    QSharedPointer<DesertSubClusterBase> DesertClusterSubData<EDesertBlockSubtype::DuneNabkha>::createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster)
    {
        return QSharedPointer<DesertSubCluster<EDesertBlockSubtype::DuneNabkha>>::create(cluster);
    }

    static float getRandomValueInRange(float a, float b)
    {
        std::uniform_real_distribution<> distr(a, b);
        return distr(Generation::gRandomEngine);
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneNabkha>::generate()
    {
        auto&& dem = Data::get()->getDEM();
        auto&& diagram = Data::get()->getTerrainCells();

        const Polygon2D clusterPolygon = Utils::makeBoundingPolygon(cluster->cells).front();
        const auto& coreCell = diagram->getCellAt(cluster->keyCell);

        const GVector2D coreCenter = coreCell.getVoronoiCenter();
        const GVector2D polygonCenter = clusterPolygon.getCenter();

        // First step - find circle inside polygon, where we can freely place points without intersecting
        const float radiusFromPolygonCenter = clusterPolygon.contains(polygonCenter) ? clusterPolygon.getRadiusOfInscribedCircleAtPoint(polygonCenter) : 0.f;
        const float radiusFromCoreCell = clusterPolygon.getRadiusOfInscribedCircleAtPoint(coreCenter);
        const float radius = 0.95f * ((radiusFromCoreCell > radiusFromPolygonCenter) ? radiusFromCoreCell : radiusFromPolygonCenter);
        const GVector2D& center = (radiusFromCoreCell > radiusFromPolygonCenter) ? coreCenter : polygonCenter;

        GeometryData<TerrainMeshVertex> geometry;
        auto [geom2D, unused] = meshPolygon2(clusterPolygon.getPts());
        auto& verts = geom2D.vertices;
        auto& indices = geom2D.indices;

        geometry.vertices.reserve(verts.size());

        static std::uniform_int_distribution focusCountDist(2, 5);
        const int focusCount = focusCountDist(gRandomEngine);

        // Generate random foci from barycentric coords.
        std::vector<std::vector<float>> fociCoords(focusCount);
        std::vector<GVector2D> foci;
        float coordsSum;

        for (int i = 0; i < focusCount; ++i)
        {
            coordsSum = 0.0f;
            for (auto&& p : clusterPolygon)
            {
                fociCoords[i] << randomChance();
                coordsSum += fociCoords[i].back();
            }

            foci.emplace_back(GVector2D());
            for (int pIdx = 0; pIdx < clusterPolygon.getPts().size(); ++pIdx)
                foci.back() += (fociCoords[i][pIdx] / coordsSum) * clusterPolygon[pIdx];
        }

        // For each point of the Nabkha ridgeline assign a random max radius
        std::vector<float> maxRadiusMap(foci.size());
        for (int i = 0; i < foci.size(); ++i)
            maxRadiusMap[i] = (getRandomValueInRange(radius / 2.0f, radius));

        // Nabkhas are usually up to 4 meters tall
        float maxHeight = std::min(getRandomValueInRange(200.0f, 400.0f), radius / 2);

        for (auto&& vert : verts)
        {
            float height = dem->heightData.sample(vert);

            if (auto&& [unused, d, idx] = directionalBoundDistance(foci, vert); d <= maxRadiusMap[idx])
                height += maxHeight * (1.0f - std::clamp(((d) / maxRadiusMap[idx]), 0.0f, getRandomValueInRange(0.85f, 0.95f)));

            TerrainMeshVertex finalPoint = { {vert.x, height, vert.z}, {}, *cluster };
            geometry.vertices << finalPoint;
        }

        geometry.indices = std::move(indices);
        cluster->section = spawnBatched(std::move(geometry), cluster->makeBatchParams());
    }

    DesertSubCluster<EDesertBlockSubtype::DuneNabkha>::DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster)
        : DesertSubClusterBase(inCluster, EDesertBlockSubtype::DuneNabkha)
    {
    }
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneNabkha>& object, OmniBin<std::ios::out>& omniBin)
{

}

void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneNabkha>& object, OmniBin<std::ios::in>& omniBin)
{

}