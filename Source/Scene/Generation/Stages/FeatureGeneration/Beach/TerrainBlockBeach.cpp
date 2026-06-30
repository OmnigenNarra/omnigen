#include "stdafx.h"

#include "TerrainBlockBeach.h"
#include "Omnigen.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/Landmasses/StageGeneration_Landmasses.h"
#include "Utils/Interpolation.h"

namespace Generation
{
    using BeachCluster = TerrainBlockCluster<ETerrainBlock::Beach>;

    float BeachCluster::chance(const BlockChanceData& data)
    {
        if (data.isWithinShoreDist && data.maxH < 100.f)
            return 1.0f;

        return 0.0f;
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> BeachCluster::generateMesh()
    {
        Polygon2D poly = calculatePolygon();

        auto [geom2D, unused] = meshPolygon2(poly.getPts());
        auto& verts = geom2D.vertices;
        auto& indices = geom2D.indices;
        
        GeometryData<TerrainMeshVertex> geometry;
        geometry.vertices.reserve(verts.size());

        for (auto&& vert : verts)
        {
            float dist = shorelineDistance(vert);
            auto [height, angle] = getPointHeightByDistance(vert, dist);

            TerrainMeshVertex finalPoint = { {vert.x, height, vert.z}, {}, *this };
            geometry.vertices << finalPoint;
        }

        geometry.indices = std::move(indices);
        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    // distance to shoreline with sign determining the side on which the point lies
    float BeachCluster::shorelineDistance(const GVector2D& point) const
    {
        auto&& qtree = Data::get()->getMarkerQuadTree<DShorelineMarker>();
        float lookupDistance = 2 * calculatePolygon().getRadius();
        auto nodes = qtree.find_all_nearest(point.x, point.z, lookupDistance);

        float minD = std::numeric_limits<float>::max();
        LineMarkerPoint closestPoint;

        for (auto&& node : nodes)
        {
            GVector2D p(node->x, node->y);
            if (float d = distance(point, p); d < minD)
            {
                minD = d;
                closestPoint = node->data;
            }
        }

        int sign = getLineSide(closestPoint, point);
        return minD * sign;
    }

    // calculates height and places point at (x, z) with precaltulated distance to the shoreline = dist
    std::tuple<float, float> BeachCluster::getPointHeightByDistance(const GVector2D& point, float dist) const
    {
        auto&& dem = Data::get()->getDEM();

        // 4 layers based on distance to shoreline:
        // < 0           -> under water
        // 0 < x < 5m    -> beach face
        // 5m < x < 7m   -> wrack line
        // 7m < x        -> berm

        float height = 0.0f;
        float hpd = 0.0f; // delta height per unit distance

        if (dist < 0) // under water
        { 
            float hpd = 0.025f;
            float t = 0; // smoothstep(-dist / 10000.0f);
            float heightBeach = dist * hpd;
            float heightDem = dem->heightData.sample(point);
            height = std::lerp(heightBeach, heightDem, t);
        }
        else if (dist < 500.0f) // beach face
        { 
            hpd = 0.075;
            height = dist * hpd;
        }
        else if (dist < 700.0f) // wrack line
        { 
            hpd = 0.12f;
            height = 0.075f * 500.0f + hpd * (dist - 500.0f);
        }
        else // berm
        { 
            static const Interpolation::Technique01<EInterpolation01::Smoothstep> smoothstep;
            hpd = 0.045f;
            float t = smoothstep((dist - 700.0f) / 5000.0f);
            float heightBeach = 0.075f * 500.0f + 0.12f * 200.0f + hpd * (dist - 700.0f);
            float heightDem = dem->heightData.sample(point);
            height = std::lerp(heightBeach, heightDem, t);
        }

        return { height, std::atan2(hpd, 1) * 180.0f / std::numbers::pi };
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Beach>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        // 100% soil, no vegetation
        packParams = 255;
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Beach>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Beach>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
}

