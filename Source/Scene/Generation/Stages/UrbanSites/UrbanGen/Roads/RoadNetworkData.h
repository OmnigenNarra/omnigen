#pragma once
#include "RoadMarker.h"
#include "Utils/AdjacencyGraph.h"

struct BaseEdgeInfo
{
    Segment2D edge = {};

    //TODO: Refactor
    QSharedPointer<DRoadMarker> correspondingRoad = nullptr;

    BaseEdgeInfo() = default;
    BaseEdgeInfo(const Segment2D& inSeg)
        : edge(inSeg) {}
};

class RoadNetworkData
{
public:
    RoadNetworkData(const bool complexTrace);

    bool shouldComplexTrace() const { return needsComplexTrace; }

    AdjacencyGraph roadGraphHighLevel;
    AdjacencyGraph roadGraphLowLevel;

    std::vector<Segment2D> roadTargets;

    std::vector<BaseEdgeInfo> baseEdges;
    std::vector<QSharedPointer<DRoadMarker>> roads;

    Polygon2D bounds;

    void computeRoadGraphHigh();
    void computeRoadGraphLow();

    [[nodiscard]] std::vector<Polygon2D> getEnclosingRegions() const;

    static void insertToGraph(const std::vector<QVector3D>& pts, AdjacencyGraph* graph);

private:
    bool needsComplexTrace = false;
};