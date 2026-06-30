#pragma once
#include "../StageGenerationBase.h"

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::UrbanLayout>
    {
    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; }
        static void finalize() {}
    };
}