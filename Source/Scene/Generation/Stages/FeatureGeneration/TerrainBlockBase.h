#pragma once

// WARNING: Don't #include this header directly, instead #include TerrainBlock.h!

#include "Utils/CoreUtils.h"
#include "Utils/Polygon.h"
#include "Utils/Voronoi/Voronoi.h"
#include "TerrainBlockUtils.h"
#include <QEnableSharedFromThis>
#include "TerrainBlockData.h"
#include "../TerrainMods/TerrainModData.h"
#include "Utils/GeometryData.h"
#include "Scene/Generation/Stages/StageGenerationBase.h"
#include <span>
#include <unordered_set>
#include "Scene/Generation/Stages/Lithomap/LithoCluster.h"
#include "ClusterMeshMarker.h"

#include "Utils/QuadTreeLite.h"

#define DEBUG_BLOCK_SELECTION (_DEBUG && 0)

class RoadConstruction;

namespace Generation
{    
    class TerrainBlockClusterBase;
    class TerrainBlockMetaClusterBase;

    template<ETerrainBlock>
    class TerrainBlockMetaCluster;
}

// Serialization impl
void omniSave(const Generation::TerrainBlockClusterBase& object, OmniBin<std::ios::out>& omniBin);

template<Generation::ETerrainBlock TB>
void omniSave(const Generation::TerrainBlockMetaCluster<TB>&, OmniBin<std::ios::out>&);

template<Generation::ETerrainBlock TB>
void omniLoad(Generation::TerrainBlockMetaCluster<TB>& object, OmniBin<std::ios::in>& omniBin);

void omniLoad(Generation::TerrainBlockClusterBase& object, OmniBin<std::ios::in>& omniBin);


namespace Generation
{
    class DEM;
    struct BorderPoint;
    class TerrainBlockClusterBase;

    template<ETerrainBlock TB>
    class TerrainBlockCluster;

    // Used in feature placement autogen cluster construction
    struct ClusterParams
    {
        float maxHeightDiff = -1.0f;
        int maxSize = -1;
    };

    struct MetaClusterParams
    {
        int maxSize = -1;
    };

    template<ETerrainBlock TB>
    struct ClusterTraits
    {
        // Don't inline, definitions in concrete types
        static ClusterParams clusterParams;
        static MetaClusterParams metaClusterParams;
    };

    ClusterParams ClusterTraits<ETerrainBlock::Last>::clusterParams = { .maxHeightDiff = -1, .maxSize = -1 };
    MetaClusterParams ClusterTraits<ETerrainBlock::Last>::metaClusterParams = { .maxSize = -1 };

    // Struct used to build clusters in feature placement autogen
    struct ClusterDataBase
    {
        ClusterDataBase(int id)
            : cells{ id }
            , type(getType(id))
            , height(getCellHeight(id))
            , lithoId(getCellLithoId(id))
            , domains(getCellDomains(id))
        {
        };

        std::unordered_set<int> cells;
        ETerrainBlock type;
        float height;
        qint64 lithoId;
        std::unordered_set<qint64> domains; // domain guids

        // Process a set of neighboring cells, return a subset which will be added to the cluster
        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates);

        // Create a set of neighboring cells to be considered for expansion
        virtual std::unordered_set<int> computeCandidates(const std::unordered_set<int>& metaCluster, const std::unordered_set<int>& alreadyAssigned);

        static ETerrainBlock getType(int id);
        static float getCellHeight(int id);
        static std::unordered_set<qint64> getCellDomains(int id);
        static qint64 getCellLithoId(int id);
    };

    // Each cluster type may do it differently
    template<ETerrainBlock TB>
    struct ClusterData : ClusterDataBase
    {
        ClusterData(TerrainBlockMetaCluster<TB>*, int id) 
            : ClusterDataBase(id)
        {
        };
    };

    // Used for geometry "smoothing" - sewing at the border points
    struct SmoothingParams
    {
        // Border points params
        float weight = 1.f; // cluster's weight, affect final border point calculation
        float upperPriorityFactor = 1.f; // if factor is > 1, then upper vertex will have more priority than lowest
        bool useNoise = false; // apply noise for border points, to prevent straight lines and blocky surface
        float noiseFrequency = 1.f;
        float noiseAmplitude = 1.f;
        // Smoothing params
        float smoothingRadius = 300.0f; // smoothing radius
    };

    // Block cluster base class
    // Contains shared data for the included blocks needed to build a single terrain form.
    // A single terrain cluster represents a (part of) single terrain form
    // Cluster is consist of several Voronoi cells and build a single terrain form.
    // Clusters may form meta clusters to achieve greater coherence in visuals (common params for a group of same type forms).
    // The generation goes: Great blobs of same type cells -> meta clusters -> clusters -> blocks
    class TerrainBlockClusterBase : public Editable, public QEnableSharedFromThis<TerrainBlockClusterBase>
    {
    public:
        using VertexQuadTree = tml::qtree<float, IndexType>;
        using FaceQuadTree = tml::qtree<float, IndexType>;
        using BPQuadTree = tml::qtree<float, IndexType>;

        TerrainBlockClusterBase() = default;
        TerrainBlockClusterBase(ETerrainBlock inType, const std::unordered_set<int>& inCells, std::optional<int> inKeyCell = {});

        void computeBiomeData();
        void computeBorderPoints();
        virtual void initialize() {};
        virtual void generate();

        // Registers border triangle info in global border point map for future smoothing
        void registerBorderData();
        virtual QSharedPointer<BatchedSection<ClusterMeshBatchParams>> generateMesh();
        virtual void clear();

        void setGuid(qint64 inGuid) { guid = inGuid; }

        // use only before generating geometry
        void addCells(const std::unordered_set<int>& cellsToAdd, bool updateMeta = true);
        void removeCells(const std::unordered_set<int>& cellsToRemove, bool updateMeta = true);
        void setCells(const std::unordered_set<int>& newCells, bool updateMeta = true);

        const SmoothingParams& getSmoothingParams() const { return smoothingParams; }

        // Check cluster against owning terrain chunk to determine in-chunk texture indices
        void computeTexSlots();
        void computeNormals();

        // Sewing process that eliminates holes between neighboring clusters
        void smoothMesh();

        // Used for rendering
        ClusterMeshBatchParams makeBatchParams(std::optional<QVector4D> color = {}) const;

        void clearGeometryPreview() const;

        // Builds a simple polygon from cluster cells
        Polygon2D calculatePolygon(bool forceCW = false) const;
        auto& getSectionRW() { return section; }
        const auto& getGuid() const { return guid; };

        // Use quad tree to retrieve height in given 2D location. If multiple points match given coords, use Pred to sort the output.
        std::vector<QVector3D> raycastDataFrom2D(const GVector2D& in2DPt, const ComparePointPred& Pred = PointByHeightPred());

        // Use quad tree to retrieve mesh data in given 2D location. If multiple points match given coords, use Pred to sort the output.
        std::vector<MeshQueryData> raycastDataFrom2DAdv(const GVector2D& in2DPt, const ComparePointPred& Pred = PointByHeightPred());

        //Returns a quadTree of the terrain vertices of this block; each entry contains its index in the original geometry array
        const VertexQuadTree& getVertexQuadTree();
        const FaceQuadTree& getFaceQuadTree();
        const BPQuadTree& getBPQuadTree();

        template<ETerrainBlock TB>
        TerrainBlockCluster<TB>* getCasted()
        {
            Q_ASSERT(TB == type);
            return static_cast<TerrainBlockCluster<TB>*>(this);
        }

        // Used for rendering
        static QVector4D getDebugColor();

        ETerrainBlock type;
        std::unordered_set<int> cells;

        // This is used in many places as the "cluster id".
        // There is a cell id -> cluster ptr map, which is then used to decode the id
        int keyCell;

        QHash<int, std::vector<BorderPointInfo>> borderPoints;
        std::unordered_map<QVector3D, float> smoothingMultiplierMap;

        // In-TerrainChunk indices
        int terrainTexPackSlot = -1;
        int biomeTexPackSlot = -1;

        float smoothingRange = std::numeric_limits<float>::max();
        std::array<float, 2> temperatureRange = {0, 0};
        std::array<float, 2> humidityRange = {0, 0};

        QSharedPointer<TerrainBlockMetaClusterBase> metaCluster;

        // Geometry is here, a part of the batched stuff; 1 meta cluster = 1 geometry
        QSharedPointer<BatchedSection<ClusterMeshBatchParams>> section;

    protected:
        qint64 guid;

        void registerBorderFace(const std::vector<TerrainMeshVertex>& vertexBuffer, const std::span<IndexType>& face, quint32 fi);

        struct WeightedBorderPoint
        {
            const BorderPointInfo& bpInfo;
            float weight;
        };

        // Performs the final blend
        void applySmoothing(TerrainMeshVertex* tmv, const std::vector<WeightedBorderPoint>& weightedBPs, float smoothingStrength);

        void calculateFacePointQuadTree();
        QSharedPointer<FaceQuadTree> faceQuadTree;

        void calculateVertexQuadTree();
        QSharedPointer<VertexQuadTree> vertexQuadTree;

        void calculateBPQuadTree();
        QSharedPointer<BPQuadTree> bpQuadTree;

        SmoothingParams smoothingParams;

        FRIEND_OMNIBIN_NS(TerrainBlockClusterBase);
    };

    template<ETerrainBlock TB>
    class TerrainBlockCluster : public TerrainBlockClusterBase
    {
    };

    template<>
    class TerrainBlockCluster<ETerrainBlock::Last> : public TerrainBlockClusterBase
    {
    public:
        // Returns a weight that is used in block type auto-assignment (Terrain Classification)
        // Weights >= 1 are considered in a separate pass and should be used only in must-appear cases
        // Static polymorphism: Implement this in block classes!
        static inline float chance(const BlockChanceData& data) { return 0.f; };

        TerrainBlockCluster() = default;
        inline TerrainBlockCluster(const ClusterData<ETerrainBlock::Last>&) { Q_ASSERT(false); }
    };

    class TerrainBlockMetaClusterBase : public Editable, public QEnableSharedFromThis<TerrainBlockMetaClusterBase>
    {
    public:
        TerrainBlockMetaClusterBase(ETerrainBlock inType, const std::unordered_set<int>& inCells);

        const auto& getClusters() const { return clusters; }
        const auto& getType() const { return type; }
        const auto& getCells() const { return cells; }
        const auto& getGuid() const { return guid; }
        const auto& getTerrainTexPack() const { return terrainTexPack; }
        const auto& getBiomeTexPack() const { return biomeTexPack; }
        const auto& getPackParams() const { return packParams; }
        std::unordered_set<int> selectNonClusterCells() const;
        std::unordered_set<int> selectClusterCells() const;
        std::vector<std::unordered_set<int>> selectCellsPerCluster() const;
        Polygon2D calculatePolygon(bool forceCW = false) const;
        virtual void spawnClusters();

        void setGuid(qint64 inGuid) { guid = inGuid; }

        // Use only on load
        void setType(ETerrainBlock inType) { type = inType; }
        void setCells(const std::unordered_set<int>& inCells) { cells = inCells; };

        void setTerrainTexPack(quint32 inTerrainTexPack) { terrainTexPack = inTerrainTexPack; };
        void setBiomeTexPack(quint32 inBiomeTexPack) { biomeTexPack = inBiomeTexPack; };
        void setPackParams(quint32 inPackParams) { packParams = inPackParams; };

        // Mind that add/remove cells won't update clusters
        void addCells(const std::unordered_set<int>& inCells);
        void removeCells(const std::unordered_set<int>& inCells);

        void addCluster(const QSharedPointer<TerrainBlockClusterBase>& cluster);
        void removeCluster(const QSharedPointer<TerrainBlockClusterBase>& cluster);
        // Resets current cells status
        void setClusters(const std::vector<QSharedPointer<TerrainBlockClusterBase>>& newClusters);

        virtual void initialize();

        // Partial autogen used by tools
        void spawnBigClusters();

        void generate();

        template<ETerrainBlock TB>
        TerrainBlockMetaCluster<TB>* getCasted()
        {
            Q_ASSERT(TB == type);
            return static_cast<TerrainBlockMetaCluster<TB>*>(this);
        }

    protected:
        TerrainBlockMetaClusterBase() = default; // for loading

        virtual void computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel);

        void computeBaseMaterial();

        std::vector<QSharedPointer<TerrainBlockClusterBase>> clusters;
        ETerrainBlock type;
        std::unordered_set<int> cells;
        qint64 guid;

        quint32 terrainTexPack = quint32(-1);   // filled in computeBaseMaterial()
        quint32 biomeTexPack = quint32(-1);     // filled in computeBaseMaterial()
        quint32 packParams = 0;                 // filled in computeBaseMaterial()

        friend TerrainBlockClusterBase;
    };

    template<ETerrainBlock TB>
    class TerrainBlockMetaCluster : public TerrainBlockMetaClusterBase
    {
    public:
        TerrainBlockMetaCluster() = default; // for loading
        TerrainBlockMetaCluster(const std::unordered_set<int>& inCells)
            : TerrainBlockMetaClusterBase(TB, inCells)
        {
        }

        FRIEND_OMNIBIN_NS(TerrainBlockMetaCluster);
    };
}

// Base block utils
std::unordered_set<int> customGrowFilterIslands(const std::unordered_set<int>& candidates, std::unordered_set<int>* clusterCells);
std::array<QVector3D, 2> getMinMaxHeightCellPts(const Polygon2D& cell);

template<Generation::ETerrainBlock TB>
inline void omniSave(const Generation::TerrainBlockMetaCluster<TB>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.clusters.size();
    for (auto&& cluster : object.clusters)
        omniBin << cluster.staticCast<Generation::TerrainBlockCluster<TB>>();

    omniBin << object.guid;
    omniBin << object.terrainTexPack;
    omniBin << object.biomeTexPack;
    omniBin << object.packParams;
}

template<Generation::ETerrainBlock TB>
inline void omniLoad(Generation::TerrainBlockMetaCluster<TB>& object, OmniBin<std::ios::in>& omniBin)
{
    size_t clustersNum;
    omniBin >> clustersNum;

    object.clusters.reserve(clustersNum);
    for (size_t i = 0; i < clustersNum; ++i)
    {
        QSharedPointer<Generation::TerrainBlockCluster<TB>> cluster;
        omniBin >> cluster;
        object.clusters << cluster;
        cluster->metaCluster = object.sharedFromThis();
    }

    omniBin >> object.guid;
    omniBin >> object.terrainTexPack;
    omniBin >> object.biomeTexPack;
    omniBin >> object.packParams;
}

constexpr auto bsize = sizeof(Generation::TerrainBlockClusterBase);