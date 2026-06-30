#include "stdafx.h"
#include "OmnigenGenerationData.h"
#include "Omnigen.h"
#include "OmnigenGeneration.h"

#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainSquareDrawable.h"

#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"
#include "Scene/Generation/Common/Markers/NurbsMarker.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Scene/Generation/Common/Markers/MultiLineColoredMarker.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverMarker.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/TerrainMods/TerrainModBase.h"
#include "Stages/Lithomap/StageGeneration_Lithomap.h"

#include "Editor/Dialogs/StageProgressConfirmation.h"
#include "Editor/StageTools/Layout/LayoutSelection.h"
#include "Editor/StageTools/StageTools.h"
#include "Utils/ITree.h"
#include "Editable.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/OmniStageBar/OmniStageBar.h"
#include "Scene/Generation/Stages/StageGeneration.h"

#include <QFileDialog>
#include <execution>

#include "Stages/UrbanSites/UrbanGen/UrbanSite.h"
#include "Stages/UrbanLayout/UrbanSuggestion.h"

#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"
#include "Data/Assets/SoilMaterial/AssetSoilMaterial.h"

#include <noise/noise.h>

namespace Generation
{
    Data::Data()
    {
    }

    Data* Data::get()
    {
        static Data* sInstance = nullptr;

        if (!sInstance)
            sInstance = new Data();

        return sInstance;
    }

    void exportTerrainVertex(const TerrainMeshVertex& v, OmniBin<std::ios::out>& writer)
    {
        writer << v.position;
        writer << v.normal;

        writer << v.terrainTexWeights;
        writer << v.biomeTexWeights;
        writer << v.packParams;

        writer << v.displacementFactor;
    }

    const Data::BlockQuadTree& Data::getBlockQuadTree()
    {
        static std::mutex treeGuard;
        std::scoped_lock lock(treeGuard);

        if (!blockQuadTree)
            calculateBlockQuadTree();

        return blockQuadTree;
    }

    QSharedPointer<UrbanSite> Data::findUrbanSiteByGuid(const qint64 id) const
    {
        if (urbanSites.empty())
            return nullptr;

        for (auto&& site : urbanSites)
        {
            if (site->getGuid() == id)
                return site;
        }

        return nullptr;
    }

    QSharedPointer<UrbanSuggestion> Data::findUrbanSuggestionByGuid(const qint64 id) const
    {
        if (urbanSuggestions.empty())
            return nullptr;

        for (auto&& s : urbanSuggestions)
        {
            if (s->getGuid() == id)
                return s;
        }

        return nullptr;
    }

    GVector2D Data::getWindVector(const GVector2D& p)
    {
        static noise::module::Perlin noiseSource;
        static noise::model::Plane noiseModel;
        static bool isInited = false;

        static std::mutex guard;
        if (std::scoped_lock lock(guard); !isInited)
        {
            noiseSource.SetSeed(gRandomEngine());
            noiseSource.SetFrequency(3e-5f);
            noiseSource.SetOctaveCount(2);
            noiseModel.SetModule(noiseSource);
            isInited = true;
        }

        float x = noiseModel.GetValue(p.x, p.z);
        float z = noiseModel.GetValue(-p.x, -p.z);
        return { x, z };
    }

    std::vector<std::vector<QSharedPointer<Isohypse>>> Data::getIsohypseMarkersByLevel() const
    {
        std::vector<std::vector<QSharedPointer<BatchedSection<IsohypseBatchParams>>>> result;

        auto&& instance = gBatchingMarkerInstance<IsohypseBatchParams>;
        auto&& [batches, batchesGuard] = instance->getBatches();
        for (auto&& [offset, section] : batches.begin()->second.sections)
        {
            int level = section->getLevel();
            if (level >= result.size())
                result.resize(level + 1);

            result[level] << section;
        }

        return result;
    }

    void Data::setGenerationStage(EGenerationStage newStage, bool generate, bool validate, bool justOpened /*= false*/)
    {
        if (validate)
        {
            // Always leave in valid state
            if (!EGenerationStageConstexpr::UseIn<EAC::ValidateStage>(generationStage))
            {
                if (!Omnigen::get()->isSectionVisible(EOmnigenSection::Log))
                    Omnigen::get()->toggleSectionVisibility(EOmnigenSection::Log);

                return;
            }

            if (newStage > generationStage && EGenerationStageConstexpr::UseIn<EAC::HasDataToSave>(newStage) && !EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(generationStage)->validatePipeline())
            {
                QStageProgressConfirmation progressStagePopup;
                int result = progressStagePopup.exec();

                if (result == 0)
                    return;
                else if (result == 1)
                {
                    EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(generationStage)->updatePipeline();
                    EGenerationStageConstexpr::UseIn<EAC::InvalidateStage>(generationStage, false);
                    for (int stage = ((int)generationStage) + 1; stage < (int)EGenerationStage::Last; stage++)
                        if (auto&& nextStage = EGenerationStage(stage); EGenerationStageConstexpr::UseIn<EAC::HasDataToSave>(nextStage))
                            EGenerationStageConstexpr::UseIn<EAC::InvalidateStage>(nextStage, true);
                }
                else if (result == 2)
                {
                    EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(generationStage)->loadSnapshotData();
                    return;
                }
            }
        }

        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(generationStage)->unbind();

        if (generate)
            newStage = Omnigen::get()->action_generate(newStage);
        else
        {
            int dir = !justOpened ? int(newStage) - int(generationStage) : 0;
            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(generationStage)->aboutToExitStage(dir);
            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(newStage)->aboutToEnterStage(dir);
        }

        generationStage = newStage;
        Omnigen::get()->getStageBar()->updateStageBar();

        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(generationStage)->bind();
    }

    void Data::createDomainSquares(const QSet<GPoint>& squares)
    {
        for (auto&& [x, z] : squares)
        {
            if (domainSquares[x][z])
                continue;

            domainSquares[x][z] = QSharedPointer<DDomainSquare>::create(x, z);
            domainSquares[x][z]->initialize();
            emit Editable::created(domainSquares[x][z]);
        }
    }

    void Data::clearDomainSquares(const QSet<GPoint>& squares, bool force)
    {
        for (auto&& gp : squares)
            if (force || getDomainsAtSquare(gp).empty())
            {
                emit Editable::aboutToBeDeleted(domainSquares[gp.x][gp.z]);
                domainSquares[gp.x][gp.z].reset();
            }
    }

    void Data::addDomain(const QSharedPointer<DDomainHandle>& inHandle, const QSharedPointer<DDomain>& inDomain)
    {
        domains.push_back({ inHandle, inDomain });
    }

    void Data::restoreDomain(const std::tuple<DDomainHandle, DDomain, int>& data)
    {
        *domains.back().second = std::get<1>(data);
        *domains.back().first = std::get<0>(data);
        domains.back().second->bindHandle(domains.back().first);
        domains.back().second->setData(*EDomainTypeConstexpr::UseIn<EAC::LoadDomainDataFromHistory>(std::get<1>(data).getType(), std::get<2>(data)));

        emit Editable::created(domains.back().first);
        emit Editable::created(domains.back().second);
    }

    void Data::removeDomain(int idx)
    {
        auto [handle, domain] = domains[idx];

        emit Editable::aboutToBeDeleted(handle);
        emit Editable::aboutToBeDeleted(domain);

        domain->setSquares({});
        domains.erase(domains.begin() + idx);
        handle->prePositionChange();
    }

    void Data::removeDomain(const QSharedPointer<DDomainHandle>& inHandle)
    {
        for(int i=0; i<domains.size(); ++i)
            if (domains[i].first == inHandle)
            {
                removeDomain(i);
                break;
            }
    }

    void Data::clearBlockQuadTree()
    {
        blockQuadTree.reset();
    }

    void Data::setDEM(const QSharedPointer<Generation::DEM>& inDEM)
    {
        dem = inDEM;

        if (dem)
        {
#if !DEBUG_HEIGHTFIELD_TEXCOORD
            dem->heightData.makePreview<DDemMarker>(dem->color);
#else
            dem->verticalDisplacementXCoords.makePreview<DDemMarker>(dem->color);
#endif
        }
    }

    void Data::setLargestVoronoiCellRadius(const float radius)
    {
        largestCellRadius = radius;
    }

    void Data::setTerrainCells(const QSharedPointer<Voronoi::BoxDiagram>& diagram)
    {
        terrainCells = diagram;

        //if (terrainCells)
        //    terrainCells->drawCells(QVector4D(0.5, 0.5, 0.5, 1), -50);
    }

    void Data::setLithomap(const std::vector<int> inLithomap, const std::vector<QSharedPointer<LithoCluster>>& inClusters)
    {
        lithoMap = inLithomap;
        lithoClusters = inClusters;
    }

    void Data::setBlockTypeMap(const std::vector<ETerrainBlock>& inTypeMap)
    {
        blockTypeMap = inTypeMap;
    }

    void Data::setTerrainClusters(const QMap<ETerrainBlock, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>>& metaClusters, const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& clustersMap)
    {
        terrainMetaClusters = metaClusters;
        terrainClustersMap = clustersMap;
    }

    void Data::setClusterForCell(int idx, const QSharedPointer<Generation::TerrainBlockClusterBase>& cluster)
    {
        if (auto&& currentCluster = terrainClustersMap[idx]; currentCluster != nullptr && cluster == nullptr)
            if (std::count(terrainClustersMap.begin(), terrainClustersMap.end(), currentCluster) == 1)
                emit Editable::aboutToBeDeleted(currentCluster);

        terrainClustersMap[idx] = cluster; 
    }

    void Data::removeMetaCluster(ETerrainBlock bt, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster) 
    { 
        emit Editable::aboutToBeDeleted(metaCluster);
        terrainMetaClusters[bt].erase(std::find(terrainMetaClusters[bt].begin(), terrainMetaClusters[bt].end(), metaCluster)); 
    };

    void Data::registerBorderFacePoint(int clusterKey, int cellIdx, const TerrainMeshVertex& p, quint32 fi)
    {
        Generation::BorderPoint::PerClusterData* pcd;

        // Critical section
        {
            std::scoped_lock lock(bpGuard);
            auto&& bp = gBorderPoints[p.position];
            pcd = &bp.intermediateData[clusterKey];
        }

        pcd->v = p;
        pcd->facesPerBlock[cellIdx] << fi;
    }

    void Data::setDomainHeightBounds(const std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>& inHeightBounds)
    {
        domainHeightBounds = inHeightBounds;
    }

    void Data::addEnviroBound(const QSharedPointer<EnvBound>& bound)
    {
        static std::mutex guard;
        std::scoped_lock lock(guard);
        enviroBounds[bound->cellIdx] << bound;
    }

    void Data::setTerrainMods(const QMap<ETerrainMod, std::vector<QSharedPointer<Generation::TerrainModBase>>>& mods)
    {
        terrainMods = mods;
    }

    void Data::setTriangleAdjList(
        const std::unordered_map<std::pair<int, int>, std::shared_ptr<TrianglesGraph::TriangleAdjacencyGraph::Node>>& inList)
    {
        triangleAdJList = inList;
    }

    void Data::addTerrainMod(ETerrainMod type, const QSharedPointer<Generation::TerrainModBase>& mod)
    {
        terrainMods[type] << mod;
    }

    void Data::removeTerrainMod(ETerrainMod type, qint64 guid)
    {
        auto&& typedMods = terrainMods[type];
        auto it = std::ranges::find_if(typedMods, [&](auto&& mod) {return mod->getGuid() == guid; });
        Q_ASSERT(it != typedMods.end());
        typedMods.erase(it);
    }

    void Data::setUrbanSuggestions(const std::vector<QSharedPointer<Generation::UrbanSuggestion>>& suggestions)
    {
        urbanSuggestions = suggestions;
    }

    void Data::setUrbanSites(const std::vector<QSharedPointer<Generation::UrbanSite>>& sites)
    {
        urbanSites = sites;
    }

    void Data::setUrbanMeshes(const std::vector<QSharedPointer<DUrbanMesh>>& inUrbanMeshes)
    {
        urbanMeshes = inUrbanMeshes;
    }

    void Data::initTerrainChunks(const std::vector<QSharedPointer<DTerrainChunk>>& chunks, const QHash<int, QSet<int>>& inChunkBlocksMap, const QHash<int, int>& inBlockChunkMap)
    {
        blockChunkMap = inBlockChunkMap;
        chunkBlocksMap = inChunkBlocksMap;
        terrainChunks = chunks;
    }

    void Data::emptyTerrainChunks()
    {
        for (auto&& chunk : terrainChunks)
        {
            emit Editable::aboutToBeDeleted(chunk);
            chunk->assignLodLevel(ELOD::Far, {});
            chunk->setActiveLOD(ELOD::Far);
        }
    }

    void Data::clearTerrainChunks()
    {
        terrainChunks.clear();
        chunkBlocksMap.clear();
        blockChunkMap.clear();
    }

    void Data::computeTextureArrays()
    {
        OmniProfile("Terrain textures gathering");

        terrainTextures.clear();
        coverTextures.clear();
        terrainTexPacks.clear();
        biomeTexPacks.clear();

        auto&& assetMgr = Omnigen::get()->getAssetsSection();
        auto&& lithoAssetsIds = assetMgr->getAssetsIds<EAsset::RockMaterial>();
        auto&& coverAssetsIds = assetMgr->getAssetsIds<EAsset::SoilMaterial>();

        std::vector<AssetMeta> meta;
        for (qint64 id : lithoAssetsIds)
            meta.push_back({EAsset::RockMaterial, id});
        for (qint64 id : coverAssetsIds)
            meta.push_back({ EAsset::SoilMaterial, id });

        assetMgr->forceLoadAssets(meta);

        auto&& lithoAssets = assetMgr->getAssets<EAsset::RockMaterial>();
        auto&& coverAssets = assetMgr->getAssets<EAsset::SoilMaterial>();

        for (auto&& lithoCluster : lithoClusters)
        {
            auto&& type = lithoCluster->getType();
            if (terrainTexPacks.contains(type))
                continue;

            terrainTexPacks[type] = terrainTextures.size();
            terrainTextures << lithoAssets[lithoCluster->getType()]->id;
        }

        for (auto&& [id, asset] : coverAssets)
        {
            auto&& allowedRocks = asset->getAllowedRockMaterials();
            bool allowedRockExists = false;

            for (auto&& rock : terrainTextures)
                if (allowedRockExists = allowedRocks.contains(rock); allowedRockExists)
                    break;

            if (!allowedRockExists)
                continue;

            biomeTexPacks[id] = coverTextures.size();
            coverTextures << asset->id;
        }
    }

    void Data::setBorderPoints(const QHash<GVector2D, Generation::BorderPoint>& bPts)
    {
        gBorderPoints = bPts;
    }

    void Data::setEnviroBounds(const QHash<int, std::vector<QSharedPointer<EnvBound>>>& inBounds)
    {
        enviroBounds = inBounds;
    }

    void Data::clearBorderPoints()
    {
        gBorderPoints.clear();
    }

    void Data::clearMarkers()
    {
        QOmnigenViewport::clearDrawables(ERenderPriority::Marker);

        markers.clear();
        markersToInitialize.clear();
    }

    void Data::clearDebugMarkers()
    {
        clearExactMarkers<DLineMarker>();
        clearExactMarkers<DMultiLineMarker>();
        clearExactMarkers<DMultiLineColoredMarker>();
        clearExactMarkers<DPolygonMarker>();
        clearExactMarkers<DPointCloudMarker>();
        clearExactMarkers<DSharedMeshMarker<>>();
        clearExactMarkers<DNurbsMarker>();
    }

    void Data::clearDomains()
    {
        // Domains
        for (auto&& [handle, domain] : domains)
        {
            emit Editable::aboutToBeDeleted(handle);
            emit Editable::aboutToBeDeleted(domain);
        }

        domains.clear();

        // Domain squares
        for (int x = 0; x < GRID_SEGMENT_COUNT; ++x)
            for (int z = 0; z < GRID_SEGMENT_COUNT; ++z)
            {
                if (domainSquares[x][z])
                    emit Editable::aboutToBeDeleted(domainSquares[x][z]);

                domainSquares[x][z].reset();
            }

        domainSquareMap.clear();
    }

    void Data::updateDomainSquaresMap(const QSharedPointer<DDomain>& domain, const QSet<GPoint>& oldSquares, const QSet<GPoint>& newSquares)
    {
        for (auto&& sq : oldSquares)
            domainSquareMap[sq].erase(domain->getType());

        for (auto&& sq : newSquares)
            domainSquareMap[sq][domain->getType()] = domain;
    }

    void Data::initializeQueuedMarkers()
    {
        while (!markersToInitialize.empty())
        {
            auto&& [marker, code, push] = markersToInitialize.back();
            marker->initialize();
            addMarkerDynamic(marker, code, push);
            markersToInitialize.pop_back();
        }
    }

    QSharedPointer<DDomain> Data::getDomainAtSquare(const GPoint& sq, EDomainType dt) const
    {
        auto squareIt = domainSquareMap.find(sq);
        if (squareIt == domainSquareMap.end())
            return {};

        auto domainIt = squareIt->second.find(dt);
        if (domainIt == squareIt->second.end())
            return {};

        return domainIt->second;
    }

    const std::map<EDomainType, QSharedPointer<DDomain>>& Data::getDomainsAtSquare(const GPoint& sq) const
    {
        auto squareIt = domainSquareMap.find(sq);
        if (squareIt == domainSquareMap.end())
        {
            static const std::map<EDomainType, QSharedPointer<DDomain>> dummy;
            return dummy;
        }

        return squareIt->second;
    }

    std::vector<QSharedPointer<DDomainHandle>> Data::getDomainHandlesAt(const QVector3D& pos, EDomainType dt /*=last*/)
    {
        std::vector<QSharedPointer<DDomainHandle>> result;

        for (auto&& [handle, domain] : domains)
            if ((dt == EDomainType::Last) || domain->getType() == dt)
                if (handle && handle->getPosition() == pos)
                    result << handle;

        return result;
    }

    std::optional<QSharedPointer<DDomain>> Data::findDomainByGuid(qint64 id) const
    {
        auto it = std::find_if(domains.begin(), domains.end(), [&id](auto&& kv) { return kv.second->getGuid() == id; });
        if (it != domains.end())
            return it->second;
        else
            return {};
    }

    std::optional<QSharedPointer<DDomainHandle>> Data::findDomainHandleByGuid(qint64 id) const
    {
        auto it = std::find_if(domains.begin(), domains.end(), [&id](auto&& kv) { return kv.first->getGuid() == id; });
        if (it != domains.end())
            return it->first;
        else
            return {};
    }

    float getTexWeight(quint32 weights, quint32 idx)
    {
        quint32 off = 8 * idx;
        return float((weights & (0xFF << off)) >> off) / 255.0f;
    }

    float getPackParam(quint32 params, quint32 idx)
    {
        quint32 off = 8 * idx;
        return float((params & (0xFF << off)) >> off) / 255.0f;
    }

    quint32 compileTexWeights(const std::vector<float>& weights)
    {
        quint32 result = 0;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            size_t off = 8 * i;
            result += (quint32(std::round(weights[i] * 255.0f)) << off);
        }

        return result;
    }

    quint32 compilePackParams(const std::vector<float>& params)
    {
        quint32 result = 0;
        for (size_t i = 0; i < params.size(); ++i)
        {
            size_t off = 8 * i;
            result += (quint32(std::round(params[i] * 255.0f)) << off);
        }

        return result;
    }

    void setPackParam(quint32* params, int slot, float newValue)
    {
        std::vector<float> extractedParams
        {
            getPackParam(*params, 0),
            getPackParam(*params, 1),
            getPackParam(*params, 2),
            getPackParam(*params, 3)
        };

        extractedParams[slot] = newValue;
        *params = compilePackParams(extractedParams);
    }

    EnvBound::EnvBound()
        : guid(makeGuid())
    {
    }

    EnvBound::EnvBound(int inIdx)
        : guid(makeGuid())
        , cellIdx(inIdx)
    {
    }

    GVector2D EnvBound::getNorm(int lineIndex) const
    {
        Q_ASSERT(lineIndex >= 0 && lineIndex < line.size());

        int firstIndex = lineIndex != (line.size() - 1) ? lineIndex : lineIndex - 1;
        int secondIndex = lineIndex != (line.size() - 1) ? lineIndex + 1 : lineIndex;
        if (flipDir)
            return ((GVector2D)(line[firstIndex] - line[secondIndex])).normalized().rotatedLeft90();
        else
            return ((GVector2D)(line[firstIndex] - line[secondIndex])).normalized().rotatedRight90();
    }
}

void Generation::Data::saveEnvBounds(OmniBin<std::ios::out>& writer)
{
    QHash<int, std::map<int, std::vector<qint64>>> guids;
    for (auto it = enviroBounds.keyValueBegin(); it != enviroBounds.keyValueEnd(); ++it)
    {
        auto&& [id, bounds] = *it;
        for (auto&& b : bounds)
            for (auto&& pb : b->pairedBounds)
            {
                auto pbl = pb.lock();
                guids[id][pbl->cellIdx] << pb.lock()->guid;
            }
    }

    writer << enviroBounds;
    writer << guids;
}

void Generation::Data::loadEnvBounds(OmniBin<std::ios::in>& reader)
{
    QHash<int, std::map<int, std::vector<qint64>>> guids;
    reader >> enviroBounds;
    reader >> guids;

    auto findBoundByGuid = [&](int blockId, qint64 guid)
    {
        Q_ASSERT_X(enviroBounds.contains(blockId), "env bound", "wtf");
        auto res = std::find_if(std::execution::par_unseq, enviroBounds[blockId].begin(), enviroBounds[blockId].end(), [&](auto&& bound) { return bound->guid == guid; });
        Q_ASSERT_X(res != enviroBounds[blockId].end(), "env bounds", "lookup fail");
        return *res;
    };

    for (auto it = enviroBounds.keyValueBegin(); it != enviroBounds.keyValueEnd(); ++it)
    {
        auto&& [id, bounds] = *it;
        for (auto&& b : bounds)
        {
            for (auto [cId, pairedGuids] : guids[id])
                for(qint64 g : pairedGuids)
                    b->pairedBounds << findBoundByGuid(cId, g);
        }
    }
}

void Generation::Data::calculateBlockQuadTree()
{
    const auto& demBounds = getDEM()->heightData.getBoundingBox();
    blockQuadTree = std::make_shared<tml::qtree<float, IndexType>>(demBounds.nbl.x(), demBounds.nbl.z() + demBounds.sizes.z(), demBounds.nbl.x() + demBounds.sizes.x(), demBounds.nbl.z());

    auto&& cells = getTerrainCells()->getCells();
    for (int i = 0; i < cells.size(); i++)
    {
        auto&& center = cells[i].getPolygon().getCenter();
        blockQuadTree->add_node(center.x, center.z, i);
    }
}

void omniSave(const Generation::EnvBound& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.line;
    omniBin << object.guid;
    omniBin << object.cellIdx;
    omniBin << object.flipDir;
}

void omniLoad(Generation::EnvBound& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.line;
    omniBin >> object.guid;
    omniBin >> object.cellIdx;
    omniBin >> object.flipDir;
}

void omniSave(const Generation::BorderPoint& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.intermediateData;
    omniBin << object.normal;
}

void omniLoad(Generation::BorderPoint& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.intermediateData;
    omniBin >> object.normal;
}

void omniSave(const Generation::BorderPoint::PerClusterData& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.v;
    omniBin << object.facesPerBlock;
}

void omniLoad(Generation::BorderPoint::PerClusterData& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.v;
    omniBin >> object.facesPerBlock;
}