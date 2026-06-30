#pragma once
#include "Utils/Polygon.h"
#include "Utils/Voronoi/Voronoi.h"

class District;
void omniSave(const District& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(District& object, OmniBin<std::ios::in>& omniBin);

class DistrictNetwork;

enum class ERoadPattern
{
    Grid,
    CircularGrid,
    Radial,
    Organic
};

class District
{
public:
    District() = default;
    explicit District(const bool isTownCenter, const GVector2D& inCenter, const int inId, DistrictNetwork* inParent);

    void calculateRoadDiagram(const ERoadPattern& withPattern);

    bool getIsTownCenter() const { return isTownCenter; }
    int getId() const { return id; }

    [[nodiscard]] const Voronoi::BoxDiagram& getDiagram() const { return diagram; }
    [[nodiscard]] ERoadPattern getDominantRoadPattern() const { return dominantRoadPattern; }

    [[nodiscard]] std::vector<Segment2D> getRoadEdges() const;

    void showRoadDiagram() const;

    const GVector2D& getCenter() const { return center; }

    float getArea() const { return bounds.getArea(); }

    const Polygon2D& getBounds() const { return bounds; }

    const std::vector<int>& getNeighbors() const { return neighbors; }
    void setBounds(const Polygon2D& inPoly) { bounds = inPoly; }
private:
    std::optional<std::vector<GVector2D>> gatherRoadSeeds(const ERoadPattern& withPattern);

    // Precoded road patterns
    std::vector<GVector2D> getGridSeeds(const GVector2D& rotateTowards, const std::pair<float, float>& densityMinMax, float deviationChance) const;
    std::vector<GVector2D> getCircularGridSeeds(const GVector2D& circleCenter, const std::pair<float, float>& radiusDensityMinMax, const float angleDiff, 
        const std::pair<float, float>& circuitDensityMinMax, float deviationChance) const;
    std::vector<GVector2D> getOrganicSeeds(const int numberOfSeeds = 8) const;
    std::vector<GVector2D> getRadialSeeds(const float radius, const bool withCenter, const int numberOfSeeds = 8) const;

private:
    GVector2D center;

    bool isTownCenter = false;
    int id = -1;

    GVector2D mapCenter = GVector2D();
    Polygon2D bounds;
    Polygon2D seedsBounds;

    float minimumSeedDistance = 400.f;
    float minRoadEdgeDistanceFromBounds = 110.f;

    Voronoi::BoxDiagram diagram;
    std::vector<Polygon2D> districtLots;

    ERoadPattern dominantRoadPattern;

    std::vector<int> neighbors;
    DistrictNetwork* parent = nullptr;
    FRIEND_OMNIBIN_NS(District);
};

inline void omniSave(const District& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.center;
    omniBin << object.isTownCenter;
    omniBin << object.id;
    omniBin << object.mapCenter;
    omniBin << object.bounds;
    omniBin << object.diagram;
    omniBin << object.neighbors;
}

inline void omniLoad(District& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.center;
    omniBin >> object.isTownCenter;
    omniBin >> object.id;
    omniBin >> object.mapCenter;
    omniBin >> object.bounds;
    omniBin >> object.diagram;
    omniBin >> object.neighbors;
}