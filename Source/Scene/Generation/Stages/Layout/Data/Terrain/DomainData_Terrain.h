#pragma once
#include "../DomainDataBase.h"
#include "Landform.h"

enum class ERivers
{
    None,
    Few,
    Many
};

enum class ELithoTexSlot
{
    RockSlab,
    RockGrain,
    Soil,
    Vertical
};

static const QMap<ERivers, QPair<double, double>> PRivers =
{
    { ERivers::None, QPair{0.0, 0.0} },
    { ERivers::Few, QPair{0.1, 0.2} },
    { ERivers::Many, QPair{0.3, 0.6} }
};

enum class ERidgeSize
{
    Small,
    Medium,
    Large
};

enum class ERidgeComplexity
{
    FewSubridges,
    SomeSubridges,
    PlentySubridges
};

enum class ERidgeSpread
{
    Shorter,
    Uniformous,
    Longer
};

struct RidgeGenParams
{
    mutable float slopeAngle = -1.0f;
    mutable float ridgelineAngle = -1.0f;
    mutable ERidgeSize size = ERidgeSize::Large;
    mutable ERidgeComplexity complexityMain = ERidgeComplexity::FewSubridges;
    mutable ERidgeComplexity complexitySub = ERidgeComplexity::FewSubridges;
    mutable ERidgeSpread spread = ERidgeSpread::Uniformous;
};

struct RidgeCharacter
{
    std::map<ERidgeSize, std::pair<int, int>> size;
    std::map<ERidgeComplexity, std::pair<int, int>> complexityMain;
    std::map<ERidgeComplexity, std::pair<int, int>> complexitySub;
    std::map<ERidgeSpread, std::pair<float, float>> spread;
};

extern RidgeCharacter PRidgeCharacter;

template<>
struct DomainData<EDomainType::Terrain> : DomainDataBase
{
    ELandform landform = ELandform::Plains;
    ELandformVariations landformVariation = ELandformVariations::PlainsBasic;
    ETableLand tableland = ETableLand::Plateau;
    ERivers rivers = ERivers::Few;
    mutable float maxHeight = -1.0f;
    mutable float minHeight = -1.0f;
    float hillsSmoothness = 0.f;
    float landformOpenness = 0.0f;
    float desiredRidgeCoverage = 0.0f;
    RidgeGenParams ridgeGenParams;
    std::optional<LandformParams> landformInstanceParams = {};
    std::vector<ELandformVariations> allowedVariations;

    virtual void fillProps(QSharedPointer<OmnigenPropertyListBase> props) override;
    virtual QString makeName(bool isInitial = false) override;

    void calculateMaxHeight(qint64 ownerId);
    ELandformVariations getDefaultVariation(ELandform lf);
    std::vector<ELandformVariations> getAvailableVariations(ELandform lf);

    FRIEND_OMNIBIN(DomainData<EDomainType::Terrain>);
};

inline void omniSave(const DomainData<EDomainType::Terrain>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DomainDataBase&>(object);
    omniBin << object.landform;
    omniBin << object.landformVariation;
    omniBin << object.tableland;
    omniBin << object.rivers;
    omniBin << object.maxHeight;
    omniBin << object.minHeight;
    omniBin << object.ridgeGenParams;
    omniBin << object.landformInstanceParams;
    omniBin << object.hillsSmoothness;
    omniBin << object.landformOpenness;
    omniBin << object.desiredRidgeCoverage;
}

inline void omniLoad(DomainData<EDomainType::Terrain>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DomainDataBase&>(object);
    omniBin >> object.landform;
    omniBin >> object.landformVariation;
    omniBin >> object.tableland;
    omniBin >> object.rivers;
    omniBin >> object.maxHeight;
    omniBin >> object.minHeight;
    omniBin >> object.ridgeGenParams;
    omniBin >> object.landformInstanceParams;
    omniBin >> object.hillsSmoothness;
    omniBin >> object.landformOpenness;
    omniBin >> object.desiredRidgeCoverage;
}