#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Utils/Interpolation.h"
#include "Scene/Generation/Common/Objects/Heightfield.h"
#include <noise\noise.h>


namespace Interpolation
{
    struct TechniqueBase;
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Slope>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Slope>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Slope>::clusterParams = { .maxHeightDiff = 2000, .maxSize = 30 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Slope>::metaClusterParams = { .maxSize = 20 };

    template<>
    struct ClusterData<ETerrainBlock::Slope> : public ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Slope>* metaCluster, int id);

        GVector2D baseGradient;

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates) override;
    };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Slope> : public TerrainBlockClusterBase
    {
        enum class ESlopeDescent
        {
            Linear,
            Convex,
            Concave
        };

        enum class ESlopeContour
        {
            Linear,
            Convex,
            Concave
        };

        struct Axis
        {
            std::vector<GVector2D> pts;
            std::vector<float> lengths;

            void reserve(int pointsCount)
            {
                pts.reserve(pointsCount);
                lengths.reserve(pointsCount);
            }

            void addPoint(const GVector2D& pt)
            {
                pts << pt;
                if (pts.size() > 1)
                    lengths << (pts.back() - *(pts.rbegin() + 1)).length() + (lengths.empty() ? 0.f : lengths.back());
                else
                    lengths << 0.f;
            }

            // projection parameter, square distance
            std::array<float, 2> getProjection(const GVector2D& pt) const
            {
                const auto [nearestP, dSq, idx] = directionalBoundDistance(pts, pt, true);
                const float t = (lengths[idx] + distance(pts[idx], nearestP)) / lengths.back();
                return {t, dSq};
            }
        };

    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        inline TerrainBlockCluster(const ClusterData<ETerrainBlock::Slope>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Slope, data.cells)
        {
        }

        virtual void initialize() override;
        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

        void calculateMinMaxHeight();
        void computeSlopeAxes(ESlopeContour contour);
        void preprocessAxes();
        void buildDescentTech();

        static QVector4D getDebugColor() { return QVector4D(0.2f, 0.4f, 0.6f, 1); };
        
        float botH = -1.0f;
        float topH = -1.0f;

        std::vector<std::vector<GVector2D>> axes;
        std::vector<std::vector<float>> accLengths;

        ESlopeDescent slopeDescentType = ESlopeDescent::Linear;
        ESlopeContour slopeContour = ESlopeContour::Linear;
        QSharedPointer<Interpolation::TechniqueBase> descentGen;
        QSharedPointer<Interpolation::TechniqueBase> contourGen;

    private:

        Heightfield heightData;

        GVector2D focalPoint;
        float radius1 = 0.f;
        float radius2 = 0.f;
        bool isConvex = true;
        std::vector<Axis> guidingAxes;

        noise::module::Billow noiseSource;
        noise::model::Plane noiseModel;

    private:

        std::array<float, 2> computeAxisData(const GVector2D& p, float* maxD);

        void initializeShape();
        float calculateHeightForPoint(const GVector2D& pt);
        void computeHeightField();

        std::vector<GVector2D> createAxis();
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Slope> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Slope, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}