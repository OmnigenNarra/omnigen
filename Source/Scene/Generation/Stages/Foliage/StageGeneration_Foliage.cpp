#include "stdafx.h"
#include "StageGeneration_Foliage.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/Plant/AssetPlant.h"
#include "Scene/Generation/Stages/Foliage/PlantDrawable.h"
#include "Omnigen.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "../TerrainFinalization/TerrainChunkDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Utils/Interpolation.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Common/Objects/Heightfield.h"
#include "Utils/AdjacencyGraph.h"

#include <tbb/blocked_range2d.h>
#include <tbb/task_group.h>

#define DEBUG_PLANT_HULLS 0

namespace Generation
{
    struct NoiseMapVertex
    {
        QVector3D position;
        QVector3D normal;
    };

    std::array<StageGen<EGenerationStage::Foliage>::BiomeLayerHeatmap, magic_enum::enum_count<EBiomeLayer>()> StageGen<EGenerationStage::Foliage>::biomeLayerHeatmaps;

    void StageGen<EGenerationStage::Foliage>::debugSpeciesNoise(qint64 speciesId)
    {
        auto&& dem = Data::get()->getDEM();

        auto&& demMarkers = Data::get()->getMarkers<DDemMarker>();
        auto&& bbox = demMarkers[0]->getBoundingBox();

        std::vector<GVector2D> debugAreaPts(4, bbox.nbl);
        debugAreaPts[1].x += bbox.sizes.x();
        debugAreaPts[2].x += bbox.sizes.x();
        debugAreaPts[2].z += bbox.sizes.z();
        debugAreaPts[3].z += bbox.sizes.z();

        auto geom = QSharedPointer<RenderGeometryData<NoiseMapVertex>>::create();
        auto [geom2D, unusedOuter] = meshPolygon2(debugAreaPts, getDefaultMeshingParams());
        geom->indices = std::move(geom2D.indices);

        static const QVector3D lightDir = { 0.57735026919f, -0.57735026919f, 0.57735026919f };
        static const QVector3D rotationAxis = QVector3D::crossProduct(lightDir, { 0, 1, 0 }).normalized();

        geom->vertices.reserve(geom2D.vertices.size());
        for (auto&& pv : geom2D.vertices)
        {
            float noiseSample = getSpeciesSeedValue(speciesId, pv);
            float angle = std::acos(noiseSample) * 180.0f / std::numbers::pi;

            auto quat = QQuaternion::fromAxisAndAngle(rotationAxis, angle);
            QVector3D normal = quat.rotatedVector(-lightDir);
            geom->vertices.push_back({ pv, normal });
        }

        spawn<DSharedMeshMarker<NoiseMapVertex>>(geom, GL_TRIANGLES, Colors::white, ERenderType::Filled, QVector3D(0, 1.0f, 0));
    }

    bool StageGen<EGenerationStage::Foliage>::autoGen()
    {
        auto plantIds = QOmnigenAssetMgrSection::getAssetsIds<EAsset::Plant>();
        Omnigen::get()->getAssetsSection()->forceLoadAssets(EAsset::Plant, plantIds);

        createSpeciesHeatmaps();
        createSpeciesGlobalData();
        spawnAll();
        DPlant::createResources(createdPlants);

        return true;
    }

    void StageGen<EGenerationStage::Foliage>::clear()
    {
        for (auto&& plant : createdPlants)
        {
            for (auto&& geometry : plant->getAllGeometries())
            {
                auto&& plantGeometry = geometry.staticCast<InstancedRenderGeometryData<MeshAssetVertex, MeshAssetInstanceData>>();
                plantGeometry->setVisibleInstances({});
                plantGeometry->instanceData.clear();
            }
            emit Editable::aboutToBeDeleted(plant);
        }

        createdPlants.clear();

        for (auto&& heatmap : biomeLayerHeatmaps)
            heatmap.clear();

        speciesGlobalData.clear();

        Data::get()->clearExactMarkers<DSharedMeshMarker<NoiseMapVertex>>();
        Data::get()->clearExactMarkers<DHeightfieldMarker>();
    }

    void StageGen<EGenerationStage::Foliage>::finalize()
    {
    }

    void StageGen<EGenerationStage::Foliage>::createSpeciesHeatmaps()
    {
        OmniProfile("Species heatmap");
        const float heatmapDensity = 3.0f;

        auto&& plants = QOmnigenAssetMgrSection::getAssets<EAsset::Plant>();
        std::array<std::vector<qint64>, magic_enum::enum_count<EBiomeLayer>()> plantsPerLayer;
        for (auto&& [id, plant] : plants)
            plantsPerLayer[int(plant->layer)] << id;

        for (EBiomeLayer layer : magic_enum::enum_values<EBiomeLayer>())
        {
            auto&& plantsOfLayer = plantsPerLayer[int(layer)];
            if (plantsOfLayer.empty())
                continue;

            auto&& layerHeatmap = biomeLayerHeatmaps[int(layer)];

            for (qint64 id : plantsOfLayer)
            {
                auto&& plant = plants[id];

                auto [tMin, tMax] = plant->temperatureRange;
                auto prevT = getOffsetedEnum(tMin, -1);
                float lowT = prevT ? PTemperature[*prevT] : 0.0f;
                float highT = PTemperature[tMax];

                auto [hMin, hMax] = plant->humidityRange;
                auto prevH = getOffsetedEnum(hMin, -1);
                float lowH = prevH ? PHumidity[*prevH] : 0.0f;
                float highH = PHumidity[hMax];

                const double debugScale = 1.0;
                auto preferenceMap = std::make_unique<Heightfield>(GVector2D{ 0.0f, 0.0f }, GVector2D{ 1.0f, 1.0f } *debugScale, 0.01f * debugScale);
                auto factorMap = std::make_unique<Heightfield>(*preferenceMap);

                GVector2D focus = { std::midpoint(lowT, highT), std::midpoint(lowH, highH) };
                focus *= debugScale;

                GVector2D radii = { (highT - lowT) * 0.5f, (highH - lowH) * 0.5f };
                radii *= debugScale;

                noise::module::Perlin perlin;
                perlin.SetSeed(gRandomEngine());
                perlin.SetFrequency(1.0 / debugScale);
                static const auto fadingCurve = Interpolation::getInterpolation01(EInterpolation01::InversePower, 2);

                tbb::parallel_for(tbb::blocked_range2d<int, int>(0, preferenceMap->getSize().x, 0, preferenceMap->getSize().z), [&](tbb::blocked_range2d<int>& r)
                    {
                        for (int z = r.cols().begin(); z <= r.cols().end(); ++z)
                            for (int x = r.rows().begin(); x <= r.rows().end(); ++x)
                            {
                                auto&& p = preferenceMap->getPoint(x, z);
                                float xF = std::pow(p.x() - focus.x, 2) / std::pow(radii.x, 2);
                                float zF = std::pow(p.z() - focus.z, 2) / std::pow(radii.z, 2);

                                float distanceFactor = std::max(1.0f - (xF + zF) / std::sqrt(plant->abundance), 0.0f);
                                distanceFactor = fadingCurve->interpolate(distanceFactor);

                                float noiseValue = std::abs(perlin.GetValue(p.x(), 0.0f, p.z()));
                                preferenceMap->setHeight(x, z, noiseValue * distanceFactor * debugScale);
                                factorMap->setHeight(x, z, distanceFactor);
                            }
                    });

                preferenceMap->makePreview(Colors::random());

                layerHeatmap.emplace(plant, std::array{ std::move(preferenceMap), std::move(factorMap) });
            }
        }
    }

    void StageGen<EGenerationStage::Foliage>::createSpeciesGlobalData()
    {
        OmniProfile("Species seeders");
        auto&& plants = QOmnigenAssetMgrSection::getAssets<EAsset::Plant>();

        for (auto&& [id, plant] : plants)
        {
            auto&& data = speciesGlobalData[id];
            auto count = plant->getMeshes().size();
            data.modelDist = std::uniform_int_distribution<int>(0, count - 1);
            data.modelGuards = std::vector<std::mutex>(count);

            for (int modelIdx = 0; modelIdx < plant->getMeshes().size(); ++modelIdx)
            {
                auto&& mesh = plant->getMeshes()[modelIdx];
                data.scaleDists.emplace_back(std::uniform_real_distribution<float>(mesh.getScaleRange()[0], mesh.getScaleRange()[1]));
            }

            if (plant->seedingType == EPlantSeeding::Uniform)
            {
                struct UniformSeeder : SpeciesCachedData::SeederFunctor
                {
                    noise::module::Perlin noise;

                    UniformSeeder(float inAbundance) : SpeciesCachedData::SeederFunctor(inAbundance)
                    {
                        noise.SetSeed(gRandomEngine());
                        noise.SetFrequency(1.0f / (gMinTriangleSideLength * 20.0f));
                        noise.SetLacunarity(3.0f);
                    }

                    virtual float seedValue(const QVector3D& p) override
                    {
                        float baseValue = noise.GetValue(p.x(), p.y(), p.z());
                        return (baseValue + abundance) * 0.5f;
                    }
                };
                data.seederFunctor = QSharedPointer<UniformSeeder>::create(plant->abundance);
            }
            else if (plant->seedingType == EPlantSeeding::Clustered)
            {
                struct ClusteredSeeder : SpeciesCachedData::SeederFunctor
                {
                    noise::module::Billow noise;

                    ClusteredSeeder(float inAbundance) : SpeciesCachedData::SeederFunctor(inAbundance)
                    {
                        noise.SetSeed(gRandomEngine());
                        noise.SetFrequency(1.0f / (gMinTriangleSideLength * 20.0f));
                        noise.SetLacunarity(3.0f);
                    }

                    virtual float seedValue(const QVector3D& p) override
                    {
                        float baseValue = noise.GetValue(p.x(), p.y(), p.z());
                        return (baseValue + abundance) * 0.5f;
                    }
                };

                data.seederFunctor = QSharedPointer<ClusteredSeeder>::create(plant->abundance);
            }
            else if (plant->seedingType == EPlantSeeding::Mixed)
            {
                struct MixedSeeder : SpeciesCachedData::SeederFunctor
                {
                    noise::module::Billow noise;

                    MixedSeeder(float inAbundance) : SpeciesCachedData::SeederFunctor(inAbundance)
                    {
                        noise.SetSeed(gRandomEngine());
                        noise.SetFrequency(1.0f / (gMinTriangleSideLength * 20.0f));
                        noise.SetLacunarity(1.5f);
                    }

                    virtual float seedValue(const QVector3D& p) override
                    {
                        float baseValue = noise.GetValue(p.x(), p.y(), p.z()) * 0.5f + 0.5f;
                        return (baseValue + abundance) * 0.5f;
                    }
                };

                data.seederFunctor = QSharedPointer<MixedSeeder>::create(plant->abundance);
            }
        }
    }

    auto getBiomeLayerColors()
    {
        static std::vector<QVector4D> colors;
        static std::mutex guard;

        std::scoped_lock lock(guard);
        if (colors.empty())
        {
            int count = magic_enum::enum_count<EBiomeLayer>();
            colors.resize(count);
            for (int i = 0; i < count; ++i)
                while (true)
                    if (colors[i] = Colors::random(false); indexOf(colors, colors[i]) == i)
                        break;
        }
            
        return colors;
    }

    enum EPlacementSurfaceMode : int
    {
        NoDebug = 0,
        WithDebug,
        PureDebug
    };
    std::map<qint64, std::set<IndexType>> getPlacementSurface(const GVector2D& p, const PlantPlacementData& placementData, float rotation, float scale, EPlacementSurfaceMode debug)
    {
        OmniProfile("Surface");
        if (!DEBUG_PLANT_HULLS && debug == PureDebug)
            return {};

        auto&& [placementBox, concaveHull] = placementData;
        float radius = std::sqrt(std::pow(placementBox.sizes.x() / 2, 2) + std::pow(placementBox.sizes.z() / 2, 2));

        // Create instance hull
        auto rotator = QQuaternion::fromEulerAngles(0, rotation, 0);
        std::vector<GVector2D> instanceHull(concaveHull.size());
        float r = 0.0f;
        for (int i = 0; i < concaveHull.size(); ++i)
        {
            instanceHull[i] = GVector2D(rotator.rotatedVector(concaveHull[i]) * scale);
            r = std::max(instanceHull[i].length(), r);
        }

        // Expand search limits to ensure enough triangle centers inside
        float expScale = 1.0f;// +(layer != EBiomeLayer::Floor) * gMaxTriangleSideLength / r;

        // Finalize search data
        radius *= expScale;
        for (auto&& ihp : instanceHull)
            ihp = p + ihp * expScale;

        auto&& blockTree = Generation::Data::get()->getBlockQuadTree();
        auto&& clusters = Generation::Data::get()->getTerrainClustersMap();
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();

        std::map<qint64, std::set<IndexType>> results;
        float maxR = Data::get()->getLargestVoronoiCellRadius();
        auto wideSearchNodes = blockTree->find_all_nearest(p.x, p.z, maxR);
        for (auto&& node : wideSearchNodes)
        {
            // Filter out impossible matches
            auto&& cellPolygon = *cells[node->data];
            if (float d = distance(p, cellPolygon.getCenter()); d > cellPolygon.getRadius() + radius)
                continue;

            // Run the in-cluster search
            auto&& cluster = clusters[node->data];
            std::vector<const tml::qtree<float, IndexType>::node_type*> clusterQuery;
            cluster->getFaceQuadTree().search(p.x, p.z, radius, clusterQuery);

            for (auto* node : clusterQuery)
                if (GVector2D triCenter(node->x, node->y); triCenter.isInsidePolygon(instanceHull))
                    results[cluster->getGuid()].insert(node->data);
        }

#if DEBUG_PLANT_HULLS
        if (debug != NoDebug)
        {
            static std::map<qint64, QVector4D> speciesColors;
            static std::mutex colorsGuard;
            if (std::scoped_lock lock(colorsGuard); !speciesColors.contains(plant.getGuid()))
                speciesColors[plant.getGuid()] = Colors::random();

            //speciesColors[plant.getGuid()]
            spawn<DLineMarker>(instanceHull, getBiomeLayerColors()[int(layer)], true, placementBox.nbl.y() + placementBox.sizes.y() * 0.5f);

            if (debug == PureDebug)
                return {};
        }
#endif

        return results;
    }

    float StageGen<EGenerationStage::Foliage>::getSpeciesSeedValue(qint64 speciesId, const QVector3D& p)
    {
        return speciesGlobalData.at(speciesId).seederFunctor->seedValue(p);
    }

    auto StageGen<EGenerationStage::Foliage>::precomputeSpeciesDataPerTriangle() -> SpeciesDataPerTriangle
    {
        OmniProfile("Precompute data");
        auto&& plants = QOmnigenAssetMgrSection::getAssets<EAsset::Plant>();

        SpeciesDataPerTriangle results;

        for (auto&& metaClusterVec : Generation::Data::get()->getTerrainMetaClusters())
            for (auto&& metaCluster : metaClusterVec)
            {
                if (metaCluster->getType() == ETerrainBlock::Seabed)
                    continue;

                for (auto&& cluster : metaCluster->getClusters())
                {
                    auto&& verts = cluster->section->mainBuffer->vertices;
                    auto&& triangles = cluster->section->getIndices();

                    auto&& clusterData = results[cluster->getGuid()];
                    clusterData.resize(triangles.size() / 3);

                    tbb::parallel_for(0, int(triangles.size() / 3), [&](int triangleIdx)
                        {
                            int ti = triangleIdx * 3;

                            auto&& p0 = verts[triangles[ti + 0]];
                            auto&& p1 = verts[triangles[ti + 1]];
                            auto&& p2 = verts[triangles[ti + 2]];

                            // Spawn location
                            QVector3D location = (p0.position + p1.position + p2.position) / 3.0f;

                            // Biome check
                            auto biomeDomain = Data::get()->getDomainAtSquare(GVector2D(location).toGPoint(), EDomainType::Biome);
                            if (!biomeDomain)
                                return;

                            auto biomeData = biomeDomain->getData<EDomainType::Biome>();

                            // Choose plant to place if any
                            QVector3D normal = (p0.normal + p1.normal + p2.normal).normalized();
                            float humusFactor = (getPackParam(p0.packParams, 0) + getPackParam(p1.packParams, 0) + getPackParam(p2.packParams, 0)) / 3.0f;
                            float terrainBlockModifier = (getPackParam(p0.packParams, 1) + getPackParam(p1.packParams, 1) + getPackParam(p2.packParams, 1)) / 3.0f;

                            auto&& triangleData = results[cluster->getGuid()][triangleIdx];

                            for (auto&& [id, plant] : plants)
                            {
                                // Filter by humus factor
                                if (humusFactor < plant->humusFactorRange[0] || plant->humusFactorRange[2] < humusFactor)
                                    continue;

                                // Filter by normal
                                float normalCosA = QVector3D::dotProduct(normal, QVector3D(0, 1, 0));
                                float normalAngle = qRadiansToDegrees(std::acos(normalCosA));
                                if (normalAngle < plant->slopeDegreesRange[0] || plant->slopeDegreesRange[2] < normalAngle)
                                    continue;

                                // Simple factors
                                float baseDensity = std::pow(biomeData->foliageDensity, int(plant->layer));

                                // Slope factor
                                float slopeDMax = -1.0f;
                                float slopeD = -1.0f;
                                if (normalAngle < plant->slopeDegreesRange[1])
                                {
                                    slopeDMax = plant->slopeDegreesRange[1] - plant->slopeDegreesRange[0];
                                    slopeD = plant->slopeDegreesRange[1] - normalAngle;
                                }
                                else
                                {
                                    slopeDMax = plant->slopeDegreesRange[2] - plant->slopeDegreesRange[1];
                                    slopeD = normalAngle - plant->slopeDegreesRange[1];
                                }
                                float slopeFactor = 1.0f - slopeD / slopeDMax;

                                // Humus factor
                                float humusDMax = -1.0f;
                                float humusD = -1.0f;
                                if (humusFactor < plant->humusFactorRange[1])
                                {
                                    humusDMax = plant->humusFactorRange[1] - plant->humusFactorRange[0];
                                    humusD = plant->humusFactorRange[1] - humusFactor;
                                }
                                else
                                {
                                    humusDMax = plant->humusFactorRange[2] - plant->humusFactorRange[1];
                                    humusD = humusFactor - plant->humusFactorRange[1];
                                }
                                static auto humusFactorCurve = Interpolation::getInterpolation01(EInterpolation01::InversePower, 3);
                                float plantHumusFactor = humusFactorCurve->interpolate(1.0f - humusD / humusDMax);

                                float finalFactorBase = baseDensity * slopeFactor * plantHumusFactor * terrainBlockModifier;
                                if (finalFactorBase > 0.0f)
                                    triangleData[int(plant->layer)][plant] = finalFactorBase;
                            }
                        });
                }
            }

        return results;
    }

    void StageGen<EGenerationStage::Foliage>::spawnAll()
    {
        auto&& plants = QOmnigenAssetMgrSection::getAssets<EAsset::Plant>();
        if (plants.empty())
            return;

        OmniProfile("Seeding");

        static std::uniform_int_distribution<int> orientationDist(0, 360);

        std::unordered_map<qint64, std::set<int>> usedModels;
        std::vector<std::vector<QVector3D>> lines;
        std::mutex linesGuard;

        struct TriangleStatus
        {
            bool covered = false;
            std::mutex guard;
        };

        // Init triangle statuses
        std::array<std::unordered_map<qint64, std::vector<TriangleStatus>>, magic_enum::enum_count<EBiomeLayer>()> trianglesCoveredPerLayer;
        for (auto&& metaClusterVec : Generation::Data::get()->getTerrainMetaClusters())
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                    for (EBiomeLayer layer : magic_enum::enum_values<EBiomeLayer>())
                        trianglesCoveredPerLayer[static_cast<int>(layer)][cluster->getGuid()] = std::vector<TriangleStatus>(cluster->section->getIndexBufferSize() / 3);

        // Compute all species occurrence factor on each valid triangle
        SpeciesDataPerTriangle perTriangleData = precomputeSpeciesDataPerTriangle();

        OmnigenProfilerSegment seg2("PostInit");

        for (int layer = int(EBiomeLayer::High); layer >= 0; --layer)
        {
            if (biomeLayerHeatmaps[layer].empty())
                continue;

            for (auto&& metaClusterVec : Generation::Data::get()->getTerrainMetaClusters())
                for (auto&& metaCluster : metaClusterVec)
                {
                    if (metaCluster->getType() == ETerrainBlock::Seabed)
                        continue;

                    for (auto&& cluster : metaCluster->getClusters())
                    {
                        if (cluster->temperatureRange[1] == 0.0f)
                            continue;

                        auto&& verts = cluster->section->mainBuffer->vertices;
                        auto&& triangles = cluster->section->getIndices();
                        auto&& clusterPrecomputedData = perTriangleData[cluster->getGuid()];

                        tbb::parallel_for(0, int(triangles.size() / 3), [&](int triangleIdx)
                            {
                                //OmniProfile("Body", true);

                                // Initial check, maybe nothing to do here
                                auto&& mainTriangleGuard = trianglesCoveredPerLayer[layer][cluster->getGuid()][triangleIdx];
                                if (std::scoped_lock lock(mainTriangleGuard.guard); mainTriangleGuard.covered)
                                    return;

                                int ti = triangleIdx * 3;

                                auto&& p0 = verts[triangles[ti + 0]];
                                auto&& p1 = verts[triangles[ti + 1]];
                                auto&& p2 = verts[triangles[ti + 2]];

                                // Spawn location
                                QVector3D location = (p0.position + p1.position + p2.position) / 3.0f;
                                float temperature = (p0.temperature + p1.temperature + p2.temperature) / 3.0f;
                                float humidity = (p0.humidity + p1.humidity + p2.humidity) / 3.0f;

                                auto plant = choosePlant(layer, location, temperature, humidity, clusterPrecomputedData[triangleIdx][layer]);
                                if (!plant)
                                    return;

                                // Instance params
                                Q_ASSERT(speciesGlobalData.contains(plant->id));
                                auto&& plantData = speciesGlobalData[plant->id];
                                int modelIdx = plantData.modelDist(gRandomEngine);

                                float rotationAngle = float(orientationDist(gRandomEngine));
                                float scale = plantData.scaleDists[modelIdx](gRandomEngine);

                                //////////////////////////////////////////////////////////////////////////
                                // BEGIN PLACEMENT MARKING
                                auto placementSurface = getPlacementSurface(location, plant->placementData[modelIdx][EBiomeLayer(layer)], rotationAngle, scale, NoDebug);
                                auto&& ownLayerTrianglesCovered = trianglesCoveredPerLayer[layer];

                                // Lock all triangles
                                for (auto&& [clusterGuid, surfTriangles] : placementSurface)
                                    for (auto&& guards = ownLayerTrianglesCovered[clusterGuid]; IndexType surfTi : surfTriangles)
                                        guards[surfTi / 3].guard.lock();

                                // Check if all can be used
                                bool canPlace = true;
                                for (auto&& [clusterGuid, surfTriangles] : placementSurface)
                                    for (auto&& guards = ownLayerTrianglesCovered[clusterGuid]; IndexType surfTi : surfTriangles)
                                        if (guards[surfTi / 3].covered)
                                        {
                                            canPlace = false;
                                            goto ABORT_PLACEMENT;
                                        }

                                // DEBUG HULLS
                                getPlacementSurface(location, plant->placementData[modelIdx][EBiomeLayer(layer)], rotationAngle, scale, PureDebug);

                                // Take space for placement in own layer
                                for (auto&& [clusterGuid, surfTriangles] : placementSurface)
                                    for (auto&& guards = ownLayerTrianglesCovered[clusterGuid]; IndexType surfTi : surfTriangles)
                                        guards[surfTi / 3].covered = true;

                                // Take space for placement in layers below
                                // Don't take space in the Floor layer
                                for (int markLayer = layer - 1; markLayer > 0; --markLayer)
                                {
                                    auto lowerPlacementSurface = getPlacementSurface(location, plant->placementData[modelIdx][EBiomeLayer(markLayer)], rotationAngle, scale, WithDebug);
                                    auto&& lowerTrianglesCovered = trianglesCoveredPerLayer[markLayer];

                                    for (auto&& [clusterGuid, surfTriangles] : lowerPlacementSurface)
                                        for (auto&& guards = lowerTrianglesCovered[clusterGuid]; IndexType surfTi : surfTriangles)
                                            guards[surfTi / 3].covered = true;
                                }

                                // Unlock triangles
                                // ******************************************************************************************
                            ABORT_PLACEMENT:
                                // ******************************************************************************************
                                for (auto&& [clusterGuid, surfTriangles] : placementSurface)
                                    for (auto&& guards = ownLayerTrianglesCovered[clusterGuid]; IndexType surfTi : surfTriangles)
                                        guards[surfTi / 3].guard.unlock();

                                if (!canPlace)
                                    return;
                                //
                                // END PLACEMENT MARKING
                                //////////////////////////////////////////////////////////////////////////
                                //OmnigenProfilerSegment seg("Placement");

                                // All good: add instance
                                if (std::scoped_lock l(plantData.plantGuard); true)
                                    usedModels[plant->id].insert(modelIdx);

                                QMatrix4x4 instanceTransform;
                                instanceTransform.translate(location);
                                instanceTransform.rotate(rotationAngle, QVector3D(0, 1, 0));
                                instanceTransform.scale(scale);

                                QVector3D normal = (p0.normal + p1.normal + p2.normal).normalized();

                                auto&& model = plant->getMeshes()[modelIdx];
                                auto&& geometry = model.getGeometry().at(ELOD::Zero);
                                if (std::scoped_lock lock(plantData.modelGuards[modelIdx]); true)
                                    geometry->instanceData <<= MeshAssetInstanceData{ instanceTransform, normal };
                            });
                    }
                }
        }

        spawnUsedModels(usedModels);

        // This overlaps, debugging with more than 1 species enabled is pointless.
        //for (auto&& [id, plant] : plants)
        //    debugSpeciesNoise(id);

        //spawn<DMultiLineMarker>(lines);
    }

    void StageGen<EGenerationStage::Foliage>::spawnUsedModels(const std::unordered_map<qint64, std::set<int>>& usedModels)
    {
        auto&& plants = QOmnigenAssetMgrSection::getAssets<EAsset::Plant>();

        for (auto&& [id, models] : usedModels)
            for (int modelId : models)
            {
                auto&& plantDrawable = QSharedPointer<DPlant>::create(plants[id], modelId);
                plantDrawable->initialize();
                emit Editable::created(plantDrawable);
                createdPlants << plantDrawable;
            }
    }

    QSharedPointer<OmnigenAsset<EAsset::Plant>> StageGen<EGenerationStage::Foliage>::choosePlant(
        int layer, const QVector3D& location, float temperature, float humidity,
        const SpeciesFactorMap& precomputedFactors)
    {
        //OmniProfile("choosePlant");

        struct PlantWithClimateFactor
        {
            QSharedPointer<OmnigenAsset_Plant> plant;
            float climateFactor;
        };

        std::multimap<float, PlantWithClimateFactor> plantFactors;
        for (auto&& [plant, heatmaps] : biomeLayerHeatmaps[layer])
            if (auto precomputedFactorIt = precomputedFactors.find(plant); precomputedFactorIt != precomputedFactors.end())
                if (float climateFactor = heatmaps[1]->sample({ temperature, humidity }); climateFactor > 0.0f)
                    plantFactors.insert({ heatmaps[0]->sample({ temperature, humidity }), PlantWithClimateFactor{plant, climateFactor * precomputedFactorIt->second } });

        for (auto&& [preferenceFactor, plantWithFactor] : plantFactors)
        {
            auto&& [plant, factor] = plantWithFactor;

            float densityThreshold = 1.0f - factor;
            if (densityThreshold < 0.001f)
                densityThreshold = 0.001f;

            float seedValue = getSpeciesSeedValue(plant->id, location);
            if (seedValue >= densityThreshold)
                return plant;
        }

        return {};
    }
}

#undef DEBUG_PLANT_HULLS