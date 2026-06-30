#pragma once

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Utils/Geom3dUtils.h"
#include "Scene/OmnigenDrawable.h"

using namespace geom3dUtils;

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Precipice>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Precipice>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Precipice>::clusterParams = { .maxHeightDiff = 10000, .maxSize = 10 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Precipice>::metaClusterParams = { .maxSize = 400 };

    template<>
    struct ClusterData<ETerrainBlock::Precipice> : public ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Precipice>* metaCluster, int id);
    };


    template<>
    class TerrainBlockCluster<ETerrainBlock::Precipice> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        TerrainBlockCluster(const ClusterData<ETerrainBlock::Precipice>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Precipice, data.cells)
        {
        }

        virtual void initialize() override
        {
            smoothingParams.weight = 10'000.f;
            smoothingParams.smoothingRadius = 1.f;
        }

        virtual void generate() override;

        void init();

        static QVector4D getDebugColor() { return QVector4D(181.f / 255.f, 184.f / 255.f, 196.f / 255.f, 1.f); };

    private:

        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

        // bottom polygon, top polygon, first and second edges indices, is direction for bottom polygon clockwise
        std::optional<std::tuple<Polygon2D, Polygon2D, int , int, bool>> dividePolygonIntoTopAndBottom(const Polygon2D& polygon, const std::vector<float>& heights, int minHeightIndex, float heightThreshold);

        // returns nullopt, if division is invalid, otherwise returns main polygon and remaining polygon parts
        std::optional<std::vector<Polygon2D>> cutBottom(const Polygon2D& polygon, float minHeightThreshold);

        float distanceFromPointToEdge(const QVector3D& pt);
        float getMaximumDistFromEdge(const Polygon2D& polygon);

        float getOffset(int frameLevelIndex) const;

    private:

        Polygon2D boundingAreaPolygon;
        Polygon2D precipiceBoundingPolygon;

        std::vector<QVector3D> vertices;
        std::vector<std::vector<int>> frameIndices;

        float maxHeight = std::numeric_limits<float>::min();
        float minHeight = std::numeric_limits<float>::max();
        float maxOverhangFactor = 0.f;

        Segment3D edgeSegment;
        Polygon2D topPolygon;
        Polygon2D bottomPolygon;
        std::vector<Polygon2D> remainingPolygons;

        float maxDistToLine = 0.f;
        GVector2D normalVecToEdge2D;

        bool needFallback = false;

    };

    // MetaCluster -----------------------------------------------------------------------------------

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Precipice> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Precipice, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);

    protected:

        virtual void spawnClusters();
    };


}