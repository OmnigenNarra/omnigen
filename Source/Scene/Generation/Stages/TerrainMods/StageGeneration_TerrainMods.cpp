#include "stdafx.h"
#include "StageGeneration_TerrainMods.h"
#include "TerrainMod.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"

#include "River/RiverMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>

#include "Utils/TriangleAdjacencyUtils.h"

namespace Generation
{
    // Mods are additional terrain forms that fuse with and/or modify geometry created during Block Generation stage.
    // Example: River valleys
    bool StageGen<EGenerationStage::ModAssignment>::autoGen()
    {
        generateMods();
        return true;
    }

    void StageGen<EGenerationStage::ModAssignment>::clear()
    {
        revertMods();
        Generation::Data::get()->setTerrainMods({});
        Generation::Data::get()->setTriangleAdjList({});
    }

    void StageGen<EGenerationStage::ModAssignment>::finalize()
    {
        applyMods();
        StageGen<EGenerationStage::FeatureGeneration>::computeNormals();
        return;

        {
            OmniProfile("Triangle List Computation");

            TrianglesGraph::TriangleAdjacencyGraph globalGraph;

            {
                OmniProfile("Triangle graph");
                auto&& trianglesGraphByClusterId = TrianglesGraph::TriangleAdjacencyGraph::calcTrianglesGraphByClusters();
                globalGraph = TrianglesGraph::TriangleAdjacencyGraph::mergeClustersGraphs(trianglesGraphByClusterId);
                // Generation::Data::get()->setTriangleAdjList(globalGraph.getNodesMap());
                // temporary stub
                Generation::Data::get()->setTriangleAdjList({});
            }

            TrianglesGraph::TwiGraph twiGraph(globalGraph);
            {
                OmniProfile("TWI");
                twiGraph.calculateGraph();
                twiGraph.calculateTwiData();
            }

            {
                OmniProfile("TWI debug draw");
                twiGraph.drawTwiGraph();
            }

            // TrianglesGraph::drawTwiGraph(twiGraph, Colors::blue);
            // twiGraph.drawWaterfalls();
        }
    }

    void StageGen<EGenerationStage::ModAssignment>::generateMods()
    {
        OmniProfile("Mod generation");

        QMap<ETerrainMod, std::vector<QSharedPointer<Generation::TerrainModBase>>> allMods;

        for (int i = 0; i < int(ETerrainMod::Last); ++i)
            allMods[ETerrainMod(i)] << ETerrainModConstexpr::UseIn<EAC::GenerateMods>(ETerrainMod(i));

        Data::get()->setTerrainMods(allMods);
    }

    void StageGen<EGenerationStage::ModAssignment>::applyMods()
    {
        OmniLog(ELoggingLevel::Trace) <<= "Applying terrain mods...";
        OmniProfile("Mod application");
        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& modBackupData = *Data::get()->getModBackupData();

        auto&& allMods = Data::get()->getTerrainMods();
        std::map<ETerrainMod, ModAlterationsList> allAlterations;

        for (auto modIt = allMods.keyValueBegin(); modIt != allMods.keyValueEnd(); ++modIt)
        {
            auto&& [type, modsOfSameType] = *modIt;
            auto targetType = ETerrainModConstexpr::UseIn<EAC::GetSubmitTarget>(type);

            for (auto&& mod : modsOfSameType)
                mod->submitAll(&allAlterations[targetType]);
        }

        for (auto&& [targetType, possibleAlterations] : allAlterations)
        {
            // cluster representative cells
            auto clustersAffected = possibleAlterations.keys();

            // Init map to then access in parallel
            for (int cId : clustersAffected)
                modBackupData[cId];

            tbb::parallel_for(0, clustersAffected.size(), [&](int clusterKeyIdx)
                {
                    int cId = clustersAffected[clusterKeyIdx];
                    auto verts = clusterMap[cId]->section->getVertices();
                    for (auto it = possibleAlterations[cId].keyValueBegin(); it != possibleAlterations[cId].keyValueEnd(); ++it)
                    {
                        auto&& [i, props] = *it;
                        modBackupData[cId][i] = verts[i];
                        verts[i] = ETerrainModConstexpr::UseIn<EAC::ApplyMods>(targetType, props);
                    }
                });
        }

        // Update preview
        for (auto cIt = modBackupData.keyBegin(); cIt != modBackupData.keyEnd(); ++cIt)
            updateBatch(clusterMap[*cIt]->section->batchParams);
    }

    void StageGen<EGenerationStage::ModAssignment>::revertMods()
    {
        ETerrainModConstexpr::UseAllIn<EAC::ClearMods>();

        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& modBackupData = *Data::get()->getModBackupData();
        auto clustersAffected = modBackupData.keys();

        tbb::parallel_for(0, clustersAffected.size(), [&](int clusterKeyIdx)
            {
                int cId = clustersAffected[clusterKeyIdx];
                auto verts = clusterMap[cId]->section->getVertices();
                for (auto it = modBackupData[cId].keyValueBegin(); it != modBackupData[cId].keyValueEnd(); ++it)
                {
                    auto&& [i, oldValue] = *it;
                    verts[i] = oldValue;
                }
            });

        // Update preview
        for (int cId : clustersAffected)
            updateBatch(clusterMap[cId]->section->batchParams);

        modBackupData.clear();
    }
}
