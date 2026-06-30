#pragma once

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene\Generation\Stages\TerrainModel\DigitalElevationModel.h"

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Fault>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Fault>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    ClusterParams ClusterTraits<ETerrainBlock::Fault>::clusterParams = { .maxHeightDiff = 500, .maxSize = 20 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Fault>::metaClusterParams = { .maxSize = 20 };

    template<>
    struct ClusterData<ETerrainBlock::Fault> : public ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Fault>* metaCluster, int id);

        // Left to right
        std::vector<int> cellGraph;

        GVector2D baseDir;
        GVector2D leftEnd;
        GVector2D rightEnd;

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates) override;

        std::array<Segment2D, 2> completeFaultline(std::vector<GVector2D>* line, bool replaceEnds) const;
    };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Fault> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        TerrainBlockCluster(const ClusterData<ETerrainBlock::Fault>& data)
            : TerrainBlockClusterBase(ETerrainBlock::Fault, data.cells)
        {
        }

        virtual void initialize() override;
        virtual void clear() override;
        static QVector4D getDebugColor() { return QVector4D(0.65, 0.4, 0, 1); };
        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh() override;

        // Computes border points of each block. Used for snapping faultlines.
        void computeClusterBorderPoints();
        void computeFaultParameters();

        // Find starting point for main line 
        std::optional<GVector2D> findHeightMidPoint();
        // Create main line for fault lines as reference for their generation
        std::vector<GVector2D> calculateFaultMainLine();
        // Check for concave point in cluster polygon from mainLine point toward direction
        std::optional<GVector2D> lookForConcavePoint(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& direction);
        // Check for intersection between mainLine point and direction with segment made of cocave border point and line direction
        std::optional<GVector2D> findIntersectionWithConcaveSegment(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& direction);

        // Fix line after snapping, method is used to fix hard (over acceptable angle) turns after line is snapped
        std::pair<int, int> fixSnappedLine(std::vector<GVector2D>* line, float acceptableAngle);
        // Snap bot and top line to border points
        void snapToBorderPoints(std::vector<GVector2D>* botLine, std::vector<GVector2D>* topLine, const std::vector<GVector2D>& mainLine);
        // Apply noise on line, making shape less regular. Effect is reduced on edges of line
        void applyNoise(std::vector<GVector2D>* line, int from, int to, const std::vector<GVector2D>& mainLine);

        // Compute fault lines for meshing
        void computeFaultSegments();

        static GVector2D snapPointToBlockBorderPoint(const GVector2D& p, const std::vector<GVector2D>& singleBlockBorderPoints, float threshold = std::numeric_limits<float>::max());

        std::vector<std::vector<GVector2D>> faultSegments;
        float botH = -1.0f;
        float topH = -1.0f;
        int verticalSegmentCount = -1;
        float faultWidthVariance = 0.1f;

        QHash<int, std::array<GVector2D, 2>> inOutPtsTop;
        QHash<int, std::array<GVector2D, 2>> inOutPtsBottom;

        Polygon2D clusterPolygon;
        std::vector<GVector2D> clusterBorderPoints;

    private:
        void meshCluster(MeshConnector* meshConnector, std::unordered_set<GVector2D>* verticalSectionVertices);

        void meshVerticalSection(const std::vector<std::vector<GVector2D>>& lines, const std::array<std::array<IndexType, 2>, 2>& outers,
            std::vector<QVector3D>* generatedPoints, std::vector<IndexType>* generatedTriangles, std::unordered_set<GVector2D>* verticalSectionVertices);
    };
}