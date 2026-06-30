#include "stdafx.h"

#include "TerrainBlockFlatland.h"
#include "Omnigen.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/OmnigenGeneration.h"

namespace Generation
{
    float Generation::TerrainBlockCluster<ETerrainBlock::Flatland>::chance(const BlockChanceData& data)
    {
        if (data.steepness < 0.25)
            return std::clamp(0.25 - data.steepness, 0.0, 1.0);

        return 0.0f;
    }

    void TerrainBlockCluster<ETerrainBlock::Flatland>::initialize()
    {
		smoothingParams.useNoise = true;
		smoothingParams.noiseFrequency = 0.2f;
		smoothingParams.noiseAmplitude = 0.5f;
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> TerrainBlockCluster<ETerrainBlock::Flatland>::generateMesh()
    {
        auto&& area = calculatePolygon();
        auto [geom, unused] = meshPolygon2(area.getPts());
        auto& verts = geom.vertices;
        auto& indices = geom.indices;

        static hybrid_int_distribution<int> patchCountDist(0, 5, 0.1, 0.1);
        int patchCount = patchCountDist(gRandomEngine);
        std::vector<N_Ellipse> patches;

        for (int pc = 0; pc < patchCount; ++pc)
        {
            static std::uniform_int_distribution focusCountDist(3, 4);
            const int focusCount = focusCountDist(gRandomEngine);

            // Generate random foci from barycentric coords.
            std::vector<std::vector<float>> fociCoords(focusCount);
            std::vector<GVector2D> foci;
            float coordsSum;

            for (int i = 0; i < focusCount; ++i)
            {
                coordsSum = 0.0f;
                for (auto&& p : area)
                {
                    fociCoords[i] << randomChance();
                    coordsSum += fociCoords[i].back();
                }

                foci.emplace_back(GVector2D());
                for (int pIdx = 0; pIdx < area.getPts().size(); ++pIdx)
                    foci.back() += (fociCoords[i][pIdx] / coordsSum) * area[pIdx];
            }

            // Random radius
            static hybrid_int_distribution<int> radiusDist(100, 1000, 0.1, 0.2);
            patches <<= N_Ellipse(foci, radiusDist(gRandomEngine));
        }

        GeometryData<TerrainMeshVertex> geometry;
        geometry.vertices.reserve(verts.size());
        for (auto&& v : verts)
        {
            // Noise effect = [-1m..1m]
            float h = height;
            if (!(biomeDomain && biomeDomain->getData<EDomainType::Biome>()->humidity == EHumidity::Desert))
            {
                GVector2D pc = { v.x / getMaxGridCoord(), v.z / getMaxGridCoord() };
                h += getGlobalNoiseValue(pc.x, pc.z, ENoiseUsage::TerrainHeight) * 200.0f;
            }

            TerrainMeshVertex finalPoint = { {v.x, h, v.z}, {}, *this };

            if (randomChance() < 0.9) // randomly exclude some points from patches
                for(auto&& patch : patches)
                    if (patch.contains(v))
                    {
                        setPackParam(&finalPoint.packParams, 0, 0.5f);
                        break;
                    }

            geometry.vertices << finalPoint;
        }

        geometry.indices = std::move(indices);
        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    void TerrainBlockCluster<ETerrainBlock::Flatland>::findBiomeDomain()
    {
        auto&& cells = Data::get()->getTerrainCells()->getCells();
        auto&& centerPoint = cells[keyCell]->getCenter().toGPoint();
        biomeDomain = Data::get()->getDomainAtSquare(centerPoint, EDomainType::Biome);
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Flatland>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        if (averageIHLevel < 5)
            setPackParam(&packParams, 0, 0.5f);
        else
            setPackParam(&packParams, 0, 1.0f);

        setPackParam(&packParams, 1, 1.0f);
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Flatland>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
    omniBin << object.height;
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Flatland>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
    omniBin >> object.height;
    object.findBiomeDomain();
}

