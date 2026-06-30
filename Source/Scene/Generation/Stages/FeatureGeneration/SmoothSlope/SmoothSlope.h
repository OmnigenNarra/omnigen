#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::SmoothSlope>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::SmoothSlope>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::SmoothSlope>::clusterParams = { .maxHeightDiff = 300, .maxSize = std::numeric_limits<int>::max() };
    MetaClusterParams ClusterTraits<ETerrainBlock::SmoothSlope>::metaClusterParams = { .maxSize = 20 };

    template<>
    class TerrainBlockCluster<ETerrainBlock::SmoothSlope> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        TerrainBlockCluster(const ClusterData<ETerrainBlock::SmoothSlope>& data)
            : TerrainBlockClusterBase(ETerrainBlock::SmoothSlope, data.cells)
        {
        }

        virtual void initialize() override
        {
            smoothingParams.weight = 5.f;
            smoothingParams.smoothingRadius = 1.0f;
        }

        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

        static QVector4D getDebugColor() { return QVector4D(0.48f, 0.99f, 0.f, 1.f); };
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::SmoothSlope> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::SmoothSlope, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}