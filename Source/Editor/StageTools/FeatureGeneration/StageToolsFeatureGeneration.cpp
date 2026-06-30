#include "stdafx.h"
#include "StageToolsFeatureGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "../SelectionMgrBase.h"

#include "tbb/parallel_for.h"


namespace Design
{
    StageTools<EGenerationStage::FeatureGeneration>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::FeatureGeneration>::getSelectionMgr() const
    {
        return StageToolsBase::getSelectionMgr();
    }

    void StageTools<EGenerationStage::FeatureGeneration>::bind()
    {
        StageToolsBase::bind();
    }

    void StageTools<EGenerationStage::FeatureGeneration>::unbind()
    {
        StageToolsBase::unbind();
    }

    void StageTools<EGenerationStage::FeatureGeneration>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();

        writer << genData->getTerrainBorderPoints();

        // Enviro bounds
        genData->saveEnvBounds(writer);

        auto&& [terrainChunks, chunkBlocksMap, blockChunkMap] = genData->getTerrainChunkData();
        writer << terrainChunks;
        writer << chunkBlocksMap;
        writer << blockChunkMap;

        // Generated geometry
        writer << gClusterMeshMarkerInstance;
        if (!gClusterMeshMarkerInstance)
            return;

        // Cluster -> section mapping
        auto&& metaClusters = Generation::Data::get()->getTerrainMetaClusters();
        std::unordered_map<qint64, qint64> cluster2section;

        for (auto&& metaClusterVec : metaClusters)
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                    cluster2section[cluster->getGuid()] = cluster->section->getGuid();

        writer << cluster2section;
    }

    void StageTools<EGenerationStage::FeatureGeneration>::load(OmniBin<std::ios::in>& reader)
    {
        OmniProfile("Border points");
        auto&& genData = Generation::Data::get();

        QHash<GVector2D, Generation::BorderPoint> borderPoints;
        reader >> borderPoints;

        genData->setBorderPoints(borderPoints);

        // Env bounds
        genData->loadEnvBounds(reader);

        std::vector<QSharedPointer<DTerrainChunk>> terrainChunks;
        QHash<int, QSet<int>> chunkBlocksMap;
        QHash<int, int> blockChunkMap;
        reader >> terrainChunks;
        reader >> chunkBlocksMap;
        reader >> blockChunkMap;

        genData->initTerrainChunks(terrainChunks, chunkBlocksMap, blockChunkMap);

        std::unordered_map<qint64, qint64> cluster2section;
        reader >> gClusterMeshMarkerInstance;
        if (!gClusterMeshMarkerInstance)
            return;

        reader >> cluster2section;

        auto&& metaClusters = genData->getTerrainMetaClusters();
        auto&& [batches, batchesGuard] = gClusterMeshMarkerInstance->getBatches();

        genData->addMarker(gClusterMeshMarkerInstance);
        genData->initializeQueuedMarkers();

        // Restore cluster sections and selection mapping
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> flatClusters;
        for (auto&& metaClusterVec : metaClusters)
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                    flatClusters << cluster;

        for (auto&& [params, batch] : batches)
            gClusterMeshMarkerInstance->painter.trianglesToClusters[params].resize(batch.geometry->indices.size() / 3);

        tbb::parallel_for(0, int(flatClusters.size()), [&](int i)
            {
                auto&& cluster = flatClusters[i];
                auto batchParams = cluster->makeBatchParams();

                auto&& batch = batches.at(batchParams);
                qint64 sectionId = cluster2section.at(cluster->getGuid());
                cluster->section = batch.sections.at(sectionId);

                auto&& batchTriangleClusterMap = gClusterMeshMarkerInstance->painter.trianglesToClusters.at(batchParams);
                IndexType triangleOffset = cluster->section->getIndexBufferOffset() / 3;
                auto triangles = cluster->section->getIndices();
                for (IndexType ti = 0; ti < triangles.size() / 3; ++ti)
                    batchTriangleClusterMap[triangleOffset + ti] = cluster.get();
            });
    }

    auto StageTools<EGenerationStage::FeatureGeneration>::findClusterTriangleUnderCursor() const -> std::optional<ClusterTriangle>
    {
        auto clusterData = SelectionMgrBase::findObjectUnderCursor<DClusterMeshMarker>();
        if (!clusterData)
            return {};
        
        auto&& [batches, batchesGuard] = gClusterMeshMarkerInstance->getBatches();
        auto batchIt = std::next(batches.begin(), clusterData->instance);
        auto&& clustersForBatch = gClusterMeshMarkerInstance->painter.trianglesToClusters.at(batchIt->first);

        return ClusterTriangle{ clustersForBatch[clusterData->primitive], clusterData->primitive };
    }
}