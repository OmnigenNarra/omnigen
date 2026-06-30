#pragma once
#include "../StageGenerationBase.h"
#include "Scene/Generation/OmnigenGenerationData.h"

struct LineMarkerPoint;

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::Landmasses>
    {
    public:
        static void initialize();
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate();
        static void finalize();

    private:
        static void computeDomainBoundHeights(std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>* result);
        static void computeSimplifiedShorelinesPerDomain(std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>* result);
    };
}