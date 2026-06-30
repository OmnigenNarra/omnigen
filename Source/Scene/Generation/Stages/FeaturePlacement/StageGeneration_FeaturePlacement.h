#pragma once
#include "../StageGenerationBase.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include <concepts>

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::FeaturePlacement>
    {
    public:
        static void initialize();
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate();
        static void finalize();
    };
}