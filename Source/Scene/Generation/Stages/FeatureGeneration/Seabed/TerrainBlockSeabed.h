#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"
#include "Utils/Triangulation/Triangulation.h"

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Seabed>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Seabed>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Seabed>::clusterParams = { .maxHeightDiff = 300, .maxSize = std::numeric_limits<int>::max() };
    MetaClusterParams ClusterTraits<ETerrainBlock::Seabed>::metaClusterParams = { .maxSize = 100 };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Seabed> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        inline TerrainBlockCluster(const ClusterData<ETerrainBlock::Seabed>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Seabed, data.cells)
        {}

        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

        static QVector4D getDebugColor() { return QVector4D(0.1, 0.1, 0.6f, 1); };
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Seabed> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Seabed, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}