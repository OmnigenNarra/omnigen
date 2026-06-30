#pragma once
#include "Layout/StageGeneration_Layout.h"
#include "Landmasses/StageGeneration_Landmasses.h"
#include "Ridges/StageGeneration_Ridges.h"
#include "ContourLines/StageGeneration_ContourLines.h"
#include "Lithomap/StageGeneration_Lithomap.h"
#include "TerrainModel/StageGeneration_TerrainModel.h"
#include "TerrainClassification/StageGeneration_TerrainClassification.h"
#include "FeaturePlacement/StageGeneration_FeaturePlacement.h"
#include "FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "TerrainMods/StageGeneration_TerrainMods.h"
#include "UrbanLayout/StageGeneration_UrbanLayout.h"
#include "UrbanSites/StageGeneration_UrbanSites.h"
#include "Foliage/StageGeneration_Foliage.h"
#include "TerrainFinalization/StageGeneration_TerrainFinalization.h"
static_assert(int(EGenerationStage::Last) == __LINE__ - 2); // must include all stage gen classes

#include "Omnigen.h"

namespace EAC
{
    // use only for loading
    struct SetStageState
    {
        template<EGenerationStage GS>
        static void Action(Generation::FStageStates state)
        {
            Generation::StageTraits<GS>::state = state;
        }
    };

    struct GetStageState
    {
        template<EGenerationStage GS>
        static Generation::FStageStates Action()
        {
            return Generation::StageTraits<GS>::state;
        }
    };

    struct InitializeStage
    {
        template<EGenerationStage GS>
        static void Action()
        {
            OmniLog(ELoggingLevel::Trace) << "Initializing stage: " <<= magic_enum::enum_name(GS);
            Generation::Data::get()->setCurrentGeneratedStage(GS);
            Generation::StageGen<GS>::initialize();
            Generation::Data::get()->setCurrentGeneratedStage({});
            Generation::Data::get()->initializeQueuedMarkers();

            Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state | Generation::FStageStates::HasDataToSave;
            Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state | Generation::FStageStates::HasBeenInitialized;
        }
    };

    struct CheckStageAutoGen
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            return Generation::StageGen<GS>::hasAutoGen();
        }
    };

    struct AutoGenStage
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            if constexpr (Generation::StageGen<GS>::hasAutoGen())
            {
                OmniLog(ELoggingLevel::Trace) << "Generating stage: " <<= magic_enum::enum_name(GS);
                Generation::Data::get()->setCurrentGeneratedStage(GS);
                bool result = Generation::StageGen<GS>::autoGen();
                Generation::Data::get()->setCurrentGeneratedStage({});
                Generation::Data::get()->initializeQueuedMarkers();
                return result;
            }
            else
                return true;
        }
    };

    struct ValidateStage
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            OmniLog(ELoggingLevel::Trace) << "Validating stage: " <<= magic_enum::enum_name(GS);
            bool result = Generation::StageGen<GS>::validate();
            if (result)
                Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state | Generation::FStageStates::HasBeenValidated;

            return result;
        }
    };

    struct HasDataToSave
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            return !!(Generation::StageTraits<GS>::state & Generation::FStageStates::HasDataToSave);
        }
    };

    struct HasBeenInitialized
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            return !!(Generation::StageTraits<GS>::state & Generation::FStageStates::HasBeenInitialized);
        }
    };

    struct HasValidState
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            return !!(Generation::StageTraits<GS>::state & Generation::FStageStates::HasBeenValidated);
        }
    };

    struct HasBeenFinalized
    {
        template<EGenerationStage GS>
        static bool Action()
        {
            return !!(Generation::StageTraits<GS>::state & Generation::FStageStates::HasBeenFinalized);
        }
    };

    struct FinalizeStage
    {
        template<EGenerationStage GS>
        static void Action()
        {
            OmniLog(ELoggingLevel::Trace) << "Finalizing stage: " <<= magic_enum::enum_name(GS);
            Generation::Data::get()->setCurrentGeneratedStage(GS);
            Generation::StageGen<GS>::finalize();
            Generation::Data::get()->setCurrentGeneratedStage({});
            Omnigen::get()->autoSave(GS);
            Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state | Generation::FStageStates::HasBeenFinalized;
        }
    };

    struct ClearStage
    {
        template<EGenerationStage GS>
        static void Action()
        {
            OmniLog(ELoggingLevel::Trace) << "Clearing stage: " <<= magic_enum::enum_name(GS);
            Generation::Data::get()->setCurrentGeneratedStage(GS);
            Generation::StageGen<GS>::clear();
            Generation::StageTraits<GS>::state = Generation::FStageStates::None;
            Generation::Data::get()->setCurrentGeneratedStage({});
        }
    };

    struct InvalidateStage
    {
        template<EGenerationStage GS>
        static void Action(bool forceReinitialization = false)
        {
            if (forceReinitialization)
                Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state & ~Generation::FStageStates::HasBeenInitialized;
            Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state & ~Generation::FStageStates::HasBeenValidated;
            Generation::StageTraits<GS>::state = Generation::StageTraits<GS>::state & ~Generation::FStageStates::HasBeenFinalized;
        }
    };
}