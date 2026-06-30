#include "stdafx.h"
#include "StageGeneration_UrbanLayout.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "UrbanSuggestion.h"

namespace Generation
{
    bool StageGen<EGenerationStage::UrbanLayout>::autoGen()
    {
        UrbanSuggestion::generateSuggestions();
        return true;
    }

    void StageGen<EGenerationStage::UrbanLayout>::clear()
    {
        Generation::Data::get()->setUrbanSuggestions({});
    }
}
