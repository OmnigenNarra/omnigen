#pragma once
#include <vector>
#include <QObject>
#include <QSharedPointer>
#include "Constants.h"
#include "Scene/Generation/Stages/Layout/Data/DomainDataBase.h"
#include "OmnigenGenerationStage.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Utils/ITree.h"
#include "Stages/FeatureGeneration/TerrainBlockData.h"
#include "Stages/TerrainMods/TerrainModData.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Editable.h"
#include "Utils/QuadTreeLite.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Common/Objects/UrbanMeshDrawable.h"
#include "Utils/TriangleAdjacencyUtils.h"
#include "Common/Markers/BatchMarker.h"

class RuralRoadGenerator;
class DDomainSquare;
class DTerrainChunk;
class DMarker;
class DRidgeMarker;
class DDomainHandle;

struct IsohypseBatchParams;
template<> struct BatchedSection<IsohypseBatchParams>;
using Isohypse = BatchedSection<IsohypseBatchParams>;

namespace Voronoi
{
    class BoxDiagram;
}

namespace Design
{
    template<EGenerationStage>
    class StageTools;
}

namespace Generation
{
    class GrassSpawner;
    class UrbanSuggestion;
    class LithoCluster;
    struct BorderPoint;
}

namespace Generation
{
    class DEM;
    class TerrainBlockClusterBase;
    class TerrainModBase;
    class UrbanSite;

    template<ETerrainMod>
    class TerrainMod;

    // Used for terrain rendering data, unpacks a single weight from uint32 (containing 4 weights)
    float getTexWeight(quint32 weights, quint32 idx);

    // Used for terrain rendering data, unpacks a single param from uint32 (containing up to 4 params)
    float getPackParam(quint32 params, quint32 idx);

    // 1 Byte = 256 values; Tex Slot range = [0.0f..2.0f], where 0.0 = Solid Rock, 1.0 = Gravel, 2.0 = Soil;
    constexpr float texSlotConverter = 255.0 / 2.0;
    constexpr float texSlotDeconverter = 1.0 / texSlotConverter;

    // Merge up to 4 values into a single uint32
    quint32 compileTexWeights(const std::vector<float>& weights);
    quint32 compilePackParams(const std::vector<float>& params);
    void setPackParam(quint32* params, int slot, float newValue);

    // Used for cluster smoothing, geometry on both sides of the borders must match height at the Border Points
#pragma pack(push, 1)
    struct BorderPoint
    {
        struct PerClusterData
        {
            TerrainMeshVertex v;
            std::map<int /*block id*/, std::vector<quint32>> facesPerBlock;
        };

        mutable std::map<int /*cluster key cell*/, PerClusterData> intermediateData;
        mutable QVector3D normal;
    };
#pragma pack(pop)

    // Env bounds are used by Urban autogen to know where the roads cannot go.
    struct EnvBound
    {
        EnvBound();
        EnvBound(int inIdx);

        qint64 guid;
        int cellIdx;
        bool flipDir = false;
        std::vector<QVector3D> line;
        std::vector<QWeakPointer<EnvBound>> pairedBounds;

        GVector2D getNorm(int lineIndex) const;
    };

    // Height bounds are used by Isohypses to choose local increments during autogen
    enum class HeightBoundOrigin
    {
        Domain,
        Shoreline,
        Ridge,
        Isohypse
    };

    // A great database-like singleton for data storage.
    // Data that contributes to the scene / further generation is stored here.
    class Data : public QObject
    {
        Q_OBJECT
            using BlockQuadTree = std::shared_ptr<tml::qtree<float, IndexType>>;

        // Guard for registerBorderFacePoint
        static inline std::mutex bpGuard; 

        template<typename LineMarkerType>
        static inline std::mutex markerQTreeGuard;

        template<typename LineMarkerType>
        static inline tml::qtree<float, LineMarkerPoint>* markerQTree = nullptr;

    public:
        Data();

        // Singleton access
        static Data* get();

        EGenerationStage getGenerationStage() const { return generationStage; };
        std::optional<EGenerationStage> getStageBeingGenerated() const { return stageBeingGenerated; };
        const auto& getDomainSquares() const { return domainSquares; };
        const auto& getDEM() const { return dem; };
        const BlockQuadTree& getBlockQuadTree();
        const auto& getLargestVoronoiCellRadius() { return largestCellRadius; };
        const auto& getTerrainCells() const { return terrainCells; };
        auto getLithomap() const { return std::tie(lithoMap, lithoClusters); };
        const auto& getTerrainTextureArray() const { return terrainTextures; }
        const auto& getCoverTextureArray() const { return coverTextures; }
        const auto& getTerrainTexturePacks() const { return terrainTexPacks; }
        const auto& getBiomeTexturePacks() const { return biomeTexPacks; }
        const auto& getTerrainMetaClusters() const { return terrainMetaClusters; };
        const auto& getTerrainClustersMap() const { return terrainClustersMap; };
        const auto& getTerrainBorderPoints() const { return gBorderPoints; };
        const auto& getTerrainMods() const { return terrainMods; };
        const auto& getTriangleAdjList() const { return triangleAdJList; }
        const auto& getUrbanSuggestions() const { return urbanSuggestions; }
        const auto& getUrbanSites() const { return urbanSites; }
        const auto& getRuralRoadGenerator() const { return ruralRoadGenerator; }
        QSharedPointer<UrbanSite> findUrbanSiteByGuid(const qint64 id) const;
        QSharedPointer<UrbanSuggestion> findUrbanSuggestionByGuid(const qint64 id) const;
        const auto& getEnviroBounds() const { return enviroBounds; }
        const auto& getDomainHeightBounds() const { return domainHeightBounds; }
        auto getTerrainChunkData() const { return std::forward_as_tuple(terrainChunks, chunkBlocksMap, blockChunkMap); }
        const auto& getUrbanMeshes() const { return urbanMeshes; }
        GVector2D getWindVector(const GVector2D& p);

        const auto& getBlockTypeMap() { return blockTypeMap; }
        auto* getModBackupData() { return &modBackupData; }

        std::vector<std::vector<QSharedPointer<Isohypse>>> getIsohypseMarkersByLevel() const;
        QSharedPointer<DDomain> getDomainAtSquare(const GPoint& sq, EDomainType dt) const;
        const std::map<EDomainType, QSharedPointer<DDomain>>& getDomainsAtSquare(const GPoint& sq) const;
        std::vector<QSharedPointer<DDomainHandle>> getDomainHandlesAt(const QVector3D& pos, EDomainType dt = EDomainType::Last);
        std::optional<QSharedPointer<DDomain>> findDomainByGuid(qint64 id) const;
        std::optional<QSharedPointer<DDomainHandle>> findDomainHandleByGuid(qint64 id) const;

        void setGenerationStage(EGenerationStage newStage, bool generate, bool validate, bool justOpened = false);
        void setCurrentGeneratedStage(std::optional<EGenerationStage> stage) { stageBeingGenerated = stage; };

        void createDomainSquares(const QSet<GPoint>& squares);
        void clearDomainSquares(const QSet<GPoint>& squares, bool force);

        void addDomain(const QSharedPointer<DDomainHandle>& inHandle, const QSharedPointer<DDomain>& inDomain);
        void restoreDomain(const std::tuple<DDomainHandle, DDomain, int>& data);
        void removeDomain(int idx);
        void removeDomain(const QSharedPointer<DDomainHandle>& inHandle);
        void clearBlockQuadTree();

        void setDEM(const QSharedPointer<Generation::DEM>& inDEM);
        void setLargestVoronoiCellRadius(const float radius);
        void setTerrainCells(const QSharedPointer<Voronoi::BoxDiagram>& diagram);
        void setLithomap(const std::vector<int> inLithomap, const std::vector<QSharedPointer<LithoCluster>>& inClusters);
        void setBlockTypeMap(const std::vector<ETerrainBlock>& inTypeMap);
        void setBlockTypeForCell(int idx, ETerrainBlock bt) { blockTypeMap[idx] = bt; };
        void setTerrainClusters(const QMap<ETerrainBlock, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>>& metaClusters, const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& clustersMap);
        void setClusterForCell(int idx, const QSharedPointer<Generation::TerrainBlockClusterBase>& cluster);
        void addMetaCluster(ETerrainBlock bt, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster) { terrainMetaClusters[bt] << metaCluster; };
        void removeMetaCluster(ETerrainBlock bt, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster);
        void registerBorderFacePoint(int clusterKey, int cellIdx, const TerrainMeshVertex& p, quint32 fi);
        void setDomainHeightBounds(const std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>& inHeightBounds);

        void addEnviroBound(const QSharedPointer<EnvBound>& bound);

        void setTerrainMods(const QMap<ETerrainMod, std::vector<QSharedPointer<Generation::TerrainModBase>>>& mods);
        void setTriangleAdjList(const std::unordered_map<std::pair<int, int>, std::shared_ptr<TrianglesGraph::TriangleAdjacencyGraph::Node>>& inList);
        void addTerrainMod(ETerrainMod type, const QSharedPointer<Generation::TerrainModBase>& mod);
        void removeTerrainMod(ETerrainMod type, qint64 guid);
        void setUrbanSuggestions(const std::vector<QSharedPointer<Generation::UrbanSuggestion>>& suggestions);
        void setUrbanSites(const std::vector<QSharedPointer<Generation::UrbanSite>>& sites);
        void setRuralRoadGenerator(const QSharedPointer<RuralRoadGenerator>& generator) { ruralRoadGenerator = generator; }
        void setUrbanMeshes(const std::vector<QSharedPointer<DUrbanMesh>>& inUrbanMeshes);

        void initTerrainChunks(const std::vector<QSharedPointer<DTerrainChunk>>& chunks, const QHash<int, QSet<int>>& inChunkBlocksMap, const QHash<int, int>& inBlockChunkMap);
        void emptyTerrainChunks();
        void clearTerrainChunks();

        void finalizeBorderPoints();
        void computeTextureArrays();
        void setBorderPoints(const QHash<GVector2D, Generation::BorderPoint>& bPts);
        void setEnviroBounds(const QHash<int, std::vector<QSharedPointer<EnvBound>>>& inBounds);
        void clearBorderPoints();
        void drawBorderPoints() const;

        void clearMarkers();
        void clearDebugMarkers();
        void clearDomains();
        void updateDomainSquaresMap(const QSharedPointer<DDomain>& domain, const QSet<GPoint>& oldSquares, const QSet<GPoint>& newSquares);

        // Markers are created uninitialized and queued by default, the following ensures that vbos are updated on the main thread.
        void initializeQueuedMarkers();

        template<EDomainType DT = EDomainType::Last>
        decltype(auto) getAllDomains() const
        {
            if constexpr (DT == EDomainType::Last)
            {
                return static_cast<const decltype(domains)&>(domains);
            }
            else
            {
                decltype(domains) result;
                for (auto&& [handle, domain] : domains)
                    if (domain->getType() == DT)
                        result << QPair{ handle, domain };

                return result;
            }
        }

        template<EDomainType DT = EDomainType::Last>
        QSet<GPoint> getAllSquares() const
        {
            QSet<GPoint> result;

            for (auto&& [handle, domain] : domains)
                if (domain->getType() == DT || DT == EDomainType::Last)
                    result += domain->getSquares();

            return result;
        }

        std::mutex markerGuard;

        // Create a new marker, remember to call initializeQueuedMarkers later if CreateImmediately is false
        template<typename T, bool CreateImmediately = false, typename... Args>
        auto createMarker(const Args&... args)
        {
            auto newMarker = QSharedPointer<T>::create(args...);

            if constexpr (!CreateImmediately)
            {
                std::scoped_lock lock(markerGuard);
                if constexpr (std::is_base_of_v<ITree<T>, T>)
                    markersToInitialize.push_back({ newMarker, typeid(T).hash_code(), newMarker->isRoot() });
                else
                    markersToInitialize.push_back({ newMarker, typeid(T).hash_code(), true });
            }
            else
            {
                newMarker->initialize();
                if constexpr (std::is_base_of_v<ITree<T>, T>)
                {
                    if (newMarker->isRoot())
                        markers[typeid(T).hash_code()].push_back(newMarker);
                }
                else
                {
                    markers[typeid(T).hash_code()].push_back(newMarker);
                }
                emit Editable::created(newMarker);
            }

            return newMarker;
        }

        // Adds an existing / loaded marker to the rendered list
        template<typename T>
        void addMarker(const QSharedPointer<T>& marker)
        {
            std::scoped_lock lock(markerGuard);
            markersToInitialize.push_back({ marker, typeid(T).hash_code(), true });
        }

        // Same as addMarker, but with more controls
        void addMarkerDynamic(const QSharedPointer<DMarker>& marker, std::size_t code, bool push)
        {
            if (push)
                markers[code].push_back(marker);

            emit Editable::created(code, marker);
        }

        void removeMarkerFromInit(const QSharedPointer<DMarker>& marker)
        {
            std::scoped_lock lock(markerGuard);
            auto&& markerIter = std::find_if(markersToInitialize.begin(), markersToInitialize.end(), [&](std::tuple<QSharedPointer<DMarker>, std::size_t, bool>& t)
                { return std::get<0>(t).data() == marker.data(); });

            if(markerIter != markersToInitialize.end()) markersToInitialize.erase(markerIter);
        }

        template<typename T>
        std::vector<QSharedPointer<T>> getMarkers() const
        {
            std::vector<QSharedPointer<T>> result;

            for (auto&& markerVec : markers)
                for (auto&& m : markerVec)
                    if (auto cm = m.dynamicCast<T>())
                        result << cm;

            return result;
        }

        template<typename T>
        const auto& getExactMarkersFast() const 
        { 
            if (auto it = markers.constFind(typeid(T).hash_code()); it != markers.constEnd())
                return *it;

            static const std::vector<QSharedPointer<DMarker>> dummy;
            return dummy; 
        };

        template<typename T>
        void getExactTreeMarkersFlat(std::vector<QSharedPointer<T>>* output, const QSharedPointer<T>& current = nullptr) const
        {
            if (!current)
            {
                auto&& targets = markers[typeid(T).hash_code()];
                for (auto&& root : targets)
                    getExactTreeMarkersFlat(output, root.staticCast<T>());
            }
            else
            {
                output->push_back(current);
                for (auto&& child : current->getChildren())
                    getExactTreeMarkersFlat(output, child);
            }
        }

        template<typename T>
        void clearExactMarkers()
        {
            if constexpr (std::is_base_of_v<ITree<T>, T>)
            {
                std::vector<QSharedPointer<T>> allMarkers;
                getExactTreeMarkersFlat<T>(&allMarkers);
                for (auto&& t : allMarkers)
                    emit Editable::aboutToBeDeleted(t);
            }
            else
            {
                auto&& targets = markers[typeid(T).hash_code()];
                for (auto&& t : targets)
                    emit Editable::aboutToBeDeleted(t);
            }

            markers.remove(typeid(T).hash_code());

            if constexpr (std::is_base_of_v<DLineMarker, T>)
            {
                if (markerQTree<T>)
                {
                    delete markerQTree<T>;
                    markerQTree<T> = nullptr;
                }
            }
        }

        void clearSingleExactMarker(size_t typeHash, const qint64 guid)
        {
            auto&& targets = markers[typeHash];

            auto markerIt = std::find_if(targets.begin(), targets.end(), [guid](auto&& marker) { return marker->getGuid() == guid; });
            if (markerIt == targets.end())
                return;

            emit Editable::aboutToBeDeleted(*markerIt);
            targets.erase(markerIt);
        }

        template<typename T>
        void clearSingleExactMarker(const qint64 guid)
        {
            clearSingleExactMarker(typeid(T).hash_code(), guid);
        }

        // Basic serialization for markers
        template<typename T>
        void saveMarkers(OmniBin<std::ios::out>& writer)
        {
            auto&& target = markers[typeid(T).hash_code()];
            std::vector<QSharedPointer<T>> markersToSave(target.size());

            for (size_t i = 0; i < target.size(); ++i)
                markersToSave[i] = target[i].staticCast<T>();

            writer << markersToSave;
        }

        template<typename T>
        void loadMarkers(OmniBin<std::ios::in>& reader)
        {
            std::vector<QSharedPointer<T>> markersLoaded;
            reader >> markersLoaded;

            auto&& target = markers[typeid(T).hash_code()];
            target = { markersLoaded.begin(), markersLoaded.end() };

            auto initMarker = [&](const QSharedPointer<T>& m)
            {
                m->initialize();
                emit Editable::created(m);
            };

            for (auto&& marker : markersLoaded)
            {
                initMarker(marker);

                if constexpr (std::is_base_of_v<ITree<T>, T>)
                    marker->forEachChild(initMarker);
            }
        }

        template<typename T>
        QSharedPointer<T> findMarkerByGuid(const qint64 guid) const
        {
            auto&& targets = markers[typeid(T).hash_code()];
            for (auto&& marker : targets)
                if (marker->getGuid() == guid)
                    return marker.staticCast<T>();

            for (auto&& [marker, tid, push] : markersToInitialize)
                if (marker->getGuid() == guid)
                    return marker.staticCast<T>();

            return {};
        }

        // Where guidChain.first() is wanted marker, guidChain.last() is root (with no parent) marker
        template<typename T>
        QSharedPointer<T> findChildMarkerByGuidChain(const std::vector<qint64>& guidChain) const
        {
            QSharedPointer<T> marker = nullptr;

            for (int i = guidChain.size() - 1; i >= 0; i--)
            {
                if(i == guidChain.size() - 1)
                    marker = findMarkerByGuid<T>(guidChain.back());
                else
                    for (auto&& child : marker->getChildren())
                    {
                        if (child->getGuid() == guidChain[i])
                        {
                            marker = child;
                            break;
                        }
                    }
            }

            return marker;
        }

        template<ETerrainMod TM>
        QSharedPointer<TerrainMod<TM>> findModByGuid(const qint64 guid) const
        {
            for (auto&& modVec : terrainMods)
                for (auto&& mod : modVec)
                    if (mod->getGuid() == guid)
                        return mod.staticCast<TerrainMod<TM>>();

            return {};
        }

        // Builds marker qtree from all points of all markers of given type.
        // Each data point contains a pointer to the source marker and an index in that marker
        template<typename LineMarkerType>
        const auto& getMarkerQuadTree() const
        {
            std::scoped_lock lock(markerQTreeGuard<LineMarkerType>);
            auto&& qtree = markerQTree<LineMarkerType>;
            if (!qtree) [[unlikely]]
            {
                // 3 squares lookup margin
                constexpr float minCoord = -3 * GRID_SEGMENT_WIDTH;
                constexpr float maxCoord = (GRID_SEGMENT_COUNT + 3) * GRID_SEGMENT_WIDTH;
                qtree = new tml::qtree<float, LineMarkerPoint>(minCoord, maxCoord, maxCoord, minCoord);

                if constexpr (std::is_base_of_v<ITree<LineMarkerType>, LineMarkerType>)
                {
                    std::vector<QSharedPointer<LineMarkerType>> markers;
                    getExactTreeMarkersFlat<LineMarkerType>(&markers);
                    for (auto&& marker : markers)
                    {
                        auto&& pts = marker->getControlPoints();
                        for (int i = 0; i < pts.size(); ++i)
                        {
                            auto&& p = pts[i];
                            qtree->add_node(p.x(), p.z(), LineMarkerPoint{ marker.get(), i });
                        }
                    }
                }
                else
                {
                    for (auto&& markerUncasted : getExactMarkersFast<LineMarkerType>())
                    {
                        auto* marker = static_cast<const LineMarkerType*>(markerUncasted.get());
                        auto&& pts = marker->getControlPoints();
                        for (int i = 0; i < pts.size(); ++i)
                        {
                            auto&& p = pts[i];
                            qtree->add_node(p.x(), p.z(), LineMarkerPoint{ marker, i });
                        }
                    }
                }
            }

            return *qtree;
        }

    private:
        void saveEnvBounds(OmniBin<std::ios::out>& writer);
        void loadEnvBounds(OmniBin<std::ios::in>& reader);
        void calculateBlockQuadTree();

        EGenerationStage generationStage = EGenerationStage::Layout;
        std::optional<EGenerationStage> stageBeingGenerated;
        std::vector<QPair<QSharedPointer<DDomainHandle>, QSharedPointer<DDomain>>> domains;
        QMap<size_t, std::vector<QSharedPointer<DMarker>>> markers;
        std::array<std::array<QSharedPointer<DDomainSquare>, GRID_SEGMENT_COUNT>, GRID_SEGMENT_COUNT> domainSquares;
        QSharedPointer<Generation::DEM> dem;
        QSharedPointer<Voronoi::BoxDiagram> terrainCells;
        std::vector<int> lithoMap;
        std::vector<QSharedPointer<LithoCluster>> lithoClusters;
        QMap<ETerrainBlock, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>> terrainMetaClusters;
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> terrainClustersMap;
        QHash<GVector2D, BorderPoint> gBorderPoints;
        std::vector<qint64> terrainTextures;
        std::vector<qint64> coverTextures;
        QMap<qint64, size_t> terrainTexPacks;
        QMap<qint64, size_t> biomeTexPacks;
        QMap<ETerrainMod, std::vector<QSharedPointer<Generation::TerrainModBase>>> terrainMods;
        QHash<int, std::vector<QSharedPointer<EnvBound>>> enviroBounds;
        std::vector<QSharedPointer<Generation::UrbanSuggestion>> urbanSuggestions;
        std::vector<QSharedPointer<Generation::UrbanSite>> urbanSites;
        QSharedPointer<RuralRoadGenerator> ruralRoadGenerator;

        BlockQuadTree blockQuadTree = nullptr;
        std::vector<QSharedPointer<DTerrainChunk>> terrainChunks;
        QHash<int, QSet<int>> chunkBlocksMap;
        QHash<int, int> blockChunkMap;
        std::vector<QSharedPointer<DUrbanMesh>> urbanMeshes;

        // Generation support data
        std::vector<ETerrainBlock> blockTypeMap;
        QHash<int, QHash<IndexType, TerrainMeshVertex>> modBackupData;
        float largestCellRadius;
        std::map<qint64 /*domain 1*/, std::map<HeightBoundOrigin, std::map<qint64 /*bound object*/, std::map<int /*height*/, std::vector<Segment2D>>>>> domainHeightBounds;
        //std::map<quint64 /*domain 1*/, std::map<quint64 /*domain 2*/, std::tuple<std::map<int /*height*/, std::vector<Segment2D>>, std::pair<qint64, HeightBoundOrigin>/*origin info*/>>> domainHeightBounds;
        std::unordered_map<GPoint, std::map<EDomainType, QSharedPointer<DDomain>>> domainSquareMap;

        // Marker queue
        std::vector<std::tuple<QSharedPointer<DMarker>, std::size_t, bool>> markersToInitialize;

        //Triangle adjacency
        std::unordered_map<std::pair<int, int>, std::shared_ptr<TrianglesGraph::TriangleAdjacencyGraph::Node>> triangleAdJList;

        template<EGenerationStage>
        friend class Design::StageTools;
    };
}

void omniSave(const Generation::EnvBound& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::EnvBound& object, OmniBin<std::ios::in>& omniBin);

void omniSave(const Generation::BorderPoint& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::BorderPoint& object, OmniBin<std::ios::in>& omniBin);

void omniSave(const Generation::BorderPoint::PerClusterData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::BorderPoint::PerClusterData& object, OmniBin<std::ios::in>& omniBin);

// Main method of creating new markers
template<typename M, bool SpawnImmediately = false, typename... Ts>
QSharedPointer<M> spawn(const Ts&... args)
{
    return Generation::Data::get()->createMarker<M, SpawnImmediately>(args...);
}

// Main method of creating new batching markers
template<typename VertexType, typename BatchParams>
QSharedPointer<BatchedSection<BatchParams>> spawnBatched(GeometryData<VertexType>&& geometry, BatchParams batchParams, std::optional<qint64> guid = std::nullopt)
{
	static_assert(std::is_same_v<VertexType, typename BatchParams::VertexType>);
	auto&& instance = gBatchingMarkerInstance<BatchParams>;
	auto&& instanceGuard = gBatchingMarkerInstanceGuard<BatchParams>;

	// Critical section #1: Instance
	if (std::scoped_lock lock(instanceGuard); !instance)
		instance = spawn<DBatchingMarker<BatchParams>>();

	// Critical section #2: Batch map
	BatchData<BatchParams>* batch = nullptr;
	if (std::scoped_lock mapLock(instance->batchesGuard); true)
		batch = &instance->batches[batchParams];

	// Critical section #3: Batch
	std::scoped_lock batchLock(batch->guard);
    
	// Create new part
	auto section = QSharedPointer<BatchedSection<BatchParams>>::create(batchParams, batch->geometry.staticCast<GeometryDataBase>());
    if (guid)
        section->setGuid(*guid);

	// Insert vertex data
	section->setVertexBufferSize(geometry.vertices.size());
	section->setVertexBufferOffset(fitIntoBuffer(std::move(geometry.vertices), &batch->vertexHoles, &batch->geometry->vertices));

	// Update index array
	for (auto&& idx : geometry.indices)
		idx += section->getVertexBufferOffset();

	// Insert index data
	section->setIndexBufferSize(geometry.indices.size());
	section->setIndexBufferOffset(fitIntoBuffer(std::move(geometry.indices), &batch->indexHoles, &batch->geometry->indices));

	// Query vbo update
	batch->bNeedsVBOUpdate = true;

    // Reset bounding box
    batch->cachedBoundingBox = {};

	batch->sections[section->getGuid()] = section;
	return section;
}

template<typename BatchParams>
void despawnBatched(const QSharedPointer<BatchedSection<BatchParams>>& section)
{
	auto& instance = gBatchingMarkerInstance<BatchParams>;
	auto& instanceGuard = gBatchingMarkerInstanceGuard<BatchParams>;

	// Critical section #1: Instance
	if (std::scoped_lock lock(instanceGuard); !instance)
		return;

    Editable::aboutToBeDeleted(section);

	// Critical section #2: Batch map
	BatchData<BatchParams>* batch = nullptr;
	if (std::scoped_lock mapLock(instance->batchesGuard); true)
		batch = &instance->batches.at(section->batchParams);

	// Critical section #3: Batch
	std::scoped_lock batchLock(batch->guard);
	std::memset(&batch->geometry->vertices[section->getVertexBufferOffset()], 0, section->getVertexBufferSize() * sizeof(QVector3D));
	std::memset(&batch->geometry->indices[section->getIndexBufferOffset()], 0, section->getIndexBufferSize() * sizeof(IndexType));
	batch->sections.erase(section->getGuid());

	// Add hole
	batch->vertexHoles.push_back({ section->getVertexBufferOffset(), section->getVertexBufferSize() });
	batch->indexHoles.push_back({ section->getIndexBufferOffset(), section->getIndexBufferSize() });

	batch->bNeedsHolesUpdate = true;
	batch->bNeedsVBOUpdate = true;

    // Reset bounding box
    batch->cachedBoundingBox = {};

	// Don't remove batches, they are likely to be reused.
}

template<typename BatchParams>
void updateBatch(const BatchParams& batchParams)
{
    auto&& instance = gBatchingMarkerInstance<BatchParams>;
    Q_ASSERT(instance);

    // Critical section #3: Batch
    auto&& batch = instance->batches.at(batchParams);
    std::scoped_lock batchLock(batch.guard);

    // Query vbo update
    batch.bNeedsVBOUpdate = true;
    batch.cachedBoundingBox = {};
}

template<typename BatchParams>
void clearAllBatches()
{
	auto& instance = gBatchingMarkerInstance<BatchParams>;
	auto& instanceGuard = gBatchingMarkerInstanceGuard<BatchParams>;

	// Critical section #1: Instance
	if (std::scoped_lock lock(instanceGuard); !instance)
		return;

	// Critical section #2: Batch map
	// Note: Do not attempt this when a parallel operation may be spawning or despawning batches owned by this!
	std::scoped_lock batchesLock(instance->batchesGuard);
    for(auto&& [params, batch] : instance->batches)
        for(auto&& [guid, section] : batch.sections)
            Editable::aboutToBeDeleted(section);

	instance->batches.clear();
}