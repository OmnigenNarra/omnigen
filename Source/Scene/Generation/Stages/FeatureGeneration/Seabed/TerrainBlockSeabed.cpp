#include "stdafx.h"

#include "TerrainBlockSeabed.h"
#include "Omnigen.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Utils/Interpolation.h"

namespace Generation
{
    using SeabedCluster = TerrainBlockCluster<ETerrainBlock::Seabed>;

    float SeabedCluster::chance(const BlockChanceData& data)
    {
        if (!data.isWithinShoreDist && data.isWaterSideOfShore)
            return 10000.0f;

        return 0.0f;
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> SeabedCluster::generateMesh()
    {
        auto [geom2D, unused] = meshPolygon2(calculatePolygon().getPts());
        auto& verts = geom2D.vertices;
        auto& indices = geom2D.indices;

        GeometryData<TerrainMeshVertex> geometry;
        geometry.vertices.reserve(verts.size());

        for (auto&& vert : verts)
        {
            TerrainMeshVertex finalPoint = { {vert.x, -1000.0f, vert.z}, {}, *this };
            geometry.vertices <<= std::move(finalPoint);
        }

        geometry.indices = std::move(indices);
        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Seabed>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        TerrainBlockMetaClusterBase::computePackParams(lithoCluster, biomeDomain, averageIHLevel);

        // No vegetation
        setPackParam(&packParams, 1, 0.0f);
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Seabed>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Seabed>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
}
