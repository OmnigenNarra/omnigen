#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include <Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h>
#include <Scene/Generation/Stages/Landmasses/ShorelineMarker.h>
#include "Utils/Triangulation/Triangulation.h"

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Beach>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Beach>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Beach>::clusterParams = { .maxHeightDiff = 300, .maxSize = 15 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Beach>::metaClusterParams = { .maxSize = 20 };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Beach> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        inline TerrainBlockCluster(const ClusterData<ETerrainBlock::Beach>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Beach, data.cells)
        {}

        static QVector4D getDebugColor() { return QVector4D(0.6, 0.6, 0.3f, 1); };

        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

    private:

        static inline const float density = 1000.f;
        static inline const int densityMultiplier = 10;

        float shorelineDistance(const GVector2D& point) const;
        std::tuple<float, float> getPointHeightByDistance(const GVector2D& point, float dist) const; // dist = distance to shoreline
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Beach> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Beach, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}