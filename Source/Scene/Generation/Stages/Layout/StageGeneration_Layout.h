#pragma once
#include "../StageGenerationBase.h"
#include <QSet>
#include "Utils/CoreUtils.h"

class DDomain;

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::Layout>
    {
    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return false; }
        static bool autoGen() {};
        static void clear();
        static bool validate();
        static void finalize();

    private:
        static bool validateContinuity();
        static bool validateSquares();
        static bool validateSeasides();
        static bool validateNoHoles();
        static bool validateLithology();

        static std::unordered_map<qint64, float> computeDomainMinimumHeight();
    };

    namespace Utils
    {
        QSet<GPoint> generateRandomContinuousSubset(const QSet<GPoint>& source, int subsetSize, const GPoint& seed);
        std::vector<Segment2D> computeSharedPerimeter(QSharedPointer<DDomain> D1, QSharedPointer<DDomain> D2);
        std::vector<QSet<GPoint>> findSeasideAreas(const QSet<GPoint>& ignoreSquares = {});
        std::vector<QSet<GPoint>> findLandAreas();
    }
}