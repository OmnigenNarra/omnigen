#pragma once
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/UrbanSite.h"
#include "RoadGenerator.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/Roads/RoadFixedTracer.h"

class RuralRoadGenerator;

void omniSave(const RuralRoadGenerator& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(RuralRoadGenerator& object, OmniBin<std::ios::in>& omniBin);

struct TargetConnectionInfo
{
    QVector3D start = {};
    QVector3D end = {};

    TargetConnectionInfo() = default;
    TargetConnectionInfo(const GVector2D& inStart, const GVector2D& inEnd);
};

class RuralPathFinder
{
    std::vector<QVector3D> finalPath;
    TargetConnectionInfo connInfo;

public:
    RuralPathFinder() = default;
    RuralPathFinder(TargetConnectionInfo inInfo);
    bool compute();
    [[nodiscard]] const auto& getFinalPath() const { return finalPath; }

private:
    std::vector<QVector3D> computeTargetPath() const;
    bool computeRoadPath();
    
    //Post-process
    std::vector<QVector3D> adjustSlopesOverMax(const RoadFixedTracerResult& inRoadData);

    [[nodiscard]] std::vector<QVector3D> getSeedsAroundSlope(const Segment2D& initialSeg, const QVector3D& targetVertex, const RoadFixedTracerResult& inRoadData);

    RoadFixedTracer roadTracer;
};

class RuralRoadGenerator
{
public:
    virtual void generate();
    //Revert any world changes.
    virtual void revertGen();
protected:
    virtual void computeRoadPlotters();

    void generateRuralRoadPaths();

    [[nodiscard]] TargetConnectionInfo getSiteConnectionPoints(const QSharedPointer<Generation::UrbanSite>& site1,
        const QSharedPointer<Generation::UrbanSite>& site2) const;

    [[nodiscard]] std::vector<TerrainVertexData> getRoadTerrainVertices(const int id) const;
private:
    RoadPainter roadPainter = {};

    std::vector<QSharedPointer<DRoadMarker>> internalRoads;
    std::vector<TargetConnectionInfo> connections;

    //TODO: Sync with path finder
    const float roadTraceStep = 100.0f;

    FRIEND_OMNIBIN_NS(RuralRoadGenerator);
};

inline void omniSave(const RuralRoadGenerator& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.roadPainter;
}

inline void omniLoad(RuralRoadGenerator& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.roadPainter;
}
