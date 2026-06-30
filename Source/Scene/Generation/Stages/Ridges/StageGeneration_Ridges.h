#pragma once
#include "../StageGenerationBase.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"

class DRidgeMarker;
class RidgePeakData;

namespace Generation
{
    struct IHHeightGraphEdge
    {
    };

    struct IHHeightGraphNode
    {
        QVector3D pos;
        float height = -1;
        qint64 ridgeId = -1;
        qint64 rootRidgeId = -1;
        int ridgeTier = -1;

        std::map<int, IHHeightGraphEdge> edges;
    };

    template<>
    class StageGen<EGenerationStage::Ridges>
    {
    public:
        static void initialize() {};
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate();
        static void finalize();

        // `heights` require proper values at elements under indices stored in `peakIndices`
        static void computeRidgeHeight(std::vector<float>* heights, const std::vector<QVector3D>& cPts, const std::vector<int>& sortedPeakIndices, QSharedPointer<DRidgeMarker> parentRidge);

    private:
        static QSet<QSet<GPoint>> computeRidgeValidLandmasses();
        static void calculateRidgelineHeight(std::vector<RidgePeakData>* peakData);
        static std::vector<QSharedPointer<DRidgeMarker>> assignHeightToRidge(QSharedPointer<DRidgeMarker> ridge, const std::vector<RidgePeakData>& peakData);

        friend class DRidgeMarker;
    };
}