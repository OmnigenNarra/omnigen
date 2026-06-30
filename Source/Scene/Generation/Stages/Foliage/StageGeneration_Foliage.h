#pragma once
#include "../StageGenerationBase.h"
#include "Utils/QuadTreeLite.h"
#include "Data/Assets/AssetBase.h"
#include "Scene/Generation/Stages/Layout/Data/Biome/DomainData_Biome.h"
#include <noise/noise.h>

namespace Design
{
    template<EGenerationStage>
    class StageTools;
}

namespace Generation
{
    class DPlant;
    class Heightfield;

    template<>
    class StageGen<EGenerationStage::Foliage>
    {
        using BiomeLayerHeatmap = std::map<QSharedPointer<OmnigenAsset<EAsset::Plant>>, std::array<std::unique_ptr<Heightfield>, 2>>;
        struct SpeciesCachedData
        {
            struct SeederFunctor
            {
                SeederFunctor(float inAbundance) : abundance(inAbundance) {}

                float abundance;
                virtual float seedValue(const QVector3D&) = 0;
            };

            QSharedPointer<SeederFunctor> seederFunctor;
            std::mutex plantGuard;
            std::vector<std::mutex> modelGuards;
            std::uniform_int_distribution<int> modelDist;
            std::vector<std::uniform_real_distribution<float>> scaleDists;
        };

        static std::array<BiomeLayerHeatmap, magic_enum::enum_count<EBiomeLayer>()> biomeLayerHeatmaps;
        static inline std::unordered_map<qint64, SpeciesCachedData> speciesGlobalData;
        static inline std::vector<QSharedPointer<DPlant>> createdPlants;

    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize();

    private:
        static void createSpeciesHeatmaps();
        static void createSpeciesGlobalData();
        static void spawnAll();
        static void spawnUsedModels(const std::unordered_map<qint64, std::set<int>>& usedModels);

        struct PlantHasher
        {
            size_t operator()(const QSharedPointer<OmnigenAssetBase>& plant) const
            {
                return std::hash<qint64>()(plant->id);
            }
        };

        using SpeciesFactorMap = std::unordered_map<QSharedPointer<OmnigenAssetBase>, float, PlantHasher>;

        static QSharedPointer<OmnigenAsset<EAsset::Plant>> choosePlant(int layer, 
            const QVector3D& location, float temperature, float humidity,
            const SpeciesFactorMap& precomputedFactors);

        using SpeciesDataPerTriangle =
            std::unordered_map<qint64, /*cluster id*/
            std::vector< /*triangle idx*/
            std::array< /*layer*/
            SpeciesFactorMap, // how each plant scores here
            magic_enum::enum_count<EBiomeLayer>()>>>;
        static SpeciesDataPerTriangle precomputeSpeciesDataPerTriangle();

        static float getSpeciesSeedValue(qint64 speciesId, const QVector3D& p);
        static void debugSpeciesNoise(qint64 speciesId);

        friend class Design::StageTools<EGenerationStage::Foliage>;
    };
}