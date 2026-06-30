#pragma once
#include "../Common/Texture.h"
#include "Scene/Generation/Stages/Layout/Data/Biome/DomainData_Biome.h"

// Intended to stack over Rock Material
// Newest planned use was to handle Sand, Ice and other textures to simply stack on top of Rocks.
// In many places called Cover Material / Cover Texture
// "Soil" is deprecated
 
template<>
struct OmnigenAsset<EAsset::SoilMaterial> : OmnigenAssetBase
{
    OmnigenAsset();

    virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    virtual void makeUniqueName() override;

    static std::vector<QSharedPointer<OmnigenAssetBase>> newAsset();
    const auto& getMaterials() const { return materials; };
    const auto& getAllowedRockMaterials() const { return rockMaterials; };
    const auto& getTemperatureRange() const { return temperatureRange; };
    const auto& getHumidityRange() const { return humidityRange; };

    // In current impl, Soils can only be spawned on valid Rocks
    void setAllowedRockMaterial(qint64 id, bool allowed);

private:
    std::array<ETemperature, 2> temperatureRange = { ETemperature::Boreal, ETemperature::Warm };
    std::array<EHumidity, 2> humidityRange = { EHumidity::Arid, EHumidity::Moderate };
    std::unordered_set<qint64> rockMaterials;
    std::array<Material, 1> materials;

    FRIEND_OMNIBIN(OmnigenAsset);
    friend class QMultiAssetCompilerMainWindow;
};

using OmnigenAsset_SoilMaterial = OmnigenAsset<EAsset::SoilMaterial>;

void omniSave(const OmnigenAsset_SoilMaterial& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenAsset_SoilMaterial& object, OmniBin<std::ios::in>& omniBin);
