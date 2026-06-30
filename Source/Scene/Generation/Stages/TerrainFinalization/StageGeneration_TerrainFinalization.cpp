#include "stdafx.h"
#include "StageGeneration_TerrainFinalization.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "TerrainChunkDrawable.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"

#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Utils/OmnigenProgressDialog.h"
#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range2d.h>

#include "Utils/QuadTreeLite.h"
#include <execution>

namespace Generation
{
    static inline const quint64 INVALID_INDEX = quint64(-1);
    static const std::array<quint64, 3> NULL_TRIANGLE = { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };

    void StageGen<EGenerationStage::TerrainFinalization>::initialize()
    {
        Data::get()->computeTextureArrays();
        DTerrainChunk::generateTerrainResources();
    }

    // 1) Computes normals
    // 2) World meshes, at this point still quite chunkified, are merged here into greater forms. This step is pure optimization.
    bool StageGen<EGenerationStage::TerrainFinalization>::autoGen()
    {
        //storeTerrainClustersInTemp();
        return bake();
    }

    void StageGen<EGenerationStage::TerrainFinalization>::clear()
    {
        Data::get()->emptyTerrainChunks();
        //restoreTerrainClustersFromTemp();
    }

    using PosMaps = std::array<std::array<QHash<QVector3D, int>, GRID_SEGMENT_COUNT>, GRID_SEGMENT_COUNT>;

    void assignClusterToChunk(const QSharedPointer<TerrainBlockClusterBase>& cluster, const QSharedPointer<DTerrainChunk>& chunk)
    {
        auto* target = chunk->getGeometry<TerrainMeshVertex>(ELOD::Far).get();
        quint32 curSize = target->vertices.size();

        auto&& source = cluster->section->getVertices();
        target->vertices.reserve(curSize + source.size());
        target->vertices.insert(target->vertices.end(), source.begin(), source.end());

        auto&& srcTris = cluster->section->getIndices();
        auto&& targetTris = target->indices;
        targetTris.reserve(targetTris.size() + srcTris.size());
        for (quint32 idx : srcTris)
            targetTris << curSize + idx - cluster->section->getVertexBufferOffset();
    }

    bool StageGen<EGenerationStage::TerrainFinalization>::divideWorldMeshIntoChunks()
    {
        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& dem = Data::get()->getDEM();
        auto&& terrainCells = Data::get()->getTerrainCells()->getCells();
        auto&& [terrainChunks, chunkBlocksMap, blockChunkMap] = Data::get()->getTerrainChunkData();

        // Partitioning loop
        OmniLog(ELoggingLevel::Info) <<= "Filling terrain chunks";
        tbb::parallel_for(0, int(terrainChunks.size()), [&](int i)
            {
                auto&& chunk = terrainChunks[i];
                chunk->assignLodLevel(ELOD::Last, QSharedPointer<TerrainChunkGeometryData>::create());
                chunk->assignLodLevel(ELOD::Far, QSharedPointer<TerrainChunkGeometryData>::create());
                chunk->setActiveLOD(ELOD::Far);

                // chunkBlocksMap has only 1 block per cluster
                for (int bId : chunkBlocksMap[i])
                {
                    auto&& cluster = clusterMap[bId];
                    assignClusterToChunk(cluster, chunk);

                    // Bounding box calculation
                    for (int cellId : cluster->cells)
                        for (QVector3D p : terrainCells[cellId])
                        {
                            p.setY(dem->heightData.sample(p));
                            chunk->cachedBoundingBox.expandToContain(p);
                        }
                }
            });

        // Init drawables
        for (auto&& chunk : terrainChunks)
        {
            // BB debug
            //auto& sizes = chunk->cachedBoundingBox.sizes;
            //std::vector<QVector3D> bbPts =
            //{
            //    chunk->cachedBoundingBox.nbl + QVector3D(0, sizes.y(), 0),
            //    chunk->cachedBoundingBox.nbl + QVector3D(sizes.x(), sizes.y(), 0),
            //    chunk->cachedBoundingBox.nbl + QVector3D(sizes.x(), sizes.y(), sizes.z()),
            //    chunk->cachedBoundingBox.nbl + QVector3D(0, sizes.y(), sizes.z()),
            //};
            //Data::get()->createMarker<DLineMarker>(bbPts, chunk->debugColor, true);

            chunk->initialize();
            emit Editable::created(chunk);
        }

        return true;
    }

    bool StageGen<EGenerationStage::TerrainFinalization>::bake()
    {
        OmniProfile("Baking");

        // Divide.
        if (!divideWorldMeshIntoChunks(/*std::move(gigamesh)*/))
            return false;

        return true;
    }
}