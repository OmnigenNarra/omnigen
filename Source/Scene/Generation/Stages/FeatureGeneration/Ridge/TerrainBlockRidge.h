#pragma once

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include <noise\noise.h>

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::in>& omniBin);

void omniSave(const Generation::TerrainBlockMetaCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockMetaCluster<Generation::ETerrainBlock::Ridge>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Ridge>::clusterParams = { .maxHeightDiff = 2000, .maxSize = 3000 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Ridge>::metaClusterParams = { .maxSize = 20 };

    template<>
    struct ClusterData<ETerrainBlock::Ridge> : public ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Ridge>* metaCluster, int id);
    };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Ridge> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        inline TerrainBlockCluster(const ClusterData<ETerrainBlock::Ridge>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Ridge, data.cells)
        {
        }

        virtual void initialize() override
        {
            smoothingParams.weight = 1000.f;
        }

        static QVector4D getDebugColor() { return QVector4D(0.7f, 0.f, 0.f, 1.f); };
        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;
    };


    float linear(float t);
    float square(float t);
    float cubic(float t);
    float root(float t);


    struct RidgeParameters
    {
        std::array<std::function<float(float)>, 4> interpolationFunctions = { &linear, &square, &cubic, &root };

        float minWidthFactor = 0.1f;
        float maxWidthFactor = 0.3f;

        float minWidthDispersion = 0.6f;
        float maxWidthDispersion = 0.8f;

        float minVerticalOffsetFactor = 0.04f;
        float maxVerticalOffsetFactor = 0.1f;

        float minNoiseAmplitudeFactor = 0.4f;
        float maxNoiseAmplitudeFactor = 0.7f;

        float minHeightLimit = 250.f;
        float maxHeightLimit = 1000.f;
    };


    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Ridge> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells) : TerrainBlockMetaClusterBase(ETerrainBlock::Ridge, inCells) {}

        virtual void initialize() override;

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        double getPeakNoiseValue(const GVector2D& pt) const;
        void initUtils();
        void initRidge();

    public:

        RidgeParameters ridgeParameters;

        float noiseAmplitudeFactor = 0.42f;
        std::function<float(float, float, float)> interpolationFunc;

    private:

        noise::module::RidgedMulti noiseSource;
        noise::model::Plane noiseModel;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}