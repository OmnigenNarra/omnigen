#pragma once
#include "../DomainDataBase.h"
#include "Data/Assets/AssetBase.h"

enum class ETemperature
{
    Polar,
    Subpolar,
    Boreal,
    Cool,
    Warm,
    Subtropical,
    Tropical
};

extern inline std::map<ETemperature, float> PTemperature = {};

enum class EHumidity
{
    Desert,
    VeryArid,
    Arid,
    Dry,
    Moderate,
    Moist,
    VeryMoist,
    Wet
};

extern inline std::map<EHumidity, float> PHumidity = {};

enum class EBiomeLayer
{
    Floor,      // 0m - 0.5m
    Low,        // 0.5m - 1m
    Middle,     // 1m - 2m
    High        // > 2m
};

using BiomeConfig = std::array<std::vector<QSharedPointer<OmnigenAsset<EAsset::Plant>>>, magic_enum::enum_count<EBiomeLayer>()>;

template<>
struct DomainData<EDomainType::Biome> : DomainDataBase
{
    ETemperature temperature = ETemperature::Cool;
    EHumidity humidity = EHumidity::Dry;
    EBiomeLayer maxLayer = EBiomeLayer::High;
    float foliageDensity = 0.5f;
    BiomeConfig config;

    virtual void fillProps(QSharedPointer<OmnigenPropertyListBase> props) override;
    virtual QString makeName(bool isInitial = false) override;

    FRIEND_OMNIBIN(DomainData<EDomainType::Biome>);
};

inline void omniSave(const DomainData<EDomainType::Biome>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DomainDataBase&>(object);
    omniBin << object.temperature;
    omniBin << object.humidity;
    omniBin << object.foliageDensity;
}

inline void omniLoad(DomainData<EDomainType::Biome>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DomainDataBase&>(object);
    omniBin >> object.temperature;
    omniBin >> object.humidity;
    omniBin >> object.foliageDensity;
}