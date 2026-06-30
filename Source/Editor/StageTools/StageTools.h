#pragma once
#include "Layout/StageToolsLayout.h"
#include "Landmasses/StageToolsLandmasses.h"
#include "Ridges/StageToolsRidges.h"
#include "ContourLines/StageToolsContourLines.h"
#include "TerrainModel/StageToolsTerrainModel.h"
#include "Lithomap/StageToolsLithomap.h"
#include "TerrainClassification/StageToolsTerrainClassification.h"
#include "FeaturePlacement/StageToolsFeaturePlacement.h"
#include "FeatureGeneration/StageToolsFeatureGeneration.h"
#include "ModAssignment/StageToolsModAssignment.h"
#include "UrbanLayout/StageToolsUrbanLayout.h"
#include "UrbanSites/StageToolsUrbanSites.h"
#include "Foliage/StageToolsFoliage.h"
#include "TerrainFinalization/StageToolsTerrainFinalization.h"
static_assert(int(EGenerationStage::LastFunctional) == __LINE__ - 3); // must include all stage tools classes

namespace EAC
{
    struct GetStageTools
    {
        template<EGenerationStage GS>
        static Design::StageToolsBase* Action()
        {
            static Design::StageTools<GS> instance;
            return &instance;
        }
    };
}

// Get casted tools
template<EGenerationStage GS>
static inline auto* getStageTools()
{
    auto* tools = EAC::GetStageTools::Action<GS>();
    return static_cast<Design::StageTools<GS>*>(tools);
}