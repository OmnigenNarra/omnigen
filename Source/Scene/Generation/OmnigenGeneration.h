#pragma once
#include "Utils/OmnigenDirectCompute.h"
#include "OmnigenGenerationStage.h"
#include "Utils/CoreUtils.h"
#include "Utils/Event.h"

namespace Generation
{
    // Entry point
    EGenerationStage processStages(EGenerationStage from, EGenerationStage to);
    bool stepForward(EGenerationStage from, EGenerationStage to, bool isLastStep);
    bool stepBack(EGenerationStage from, EGenerationStage to);

    enum class EGenerationEvent
    {
        Generated,
        Count
    };
    EventManager<EGenerationEvent>& getEventMgr();

    float randomChance();

    static QMap<EGenerationStage, std::array<std::function<bool()>, 2>> generationStageLogic;
    static std::mt19937 gRandomEngine;
    static OmnigenDirectCompute gDirectCompute;
}