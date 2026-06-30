#pragma once
#include "../StageGenerationBase.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include <concepts>

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::FeatureGeneration>
    {
        struct ChunkData
        {
            ChunkData() = default;
            explicit ChunkData(const QSharedPointer<TerrainBlockClusterBase>& firstCluster);

            QSet<QSharedPointer<TerrainBlockClusterBase>> clusters;
            std::unordered_set<quint32> terrainPackIds;
            std::unordered_set<quint32> biomePackIds;
        };

    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize() {};

        static void computeBorderPointNormals();
        static void computeNormals();

    private:
        // Spawn block meshes

        // Group clusters into Chunks (export-size objects).
        // Doing it early sets boundaries for cluster materials
        // Each Chunk can't have more than 4 Rock Materials and 4 Cover Materials, neighbor Chunks' materials must be included for blending purposes.
        static void prepareTerrainChunks();

        static void assignClusterTexPackSlots();
        static std::vector<int> findChunkSeeds(const QSet<int>& blockCheckmap);
        static void growTerrainChunk(ChunkData* chunkData, const GVector2D& refPoint, QSet<int>* blockCheckmap);

        static void generateBlockMeshes();

        static float maxChunkRadius;
    };

    namespace Utils
    {
        std::vector<QVector3D> castPointTo3D(const GVector2D& p, const ComparePointPred& Pred = PointByHeightPred());
        std::vector<MeshQueryData> castPointTo3DAdv(const GVector2D& p, const ComparePointPred& Pred = PointByHeightPred());
    }
}