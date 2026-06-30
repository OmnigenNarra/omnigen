#include "stdafx.h"

#include "SmoothSlope.h"
#include "Omnigen.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

namespace Generation
{
    float Generation::TerrainBlockCluster<ETerrainBlock::SmoothSlope>::chance(const BlockChanceData& data)
    {
        if (!data.terrainDomain)
            return 0.0f;

        const auto props = data.terrainDomain->getData<EDomainType::Terrain>();
        const float hillsSmoothness = props->hillsSmoothness;

        if (data.steepness >= 0.2f)
        {
            float smoothHillChance = hillsSmoothness * 0.01f;
            if (hillsSmoothness > 80.f)
                smoothHillChance = std::exp(hillsSmoothness * 0.1f);
            return data.steepness * smoothHillChance;
        }

        return 0.0f;
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> TerrainBlockCluster<ETerrainBlock::SmoothSlope>::generateMesh()
    {
        auto&& dem = Data::get()->getDEM();
        auto [geom2D, unused] = meshPolygon2(calculatePolygon().getPts());
        auto& verts = geom2D.vertices;
        auto& indices = geom2D.indices;

        GeometryData<TerrainMeshVertex> geometry;
        geometry.vertices.reserve(verts.size());

        for (auto& vert : verts)
            geometry.vertices <<= TerrainMeshVertex{ {vert.x, dem->heightData.sampleSmooth(vert), vert.z}, {}, *this };

        geometry.indices = std::move(indices);
        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    void TerrainBlockMetaCluster<ETerrainBlock::SmoothSlope>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        if (averageIHLevel < 5)
            setPackParam(&packParams, 0, 0.5f);
        else
            setPackParam(&packParams, 0, 1.0f);

        setPackParam(&packParams, 1, 1.0f);
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::SmoothSlope>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::SmoothSlope>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
}

