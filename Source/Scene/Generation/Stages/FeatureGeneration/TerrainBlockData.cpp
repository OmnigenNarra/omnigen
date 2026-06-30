#include "stdafx.h"
#include "TerrainBlockData.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"

void Generation::BorderPointInfo::setFinalData(TerrainBlockClusterBase* owner, const BorderPoint& bp)
{
    Q_ASSERT(bp.intermediateData.size() >= 2);
    auto&& clusterMap = Data::get()->getTerrainClustersMap();

    // Compute weight sum for normalization

    float finalHeight = 0.0f;
    std::vector<float> finalTerrainWeights(4);
    std::vector<float> finalBiomeWeights(4);
    std::vector<float> finalPackParams(4);

    auto&& [terrainChunks, chunkBlocksMap, blockChunkMap] = Data::get()->getTerrainChunkData();
    auto&& hostChunk = terrainChunks[blockChunkMap[owner->keyCell]];

    std::unordered_map<int, float> weights; // cluster id - > weight
    weights.reserve(bp.intermediateData.size());
    const auto maxHeightElement = std::max_element(bp.intermediateData.begin(), bp.intermediateData.end(),
        [](const auto& data1, const auto& data2) -> bool
        {
            //BorderPoint::PerClusterData
            return data1.second.v.position.y() > data2.second.v.position.y();
        });
    const int maxHeightClusterId = maxHeightElement->first;

    float weightSum = 0.f;
    for (const auto& [cIdx, cData] : bp.intermediateData)
    {
        const auto& iCluster = clusterMap[cIdx];
        const Generation::SmoothingParams& params = iCluster->getSmoothingParams();
        float currWeight = params.weight * (cIdx == maxHeightClusterId ? params.upperPriorityFactor : 1.f / params.upperPriorityFactor);
        if (params.useNoise)
        {
            const float noiseX = (cData.v.position.x() + cIdx) * params.noiseFrequency;
            const float noiseZ = (cData.v.position.z() + cIdx) * params.noiseFrequency;
            currWeight *= getGlobalNoiseValue(noiseX, noiseZ, ENoiseUsage::TerrainHeight) * params.noiseAmplitude;
            currWeight = std::max(currWeight, 0.05f);
        }
        weights[cIdx] = currWeight;
        weightSum += currWeight;
    }

    for (auto&& [cIdx, pcd] : bp.intermediateData)
    {
        auto&& iCluster = clusterMap[cIdx];
        float w = weights[cIdx] / weightSum;
        auto&& chunk = terrainChunks[blockChunkMap[iCluster->keyCell]];

        // Simple weighting
        finalHeight += pcd.v.position.y() * w;
        for(int i = 0; i < 4; ++i)
            finalPackParams[i] += getPackParam(pcd.v.packParams, i) * w;

        temperature += w * pcd.v.temperature;
        humidity += w * pcd.v.humidity;

        // Terrain slot lookup -> Simple weighting
        int terrainTexChunkSlot = indexOf(hostChunk->getTerrainTextureIds(), chunk->getTerrainTextureIds()[iCluster->terrainTexPackSlot]);
        if (terrainTexChunkSlot == -1)
        {
            OmniLog(ELoggingLevel::Error) <<= "Terrain chunk match failure";
            continue;
        }
        finalTerrainWeights[terrainTexChunkSlot] += w * getTexWeight(pcd.v.terrainTexWeights, terrainTexChunkSlot);

        if (chunk->getBiomeTextureIds().empty() || iCluster->biomeTexPackSlot == -1)
            continue;
        
        // Biome slot lookup -> Simple weighting
        int biomeTexChunkSlot = indexOf(hostChunk->getBiomeTextureIds(), chunk->getBiomeTextureIds()[iCluster->biomeTexPackSlot]);
        if (biomeTexChunkSlot == -1)
        {
            OmniLog(ELoggingLevel::Error) <<= "Biome chunk match failure";
            continue;
        }
        finalBiomeWeights[biomeTexChunkSlot] += w * getTexWeight(pcd.v.biomeTexWeights, biomeTexChunkSlot);
    }

    pos.setY(finalHeight);
    terrainTexWeights = compileTexWeights(finalTerrainWeights);
    biomeTexWeights = compileTexWeights(finalBiomeWeights);
    packParams = compilePackParams(finalPackParams);
}

TerrainMeshVertex::TerrainMeshVertex(const QVector3D& inPos, const QVector3D& inNormal, const Generation::TerrainBlockClusterBase& cluster)
    : position(inPos)
    , normal(inNormal)
    , terrainTexWeights(0xFF << (8 * cluster.terrainTexPackSlot)) // 100% of own tex slot, 0% of others 
    , biomeTexWeights(0)
    , packParams(cluster.metaCluster->getPackParams())
    , temperature(std::midpoint(cluster.temperatureRange[0], cluster.temperatureRange[1]) 
        + float(getGlobalNoiseValue(inPos.x() * 0.05f, inPos.z() * 0.05f, ENoiseUsage::Temperature)) 
            * (cluster.temperatureRange[1] - cluster.temperatureRange[0]) * 0.5f)
    , humidity(std::midpoint(cluster.humidityRange[0], cluster.humidityRange[1])
        + float(getGlobalNoiseValue(inPos.x() * 0.05f, inPos.z() * 0.05f, ENoiseUsage::Humidity))
            * (cluster.humidityRange[1] - cluster.humidityRange[0]) * 0.5f)
{
    // Randomize terrain tex
    using namespace Generation;

    static std::uniform_real_distribution<float> slotDist(-1.0, 1.0);
    float terrainSlot = getPackParam(packParams, 0) * 2.0f;
    terrainSlot = std::clamp(terrainSlot + slotDist(gRandomEngine), 0.0f, 2.0f);

    if (cluster.biomeTexPackSlot >= 0)
    {
        float biomeWeight = 1.0f;
        if (terrainSlot < 1.3f)
        {
            static std::uniform_real_distribution<float> biomeFalloffDist(0.8, 1.2);
            float biomeFalloff = biomeFalloffDist(gRandomEngine) * (1.0f - terrainSlot);
            biomeWeight = std::clamp(1.0f - biomeFalloff, 0.0f, 1.0f);
        }

        // X% of own tex slot, 0% of others 
        biomeTexWeights = (quint32(biomeWeight * 255.0f) << (8 * cluster.biomeTexPackSlot));
    }

    packParams = compilePackParams({ terrainSlot / 2.0f, getPackParam(packParams, 1) });
}
