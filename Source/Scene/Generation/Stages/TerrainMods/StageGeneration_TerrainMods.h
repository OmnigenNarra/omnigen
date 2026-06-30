#pragma once
#include "../StageGenerationBase.h"

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::ModAssignment>
    {
    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize();

    private:
        // Detect areas for Mods and create them
        static void generateMods();
        // Modify terrain using Mods
        static void applyMods();
        static void revertMods();
    };
}