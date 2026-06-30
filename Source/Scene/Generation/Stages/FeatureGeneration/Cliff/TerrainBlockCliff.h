#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include <Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h>
#include <Scene/Generation/Stages/Landmasses/ShorelineMarker.h>
#include "Utils/Triangulation/Triangulation.h"

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Cliff>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Cliff>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Cliff>::clusterParams = { .maxHeightDiff = std::numeric_limits<float>::max(), .maxSize = std::numeric_limits<int>::max() };
    MetaClusterParams ClusterTraits<ETerrainBlock::Cliff>::metaClusterParams = { .maxSize = std::numeric_limits<int>::max() };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Cliff> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        inline TerrainBlockCluster(const ClusterData<ETerrainBlock::Cliff>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Cliff, data.cells)
        {}

        virtual void initialize() override;
        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;
        //virtual void computeNormals() override {};
        void computeNormalsDuringMeshGen();

        static QVector4D getDebugColor() { return QVector4D(0.3f, 0.3f, 0.1f, 1); };

    private:
        std::vector<std::vector<GVector2D>> makeLines() const;

        void computeClusterBorderPoints();
        void computeFaultParameters();
        void computeFaultSegments();
        std::optional<GVector2D> findHeightMidPoint();
		std::vector<GVector2D> calculateFaultMainLine();
		std::optional<GVector2D> lookForConcavePoint(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& lineDir);
		std::optional<GVector2D> findIntersectionWithConcaveSegment(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& lineDir);
        void meshCluster(MeshConnector* meshConnector, std::unordered_set<GVector2D>* verticalSectionVertices);
		void meshVerticalSection(const std::vector<std::vector<GVector2D>>& lines, const std::array<std::array<IndexType, 2>, 2>& outers,
			std::vector<QVector3D>* generatedPoints, std::vector<IndexType>* generatedTriangles, std::unordered_set<GVector2D>* verticalSectionVertices);
        std::pair<int, int> fixSnappedLine(std::vector<GVector2D>* line, float acceptableAngle);
		void snapToBorderPoints(std::vector<GVector2D>* botLine, std::vector<GVector2D>* topLine, const std::vector<GVector2D>& mainLine);
		void applyNoise(std::vector<GVector2D>* line, int from, int to, const std::vector<GVector2D>& mainLine);

		std::vector<std::vector<GVector2D>> faultSegments;
		float botH = -1.0f;
		float topH = -1.0f;
        Polygon2D clusterPolygon;
        std::vector<GVector2D> clusterBorderPoints;

        static inline const float density = 1000.f;
        static inline const int densityMultiplier = 10;

        float shorelineDistance(const GVector2D& point) const;
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Cliff> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Cliff, inCells)
        {
        }

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}