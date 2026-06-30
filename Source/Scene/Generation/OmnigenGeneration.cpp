#include "stdafx.h"
#include "OmnigenGeneration.h"
#include "Stages/StageGeneration.h"

#include "Omnigen.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/StageTools/StageTools.h"

static const QString gClustersBackupPath = QDir::tempPath() + "/OMNIGEN_clustersBackup";

namespace Generation
{
    EGenerationStage processStages(EGenerationStage from, EGenerationStage to)
    {
        OmniStartProfiling;

        int dir = int(to) - int(from);
        if (dir == 0)
        {
            // Regenerate current stage if possible
            if (EGenerationStageConstexpr::UseIn<EAC::CheckStageAutoGen>(to))
            {
                Generation::Data::get()->clearDebugMarkers();
                EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(to)->clearHistory();
                EGenerationStageConstexpr::UseIn<EAC::ClearStage>(to);
                EGenerationStageConstexpr::UseIn<EAC::InitializeStage>(to);
                EGenerationStageConstexpr::UseIn<EAC::AutoGenStage>(to);
                Generation::Data::get()->initializeQueuedMarkers();
            }
        }
        else if (dir < 0)
        {
            // Go back
            Generation::Data::get()->clearDebugMarkers();

            int lastToRevert = (dir == 0) ? int(to) : std::min(int(EGenerationStage::LastFunctional), int(to) + 1);
            for (int stage = int(from); stage >= lastToRevert; --stage)
                stepBack(EGenerationStage(stage), EGenerationStage(stage - 1));
        }
        else if (dir > 0)
        {
            // Go forward: Finalize current stage, only initialize the target stage
            int firstToGenerate = std::min(int(EGenerationStage::LastFunctional), int(from) + 1);
            for (int stage = firstToGenerate; stage <= int(to) && stage <= int(EGenerationStage::LastFunctional); ++stage)
            {
                EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(stage))->aboutToEnterStage(dir);

                if (!stepForward(EGenerationStage(stage - 1), EGenerationStage(stage), stage == int(to)))
                {
                    EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(stage))->aboutToExitStage(-1);
                    return EGenerationStage(stage - 1);
                }

                EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(stage - 1))->aboutToExitStage(dir);
            }

            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(to))->clearHistory();
        }

        return to;
    }

    bool stepForward(EGenerationStage from, EGenerationStage to, bool isLastStep)
    {
        if (int(to) >= magic_enum::enum_count<EGenerationStage>())
            return true;

        // The first step is validated in setGenerationStage
        if (!EGenerationStageConstexpr::UseIn<EAC::HasValidState>(from) && !EGenerationStageConstexpr::UseIn<EAC::ValidateStage>(from))
        {
            if (!Omnigen::get()->isSectionVisible(EOmnigenSection::Log))
                Omnigen::get()->toggleSectionVisibility(EOmnigenSection::Log);

            return false;
        }

        if (!EGenerationStageConstexpr::UseIn<EAC::HasBeenFinalized>(from))
            EGenerationStageConstexpr::UseIn<EAC::FinalizeStage>(from);

        if (!EGenerationStageConstexpr::UseIn<EAC::HasBeenInitialized>(to))
        {
            EGenerationStageConstexpr::UseIn<EAC::InitializeStage>(to);
            return isLastStep || EGenerationStageConstexpr::UseIn<EAC::AutoGenStage>(to);
        }
        
        return true;
    }

    bool stepBack(EGenerationStage from, EGenerationStage to)
    {
        if (int(to) < 0)
            return true;

        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(from)->clearHistory();
        EGenerationStageConstexpr::UseIn<EAC::ClearStage>(from);
        return true;
    }


    EventManager<Generation::EGenerationEvent>& getEventMgr()
    {
        static EventManager<EGenerationEvent> eventMgr;
        return eventMgr;
    }

    float randomChance()
    {
        static std::uniform_real_distribution<double> test(0.0, 1.0);
        return test(gRandomEngine);
    }
}