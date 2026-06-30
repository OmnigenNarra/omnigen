#pragma once
#include "RoadFixedTracer.h"
#include "RoadNetworkData.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Scene/Generation/Stages/UrbanSites/Buildings/BuildingGenerator.h"
#include "Scene/Generation/Stages/UrbanSites/RuralGen/RoadGenerator.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/Core/DistrictNetwork.h"
#include "Utils/Polygon.h"

class RoadNetworkData;

namespace Generation
{
    class UrbanSite;
}

class DRoadMarker;
class UrbanTopologyGenerator;

void omniSave(const UrbanTopologyGenerator& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(UrbanTopologyGenerator& object, OmniBin<std::ios::in>& omniBin);

struct TerrainAdjustmentData
{
    IndexType vertexId = -1;
    QVector3D originalPos = {};
    QVector3D adjustedPos = {};
    float originalDisplacement = 0.f;
    float adjustedDisplacement = 0.f;

    TerrainAdjustmentData() = default;
};

inline void omniSave(const TerrainAdjustmentData& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.vertexId;
    omniBin << object.originalPos;
    omniBin << object.adjustedPos;
    omniBin << object.originalDisplacement;
    omniBin << object.adjustedDisplacement;
}
inline void omniLoad(TerrainAdjustmentData& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.vertexId;
    omniBin >> object.originalPos;
    omniBin >> object.adjustedPos;
    omniBin >> object.originalDisplacement;
    omniBin >> object.adjustedDisplacement;
}

class UrbanTopologyGenerator
{
    enum ERoadType
    {
        Primary,
        Secondary
    };

public:
    UrbanTopologyGenerator() = default;
    explicit UrbanTopologyGenerator(const Generation::UrbanSite* inParent, std::vector<Segment2D> inPrimaryEdges);

    void generate();
    void clear();

    [[nodiscard]] const auto& getBuildingLots() const { return buildingLots; }
    [[nodiscard]] const auto& getPrimaryRoads() const { return roadData.at(Primary); }
    [[nodiscard]] const auto& getSecondaryRoads() const { return roadData.at(Secondary); }

    [[nodiscard]] const auto& getNodesQTree() const { return nodesQTree; }

    [[nodiscard]] size_t getFacesInArea() const;

    std::vector<DistrictCreationInfo> computeDistrictBounds();
private:
    void tracePrimaryRoads();

    void traceSecondaryRoads();

    //Adjust the terrain based on the generated roads.
    void adjustTerrain();
    void revertTerrainAdjustment();

    void extractBuildingBlocks();
    void extractBuildingLots();

    void paintRoads();

    void smoothTerrainVertex(const int cluster, const int idx);

    // Gets the snapped road of A --> B by only including those 2 vertices. Height IS NOT a tracing factor.
    [[nodiscard]] QSharedPointer<DRoadMarker> traceSimpleRoad(const Segment2D& initialSegment, ERoadType roadType);

    // Gets the snapped road of A --> B by a collection of subdivided vertices per the tracing step. Height IS NOT a tracing factor.
    [[nodiscard]] QSharedPointer<DRoadMarker> traceComplexRoadNoHeight(const Segment2D& initialSegment, ERoadType roadType) const;

    // Gets the snapped road of A --> B by a collection of subdivided vertices per the tracing step. Height IS a tracing factor.
    [[nodiscard]] QSharedPointer<DRoadMarker> traceComplexRoad(const Segment2D& initialSegment, const Polygon2D& bounds) const;

    // Traces a path from segment.first --> segment.second by using an A* through the terrain geometry.
    [[nodiscard]] std::vector<QVector3D> traceTerrainBasedRoad(const Segment2D& initialSegment) const;

    [[nodiscard]] std::optional<GVector2D> getNextBestSeed(const GVector2D& startingPoint, const GVector2D& endPoint,
        const GVector2D& offsetV, const float targetElevation) const;
private:
    std::unordered_map<ERoadType, std::vector<QSharedPointer<RoadNetworkData>>> roadData;

    RoadPainter roadPainter = {};
    RoadFixedTracer roadTracer = {};

    std::vector<Polygon2D> buildingBlocks;
    //Subdivided blocks
    std::vector<BuildingLotInfo> buildingLots;

    const Generation::UrbanSite* parent = nullptr;

    std::unordered_map<IndexType, std::vector<TerrainAdjustmentData>> terrainModifications;
    std::vector<qint64> terrainModMarkers;

    //TODO: Obsolete
    std::shared_ptr<tml::qtree<float, QVector3D>> nodesQTree;

    //TODO: Potentially allow for different values per streamline and / or base it off of segment distance
    int roadTraceDensity = 10;
    float roadTraceStep = 300.0f;

    std::shared_ptr<tml::qtree<float, size_t>> terrainVertexQTree;

    size_t cachedFacesNum = 0;

    friend class RoadVertexSearchExecutor;
    friend class RoadVertexSearch;

    FRIEND_OMNIBIN_NS(UrbanTopologyGenerator);
};

 inline void omniSave(const UrbanTopologyGenerator& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.terrainModifications;
    omniBin << object.roadPainter;
    omniBin << object.roadTraceDensity;
    omniBin << object.roadTraceStep;
}

inline void omniLoad(UrbanTopologyGenerator& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.terrainModifications;
    omniBin >> object.roadPainter;
    omniBin >> object.roadTraceDensity;
    omniBin >> object.roadTraceStep;
}