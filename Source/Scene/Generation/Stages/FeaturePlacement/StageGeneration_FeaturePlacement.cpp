#include "stdafx.h"
#include "StageGeneration_FeaturePlacement.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"

#include "Scene/Generation/Stages/Landmasses/StageGeneration_Landmasses.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
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
    void StageGen<EGenerationStage::FeaturePlacement>::initialize()
    {
        auto&& cells = Data::get()->getTerrainCells()->getCells();
        std::vector<QSharedPointer<TerrainBlockClusterBase>> terrainClustersMap(cells.size());
        Data::get()->setTerrainClusters({}, terrainClustersMap);
    }

    bool StageGen<EGenerationStage::FeaturePlacement>::autoGen()
    {
        OmniProfile("Clustering");

        const auto& diagram = *Data::get()->getTerrainCells();
        auto&& cells = diagram.getCells();
        auto&& blockTypeMap = Data::get()->getBlockTypeMap();

        // Meta Clusters
        std::unordered_set<int> assignedIndices;
        QMap<ETerrainBlock, std::vector<QSharedPointer<TerrainBlockMetaClusterBase>>> metaClusters = Data::get()->getTerrainMetaClusters();
        for (auto&& metaClusterVec : metaClusters)
            for (auto&& metaCluster : metaClusterVec)
                assignedIndices += metaCluster->getCells();

        for (int i = 0; i < blockTypeMap.size(); i++)
        {
            if (assignedIndices.contains(i))
                continue;

            static const auto typeGeter = [](const ETerrainBlock& type) { return type; };
            auto&& maxSize = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetClusterTraits>(blockTypeMap[i]).maxSize;
            auto&& cells = convertQSetToSTL(Utils::createMetaCluster(blockTypeMap, typeGeter, diagram, i, &assignedIndices, maxSize));

            auto metaCluster = ETerrainBlockConstexpr::UseIn<EAC::CreateMetaCluster>(blockTypeMap[i], cells);
            metaClusters[blockTypeMap[i]].push_back(metaCluster);
        }

        // Final Clusters
        for (auto&& metaClusterVec : metaClusters)
        {
#if DEBUG_BLOCK_SELECTION
            for (auto&& metaCluster : metaClusterVec)
            {
#else
            tbb::parallel_for(0, int(metaClusterVec.size()), [&](int i)
                {
                    auto&& metaCluster = metaClusterVec[i];
#endif
                    metaCluster->spawnClusters();
                }
#if !DEBUG_BLOCK_SELECTION
            );
#endif
            }

        // Compute cluster map
        std::vector<QSharedPointer<TerrainBlockClusterBase>> terrainClustersMap(cells.size());
        for (auto&& metaClusterVec : metaClusters)
        {
            tbb::parallel_for(0, int(metaClusterVec.size()), [&](int metaClusterIdx)
                {
                    auto&& metaCluster = metaClusterVec[metaClusterIdx];
                    for (auto&& cluster : metaCluster->getClusters())
                        for (int cellIdx : cluster->cells)
                            terrainClustersMap[cellIdx] = cluster;
                });
        }

        Data::get()->setTerrainClusters(metaClusters, terrainClustersMap);

        return true;
    }

    void StageGen<EGenerationStage::FeaturePlacement>::clear()
    {
        auto&& metaClustersPerType = Data::get()->getTerrainMetaClusters();
        auto&& clusterMap = Data::get()->getTerrainClustersMap();

        for (auto&& metaClusters : metaClustersPerType)
            for (auto&& metaCluster : metaClusters)
                emit Editable::aboutToBeDeleted(metaCluster);

        QSet<QSharedPointer<TerrainBlockClusterBase>> clusters;
        for (auto&& cluster : clusterMap)
            clusters += cluster;

        for (auto&& cluster : clusters)
            emit Editable::aboutToBeDeleted(cluster);

        Data::get()->setTerrainClusters({}, {});
    }

    bool StageGen<EGenerationStage::FeaturePlacement>::validate()
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        for (auto&& clusterOnCell : clusterMap)
        {
            if (!clusterOnCell)
            {
                OmniLog(ELoggingLevel::Error) <<= "Not all cells are filled with clusters!";
                return false;
            }
        }

        return true;
    }

    void StageGen<EGenerationStage::FeaturePlacement>::finalize()
    {
        auto&& metaClusters = Data::get()->getTerrainMetaClusters();

        for (auto&& metaClusterVec : metaClusters)
            for (auto&& metaCluster : metaClusterVec)
                metaCluster->initialize();
//             tbb::parallel_for(0, int(metaClusterVec.size()), [&](int metaClusterIdx)
//                 {
//                     metaClusterVec[metaClusterIdx]->initialize();
//                 });
    }
}
