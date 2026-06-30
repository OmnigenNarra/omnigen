#pragma once
#include "../StageGenerationBase.h"
#include "RuralGen/RuralRoadGenerator.h"

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::UrbanSites>
    {
    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize() {};

    private:
        RuralRoadGenerator plotter = {};
    };
}
