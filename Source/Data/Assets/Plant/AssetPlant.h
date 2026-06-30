#pragma once
#include "../Common/MeshAtlas.h"
#include "Scene/Generation/Stages/Layout/Data/Biome/DomainData_Biome.h"

enum class EPlantSeeding
{
    Uniform,
    Clustered,
    Mixed
};

struct PlantPlacementData
{
    BoundingBox box;
    std::vector<GVector2D> concaveHull;
};

void omniSave(const PlantPlacementData& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(PlantPlacementData& object, OmniBin<std::ios::out>& omniBin);

template<>
struct OmnigenAsset<EAsset::Plant> : MeshAtlasAssetBase
{
    OmnigenAsset();

    virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    virtual void makeUniqueName() override;

    // For autogen
    void buildPlacementData() const;

    static std::vector<QSharedPointer<OmnigenAssetBase>> newAsset();

    // Plant params
    std::array<ETemperature, 2> temperatureRange = { ETemperature::Boreal, ETemperature::Warm };
    std::array<EHumidity, 2> humidityRange = { EHumidity::Arid, EHumidity::Moderate };
    EBiomeLayer layer = EBiomeLayer::Low;
    EPlantSeeding seedingType = EPlantSeeding::Uniform;
    float abundance = 1.0f;
    std::array<float, 3> slopeDegreesRange = { 0.0f /*min*/, 0.0f /*optimal*/, 60.0f /*max*/ };
    std::array<float, 3> humusFactorRange = { 0.2f /*min*/, 1.0f /*optimal*/, 1.0f /*max*/ };
    std::array<float, 3> lightFactorRange = { 0.3f /*min*/, 1.0f /*optimal*/, 1.0f /*max*/ };

    mutable std::vector<std::map<EBiomeLayer, PlantPlacementData>> placementData;
};

using OmnigenAsset_Plant = OmnigenAsset<EAsset::Plant>;

void omniSave(const OmnigenAsset_Plant& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenAsset_Plant& object, OmniBin<std::ios::in>& omniBin);