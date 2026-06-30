#include "stdafx.h"
#include "StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "ClusterMeshMarker.h"

#include "Scene/Generation/Stages/Landmasses/StageGeneration_Landmasses.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"

#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <execution>

#define DEBUG_BORDER_POINTS (1 & !NDEBUG)

#if DEBUG_BORDER_POINTS
#include "Utils/Colors.h"
#endif

namespace Generation
{
    // *** Omnigen core ***
    // Cluster consist of some Voronoi cells united together
    // Each cluster generates detailed mesh of its terrain form independently.
    // Merging with other clusters is based on border points - points lying on the borders of clusters.
    // Border points are carefully picked and store additional information.
    // Border points reside in a global map, and each cluster stores its own BPs in addition.
    // Mesh smoothing is based on distance to borders and interpolation between raw height and border height.
    bool StageGen<EGenerationStage::FeatureGeneration>::autoGen()
    {
        prepareTerrainChunks();
        assignClusterTexPackSlots();
        generateBlockMeshes();
        return true;
    }

    void StageGen<EGenerationStage::FeatureGeneration>::clear()
    {
        Data::get()->clearBorderPoints();
        clearAllBatches<ClusterMeshBatchParams>();
        Data::get()->setEnviroBounds({});
        Data::get()->clearTerrainChunks();

        for (auto&& metaClusterVec : Data::get()->getTerrainMetaClusters())
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                    cluster->clear();
    }

    float StageGen<EGenerationStage::FeatureGeneration>::maxChunkRadius = 50'00;

    void StageGen<EGenerationStage::FeatureGeneration>::prepareTerrainChunks()
    {
        auto&& metaClusters = Data::get()->getTerrainMetaClusters();
        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& cells = Data::get()->getTerrainCells()->getCells();

        QSet<int> blockCheckmap;
        std::vector<QSharedPointer<DTerrainChunk>> terrainChunks;
        QHash<int, QSet<int>> chunkBlocksMap;
        QHash<int, int> blockChunkMap;

        // Partitioning loop
        OmniProfile("Terrain chunks");
        OmniLog(ELoggingLevel::Info) <<= "Initializing terrain chunks...";
        while (blockCheckmap.size() < cells.size())
        {
            // Block indices
            auto chunkSeeds = findChunkSeeds(blockCheckmap);

            // Initialize new chunks' data
            std::vector<ChunkData> chunkData(chunkSeeds.size());

            for (int i = 0; i < chunkSeeds.size(); ++i)
            {
                auto&& cluster = clusterMap[chunkSeeds[i]];
                chunkData[i] = ChunkData(cluster);
                blockCheckmap += convertStlToQSet(cluster->cells);
            }

            // Grow chunks in parallel
            tbb::parallel_for(0, int(chunkData.size()), [&](int i)
                {
                    GVector2D chunkRefPoint = cells[chunkSeeds[i]]->getCenter();
                    growTerrainChunk(&chunkData[i], chunkRefPoint, &blockCheckmap);
                });

            // Create chunks from gathered data
            terrainChunks.reserve(terrainChunks.size() + chunkData.size());
            for (auto&& singleChunkData : chunkData)
            {
                // Create block-chunk mappings.
                for (auto&& cluster : singleChunkData.clusters)
                {
                    // Practically a cluster-chunk mapping, 1 block per cell
                    blockChunkMap[cluster->keyCell] = terrainChunks.size();
                    chunkBlocksMap[terrainChunks.size()] << cluster->keyCell;
                }

                auto newChunk = QSharedPointer<DTerrainChunk>::create(convertStlToQSet(singleChunkData.terrainPackIds), convertStlToQSet(singleChunkData.biomePackIds));
                terrainChunks << newChunk;
            }
        }

        Data::get()->initTerrainChunks(terrainChunks, chunkBlocksMap, blockChunkMap);
    }

    void StageGen<EGenerationStage::FeatureGeneration>::assignClusterTexPackSlots()
    {
        for (auto&& metaClusterVec : Data::get()->getTerrainMetaClusters())
        {
            tbb::parallel_for(0, int(metaClusterVec.size()), [&](int i)
                {
                    for (auto&& cluster : metaClusterVec[i]->getClusters())
                        cluster->computeTexSlots();
                });
        }
    }

    std::vector<int> StageGen<EGenerationStage::FeatureGeneration>::findChunkSeeds(const QSet<int>&blockCheckmap)
    {
        std::vector<int> result;
        auto&& cells = Data::get()->getTerrainCells()->getCells();

        int seedCount = std::min(QThread::idealThreadCount(), int(cells.size() - blockCheckmap.size()));
        result.reserve(seedCount);

        while (true)
        {
            // Look for a new seed
            auto it = std::find_if(std::execution::unseq, cells.begin(), cells.end(), [&](const Voronoi::GVoronoiCell& cell)
                {
                    int idx = &cell - &(cells[0]);
                    if (blockCheckmap.contains(idx))
                        return false;

                    for (int seedIdx : result)
                        if (distance(cells[seedIdx]->getCenter(), cell->getCenter()) < maxChunkRadius * 3) // *2 is enough, *3 accounts for cluster radius
                            return false;

                    return true;
                });

            if (it == cells.end())
                break;

            // Accept seed
            int idx = std::distance(cells.begin(), it);
            result << idx;

            if (result.size() == seedCount)
                break;
        }

        return result;
    }

    void StageGen<EGenerationStage::FeatureGeneration>::growTerrainChunk(ChunkData * chunkData, const GVector2D & refPoint, QSet<int>*blockCheckmap)
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& cells = diagram->getCells();
        auto&& clusterMap = Data::get()->getTerrainClustersMap();

        static std::mutex blockCheckmapGuard;

        // Cluster hull
        auto createClusterHull = [&](const QSet<QSharedPointer<TerrainBlockClusterBase>>& source)
        {
            QSet<QSharedPointer<TerrainBlockClusterBase>> hull;
            for (auto&& cluster : source)
                for (int cell : cluster->cells)
                {
                    auto&& neighbors = diagram->getCellNeighborsAt(cell);
                    for (auto nit = neighbors.keyBegin(); nit != neighbors.keyEnd(); ++nit)
                    {
                        auto&& nCluster = clusterMap[*nit];
                        if (!source.contains(nCluster))
                            hull << nCluster;
                    }
                    auto&& pointNeighbors = diagram->getCellPointNeighborsAt(cell);
                    for (auto nit = pointNeighbors.keyBegin(); nit != pointNeighbors.keyEnd(); ++nit)
                    {
                        auto&& nCluster = clusterMap[*nit];
                        if (!source.contains(nCluster))
                            hull << nCluster;
                    }
                }

            return hull;
        };

        auto hullCheck = [&](const QSharedPointer<TerrainBlockClusterBase>& cluster, bool update)
        {
            // Materials limit
            std::unordered_set<quint32> terrainPackIdsPreview = chunkData->terrainPackIds;
            std::unordered_set<quint32> biomePackIdsPreview = chunkData->biomePackIds;

            auto neighbors = createClusterHull({ cluster });
            for (auto&& neighbor : neighbors)
            {
                terrainPackIdsPreview += neighbor->metaCluster->getTerrainTexPack();

                if (quint32 btp = neighbor->metaCluster->getBiomeTexPack(); btp != quint32(-1))
                    biomePackIdsPreview += btp;
            }

            if (terrainPackIdsPreview.size() > 4 || biomePackIdsPreview.size() > 4)
                return false;

            // Valid addition candidate
            if (update)
            {
                chunkData->terrainPackIds = std::move(terrainPackIdsPreview);
                chunkData->biomePackIds = std::move(biomePackIdsPreview);

                // Don't add same cluster twice
                if (std::scoped_lock lock(blockCheckmapGuard); blockCheckmap->contains(cluster->keyCell))
                    return false;

                // Distance check
                if (distance(cells[cluster->keyCell]->getCenter(), refPoint) >= maxChunkRadius)
                    return false;

                chunkData->clusters << cluster;

                // CRITICAL SECTION
                std::scoped_lock lock(blockCheckmapGuard);
                (*blockCheckmap) += convertStlToQSet(cluster->cells);
            }

            return true;
        };

        auto filterHull = [&](QSet<QSharedPointer<TerrainBlockClusterBase>>* hull)
        {
            while (true)
            {
                auto size = hull->size();

                for (auto&& cluster : *hull)
                {
                    if (!hullCheck(cluster, false))
                    {
                        hull->remove(cluster);
                        break;
                    }
                }

                if (size == hull->size())
                    break;
            }
        };

        // Grow
        while (true)
        {
            auto hull = createClusterHull(chunkData->clusters);
            filterHull(&hull);

            // Update on the fly
            bool anyAdded = false;
            for (auto&& cluster : hull)
                anyAdded |= hullCheck(cluster, true);

            if (!anyAdded)
                break;
        }
    }

    static void debugIncorrectBorderPoints(const BorderPoint& bp, const BorderPointInfo& info)
    {
#if DEBUG_BORDER_POINTS
        if (bp.intermediateData.size() < 2)
        {
            static int counter = 0;
            const auto color = ++counter % 2 == 0 ? Colors::red : Colors::yellow;
            const float length = counter % 2 == 0 ? 2000.f : 1000.f;
            const auto& dem = *Data::get()->getDEM();
            Data::get()->createMarker<DLineMarker>(info.pos + QVector3D(0.f, dem.heightData.sample(info.pos), 0.f), length, color);
        }
#endif
    }

    void StageGen<EGenerationStage::FeatureGeneration>::generateBlockMeshes()
    {
        OmniLog(ELoggingLevel::Info) <<= "Generating terrain geometry...";
        auto&& metaClusters = Data::get()->getTerrainMetaClusters();

        std::vector<QSharedPointer<TerrainBlockClusterBase>> flatClusters;
		for (auto&& metaClusterVec : metaClusters)
			for (auto&& metaCluster : metaClusterVec)
				for (auto&& cluster : metaCluster->getClusters())
					flatClusters << cluster;

        // Spawn clusters
        {
            OmniProfile("Block meshes");
            OmniLog(ELoggingLevel::Trace) <<= "Generating block meshes...";
            //for (int i = 0; i < flatClusters.size(); ++i)
			tbb::parallel_for(0, int(flatClusters.size()), [&](int i)
				{
					flatClusters[i]->generate();
				});
        }

        {
            OmniProfile("Cluster triangle mapping for selection");
            auto&& [batches, batchesGuard] = gClusterMeshMarkerInstance->getBatches();
            for (auto&& [params, batch] : batches)
                gClusterMeshMarkerInstance->painter.trianglesToClusters[params].resize(batch.geometry->indices.size() / 3);

            tbb::parallel_for(0, int(flatClusters.size()), [&](int i)
                {
                    auto&& cluster = flatClusters[i];
                    auto&& batchTriangleClusterMap = gClusterMeshMarkerInstance->painter.trianglesToClusters.at(cluster->makeBatchParams());
                    IndexType triangleOffset = cluster->section->getIndexBufferOffset() / 3;
                    auto triangles = cluster->section->getIndices();
                    for (IndexType ti = 0; ti < triangles.size() / 3; ++ti)
                        batchTriangleClusterMap[triangleOffset + ti] = cluster.get();
                });
        }

        {
            OmniProfile("Border data registration");
            tbb::parallel_for(0, int(flatClusters.size()), [&](int i)
                {
                    flatClusters[i]->registerBorderData();
                });
        }

        // Border data finalization
        {
            OmniProfile("Cluster border blending");
            OmniLog(ELoggingLevel::Trace) <<= "Blending cluster borders...";
            auto&& borderPoints = Data::get()->getTerrainBorderPoints();

            std::vector<QVector3D> borderPointCloud;
            //std::vector<GVector2D> borderPointCloud;
            std::mutex bpcGuard;

            const auto finalizeBorderPoint = [&bpcGuard, &borderPointCloud, &borderPoints](BorderPointInfo& bpInfo, TerrainBlockClusterBase* owner)
            {
                auto& bp = borderPoints[bpInfo.pos];
                debugIncorrectBorderPoints(bp, bpInfo);
                bpInfo.setFinalData(owner, bp);
                std::scoped_lock lock(bpcGuard);
                borderPointCloud.push_back(bpInfo.pos);
            };

            for (auto&& metaClusterVec : metaClusters)
                tbb::parallel_for(0, int(metaClusterVec.size()), [&](int i)
                    {
                        for (auto&& cluster : metaClusterVec[i]->getClusters())
                        {
                            for (auto&& bpVec : cluster->borderPoints)
                                for (auto&& bpInfo : bpVec)
                                    finalizeBorderPoint(bpInfo, cluster.get());
                        }
                    });

            Data::get()->createMarker<DPointCloudMarker>(borderPointCloud);
            //Data::get()->createMarker<DPointCloudMarker>(std::vector<QVector3D>(borderPointCloud.begin(), borderPointCloud.end()));
        }

        //-------------------------------------------------------------------
        // Smoothing
        // ------------------------------------------------------------------
        {
            OmniProfile("Smoothing");
            OmniLog(ELoggingLevel::Trace) <<= "Smoothing terrain...";

            tbb::parallel_for(0, int(flatClusters.size()), [&](int i)
                {
                    flatClusters[i]->smoothMesh();
                });
        }

        // Compute border normals
        computeBorderPointNormals();

        // All normals
        computeNormals();
    }

    void StageGen<EGenerationStage::FeatureGeneration>::computeBorderPointNormals()
    {
        OmniProfile("Border Normals");
        OmniLog(ELoggingLevel::Trace) <<= "Border normals...";

        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& bps = Data::get()->getTerrainBorderPoints();

        std::vector<GVector2D> bpKeys;
        bpKeys.reserve(bps.size());
        for (auto posIt = bps.keyBegin(); posIt != bps.keyEnd(); ++posIt)
            bpKeys.push_back(*posIt);

        tbb::parallel_for(0, int(bpKeys.size()), [&](int i)
            {
                auto&& bp = bps.constFind(bpKeys[i]);

                for (auto&& [cIdx, pcd] : bp->intermediateData)
                {
                    auto&& cluster = clusterMap[cIdx];

                    auto&& vertices = cluster->section->mainBuffer->vertices;
                    auto triangles = cluster->section->getIndices();
                    for (IndexType triangleIdx : pcd.facesPerBlock[cluster->keyCell])
                    {
                        IndexType i1 = triangles[triangleIdx * 3];
                        IndexType i2 = triangles[triangleIdx * 3 + 1];
                        IndexType i3 = triangles[triangleIdx * 3 + 2];

                        const auto& p1 = vertices[i1].position;
                        const auto& p2 = vertices[i2].position;
                        const auto& p3 = vertices[i3].position;

                        bp->normal += computeFaceNormal({ p1, p2, p3 });
                    }
                }

                bp->normal.normalize();
            });
    }

    void StageGen<EGenerationStage::FeatureGeneration>::computeNormals()
    {
        OmniProfile("Normals");
        OmniLog(ELoggingLevel::Trace) <<= "Generating normals...";

        for (auto&& metaClusterVec : Data::get()->getTerrainMetaClusters())
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                    cluster->computeNormals();
    }

    StageGen<EGenerationStage::FeatureGeneration>::ChunkData::ChunkData(const QSharedPointer<TerrainBlockClusterBase>& firstCluster)
        : clusters({ firstCluster })
        , terrainPackIds({ firstCluster->metaCluster->getTerrainTexPack() })
        , biomePackIds(firstCluster->metaCluster->getBiomeTexPack() == quint32(-1) ? std::unordered_set<IndexType>{} : std::unordered_set<IndexType>{ firstCluster->metaCluster->getBiomeTexPack() })
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& clusterMap = Data::get()->getTerrainClustersMap();

        // Setup starting materials - must include neighbors
        for (int cell : firstCluster->cells)
        {
            auto&& neighbors = diagram->getCellNeighborsAt(cell);
            for (auto nit = neighbors.keyBegin(); nit != neighbors.keyEnd(); ++nit)
            {
                auto&& nCluster = clusterMap[*nit];
                terrainPackIds << nCluster->metaCluster->getTerrainTexPack();

                if (quint32 btp = nCluster->metaCluster->getBiomeTexPack(); btp != quint32(-1))
                    biomePackIds << btp;
            }

            auto&& pointNeighbors = diagram->getCellPointNeighborsAt(cell);
            for (auto nit = pointNeighbors.keyBegin(); nit != pointNeighbors.keyEnd(); ++nit)
            {
                auto&& nCluster = clusterMap[*nit];
                terrainPackIds << nCluster->metaCluster->getTerrainTexPack();

                if (quint32 btp = nCluster->metaCluster->getBiomeTexPack(); btp != quint32(-1))
                    biomePackIds << btp;
            }
        }
    }


    namespace Utils
    {
        std::vector<QVector3D> castPointTo3D(const GVector2D& p, const ComparePointPred& Pred)
        {
            OmniProfile("Blocks -- Point height lookup");
            auto&& blockTree = Generation::Data::get()->getBlockQuadTree();
            auto&& clusters = Generation::Data::get()->getTerrainClustersMap();

            std::vector<QVector3D> results;
            std::unordered_set<int> alreadyChecked;
            float r = 200.f;
            float maxR = Data::get()->getLargestVoronoiCellRadius();
            while (true)
            {
                auto nodes = blockTree->find_all_nearest(p.x, p.z, r);
                for (auto&& node : nodes)
                {
                    if (alreadyChecked.contains(node->data))
                        continue;

                    alreadyChecked.insert(node->data);
                    for (auto&& result : clusters[node->data]->raycastDataFrom2D(p, Pred))
                        if (!contains(results, result))
                            results <<= result;
                }

                if (!results.empty() || r > maxR)
                    break;

                r *= 2.f;
            }

            std::ranges::sort(results, Pred);
            return results;
        }

        std::vector<MeshQueryData> castPointTo3DAdv(const GVector2D& p, const ComparePointPred& Pred)
        {
            OmniProfile("Blocks -- Point height lookup Adv");
            auto&& blockTree = Generation::Data::get()->getBlockQuadTree();
            auto&& clusters = Generation::Data::get()->getTerrainClustersMap();

            std::vector<MeshQueryData> results;
            std::unordered_set<int> alreadyChecked;
            float r = 200.f;
            float maxR = Data::get()->getLargestVoronoiCellRadius();
            while (true)
            {
                auto nodes = blockTree->find_all_nearest(p.x, p.z, r);
                for (auto&& node : nodes)
                {
                    if (alreadyChecked.contains(node->data))
                        continue;

                    alreadyChecked.insert(node->data);
                    for (auto&& result : clusters[node->data]->raycastDataFrom2DAdv(p, Pred))
                        if (!contains(results, result))
                            results <<= result;
                }

                if (!results.empty() || r > maxR)
                    break;

                r *= 2.f;
            }

            std::ranges::sort(results, [&](const MeshQueryData& A, const MeshQueryData& B) {return Pred(A.pos, B.pos); });
            return results;
        }
    }
}
