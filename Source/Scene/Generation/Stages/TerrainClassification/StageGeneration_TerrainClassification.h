#pragma once
#include "../StageGenerationBase.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include <concepts>

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::TerrainClassification>
    {
    public:
        static void initialize();
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate();
        static void finalize();

        // Assign terrain forms to Voronoi cells
        static void selectBlocks();

    private:
        static std::vector<Generation::BlockChanceData> computeBlockChanceData();
        static int compareNeighbhorsAlongRidge(int lookupDepth, const std::vector<Segment2D>& ridgeSegments, int index, int height, bool succesor);
        static int findNeighborAlongRidge(const std::vector<Segment2D>& ridgeSegments, int cellIndex, bool succesor);
    };
}