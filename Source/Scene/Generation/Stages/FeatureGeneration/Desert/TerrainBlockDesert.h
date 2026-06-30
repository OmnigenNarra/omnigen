#pragma once

#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include <Source\Scene\Generation\Stages\TerrainModel\DigitalElevationModel.h>
#include "Utils/CoreUtils.h"
#include "Utils/Colors.h"


struct DuneVertex;

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Desert>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Desert>& object, OmniBin<std::ios::in>& omniBin);


namespace Generation
{
    enum class EDesertBlockSubtype
    {
        DuneBarchan,
        DuneStar,
        DuneLongitudinal,
        DuneNabkha,
        DuneSand,
        Last
    };
    ENABLE_ENUM_AS_CONSTEXPR(EDesertBlockSubtype, EDesertBlockSubtype::Last);

    struct DesertSubClusterBase
    {
        DesertSubClusterBase() = default;
        DesertSubClusterBase(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster, EDesertBlockSubtype inType)
            : type(inType)
            , cluster(inCluster)
        {
        }

        virtual void generate() = 0;

        EDesertBlockSubtype type = EDesertBlockSubtype::Last;
        TerrainBlockCluster<ETerrainBlock::Desert>* cluster = nullptr;
    };

    struct DesertClusterSubDataBase
    {
        DesertClusterSubDataBase(ClusterData<ETerrainBlock::Desert>* inBaseData);

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates) = 0;
        virtual QSharedPointer<DesertSubClusterBase> createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster) = 0;

        ClusterData<ETerrainBlock::Desert>* baseData;
    };

    template<EDesertBlockSubtype Subtype>
    struct DesertClusterSubData : DesertClusterSubDataBase
    {
        DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData)
            : DesertClusterSubDataBase(inBaseData)
        {
        }

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>&) override { return {}; }
        virtual QSharedPointer<DesertSubClusterBase> createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>*) override { return {}; }
    };

    ClusterParams ClusterTraits<ETerrainBlock::Desert>::clusterParams = { .maxHeightDiff = 20000, .maxSize = 15 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Desert>::metaClusterParams = { .maxSize = 400 };

    template<>
    struct ClusterData<ETerrainBlock::Desert> : public ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Desert>* metaCluster, int id);

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates) override;

        QSharedPointer<DesertClusterSubDataBase> subData;
        int centerCell;
    };

    template<>
    class TerrainBlockMetaCluster<ETerrainBlock::Desert> : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(ETerrainBlock::Desert, inCells)
        {
        }

        virtual void initialize();
        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel) override;

        std::optional<EDesertBlockSubtype> forcedType;

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);

    protected:

        virtual void spawnClusters();
    };

    template<EDesertBlockSubtype Subtype>
    struct DesertSubCluster : DesertSubClusterBase
    {
        DesertSubCluster() = default;
        virtual void generate() override {};
    };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Desert> : public TerrainBlockClusterBase
    {
    public:
        static float chance(const BlockChanceData& data);

        TerrainBlockCluster() = default;
        TerrainBlockCluster(const ClusterData<ETerrainBlock::Desert>& data);

        static QVector4D getDebugColor() { return QVector4D(0.988f, 0.866f, 0.46f, 1.f); };

        virtual void initialize() override
        {
            smoothingParams.weight = 0.2f;
            smoothingParams.upperPriorityFactor = 1.2f;
        }

        virtual void generate() override;

        EDesertBlockSubtype subType;
        QSharedPointer<DesertSubClusterBase> subCluster;

        void fillResultMesh(MeshConnector& meshConnector);

    private:
        float getDesertNeighborsPart() const;

    };
}

// Dunes Utils --------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Computes random bezier curve, inscribed in convex hull quad. numParts means how much control points can be. widthFactor must be in range (0, 1]
BezierCurve2D computeRandomBezierCurve(const GVector2D& p0, const GVector2D& p1, const GVector2D& p2, const GVector2D& p3, int numParts, float widthFactor = 1.f);
BezierCurve2D computeRandomBezierCurve(const Polygon2D& restrictionPolygon, int numParts, float widthFactor = 1.f);

// Sand noise for more natural and smooth connection between cluster dunes
float getSandNoise(float x, float z);
float getSandHeight(const GVector2D& pt);
QVector3D get3dSandPoint(const QVector3D& pt);

// dune height helpers
DuneVertex createDuneVertexAtPoint(const GVector2D& pt);
float getDunePeakHeight(const GVector2D& pt, float min = 42.f, float max = 242.f);

struct CellsLayer
{
    std::unordered_set<int> cells;
    std::unordered_set<int> usedCells;

    float getFillingRate() const
    {
        Q_ASSERT(!cells.empty());
        return usedCells.size() / (float)cells.size();
    }

    bool isCellInLayer(int cellId) const
    {
        return cells.contains(cellId);
    }

    bool isCellUsed(int cellId) const
    {
        return usedCells.contains(cellId);
    }
};

std::unordered_set<int> customGrowWithCellsLayers(const std::unordered_set<int>& candidates, std::unordered_set<int>& clusterCells, std::vector<CellsLayer>& layers, std::unordered_set<int>& allLayersCells);

