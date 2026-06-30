#pragma once
#include "../DomainDataBase.h"

enum class ELandmassSize
{
    Small,
    Medium,
    Large
};

static const QMap<ELandmassSize, std::pair<int, int>> PShorelineSpawnSizeRange =
{
    {ELandmassSize::Small, {1, 3}},
    {ELandmassSize::Medium, {4, 8}},
    {ELandmassSize::Large, {9, 25}}
};

enum class EAmountOfIslands
{
    None,
    Small,
    Medium,
    Large
};

static const QMap<EAmountOfIslands, float> PIslandsWeightQuanity =
{
    {EAmountOfIslands::None, 0.0f},
    {EAmountOfIslands::Small, 0.35f},
    {EAmountOfIslands::Medium, 0.65f},
    {EAmountOfIslands::Large, 1.0f}
};

enum class EIslandsCoverage
{
    None,
    Small,
    Medium,
    Large
};

static const QMap<EIslandsCoverage, float> PIslandsRatioCoverage =
{
    {EIslandsCoverage::None, 0.0f},
    {EIslandsCoverage::Small, 0.35f},
    {EIslandsCoverage::Medium, 0.65f},
    {EIslandsCoverage::Large, 1.0f}
};

enum class EShorelineComplexity
{
    VeryLow,
    Low,
    Medium,
    High,
    VeryHigh
};

static const QMap<EShorelineComplexity, float> PShorelineComplexity =
{
    {EShorelineComplexity::VeryLow, 0.80f},
    {EShorelineComplexity::Low, 0.50f},
    {EShorelineComplexity::Medium, 0.30f},
    {EShorelineComplexity::High, 0.10f},
    {EShorelineComplexity::VeryHigh, 0.05f},
};

template<>
struct DomainData<EDomainType::Water> : DomainDataBase
{
    EIslandsCoverage landCoverage = EIslandsCoverage::Medium;
    EAmountOfIslands amountOfSmallIslands = EAmountOfIslands::Medium;
    EAmountOfIslands amountOfMediumIslands = EAmountOfIslands::Medium;
    EAmountOfIslands amountOfLargeIslands = EAmountOfIslands::Medium;     
    EShorelineComplexity shorelineComplexity = EShorelineComplexity::Medium;

    virtual void fillProps(QSharedPointer<OmnigenPropertyListBase> props) override;
    virtual QString makeName(bool isInitial = false) override;

    FRIEND_OMNIBIN(DomainData<EDomainType::Water>);
};

inline void omniSave(const DomainData<EDomainType::Water>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DomainDataBase&>(object);
    omniBin << object.landCoverage;
    omniBin << object.amountOfSmallIslands;
    omniBin << object.amountOfMediumIslands;
    omniBin << object.amountOfLargeIslands;
    omniBin << object.shorelineComplexity;
}

inline void omniLoad(DomainData<EDomainType::Water>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DomainDataBase&>(object);
    omniBin >> object.landCoverage;
    omniBin >> object.amountOfSmallIslands;
    omniBin >> object.amountOfMediumIslands;
    omniBin >> object.amountOfLargeIslands;
    omniBin >> object.shorelineComplexity;
}