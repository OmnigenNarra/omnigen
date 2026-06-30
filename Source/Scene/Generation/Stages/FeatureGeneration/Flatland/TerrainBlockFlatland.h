#pragma once

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Flatland>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Flatland>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Flatland>::clusterParams = { .maxHeightDiff = 50, .maxSize = std::numeric_limits<int>::max() };
    MetaClusterParams ClusterTraits<ETerrainBlock::Flatland>::metaClusterParams = { .maxSize = 20 };

    template<>
    struct ClusterData<ETerrainBlock::Flatland> : public ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Flatland>* metaCluster, int id)
            : ClusterDataBase(id)
        {}
    };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Flatland> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        TerrainBlockCluster(const ClusterData<ETerrainBlock::Flatland>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Flatland, data.cells)
            , height(data.height)
        {
            findBiomeDomain();
        }

        virtual void initialize() override;

        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

        static QVector4D getDebugColor() { return QVector4D(0.2, 0.35, 0.15, 1); };

        void findBiomeDomain();

        float height;
        QSharedPointer<DDomain> biomeDomain;
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Flatland> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Flatland, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}