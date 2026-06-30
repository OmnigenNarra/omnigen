#pragma once
#include "Utils/EnumAsConstexpr.h"

enum class EGenerationStage
{
    Layout,             // Domains
    Landmasses,         // Shore + LandmassBounds
    Ridges,
    ContourLines,       // Isohypses
    TerrainModel,       // DEM
    Lithomap,
    TerrainClassification,
    FeaturePlacement,
    FeatureGeneration,
    ModAssignment,
    UrbanLayout,
    UrbanSites,
    Foliage,
    TerrainFinalization,

    Last,                // Aux
    LastFunctional = Last - 1
};
ENABLE_ENUM_AS_CONSTEXPR(EGenerationStage, EGenerationStage::Last)