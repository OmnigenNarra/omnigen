#pragma once
#include "District.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "../../../UrbanLayout/UrbanUtils.h"
#include "Utils/Voronoi/Voronoi.h"

void omniSave(const DistrictNetwork& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DistrictNetwork& object, OmniBin<std::ios::in>& omniBin);

namespace Generation {
    class UrbanSite;
}

struct RoadControlData;

struct NetworkTopologyData
{
    Polygon2D urbanSiteArea;
    EUrbanSize urbanSiteSize;
};

struct DistrictCreationInfo
{
    IndexType id;
    Polygon2D cellBounds;
    Polygon2D roadBounds;
};

class DistrictNetwork
{
public:
    DistrictNetwork() = default;
    explicit DistrictNetwork(NetworkTopologyData data, bool perimeterEdges);

    void showNetwork(bool drawCenters = true) const;

    [[nodiscard]] const std::vector<District>& getDistricts() const { return districts; }
    [[nodiscard]] const Voronoi::PolygonDiagram& getDiagram() const { return diagram; }

    [[nodiscard]] std::vector<Segment2D> getPrimaryRoadEdges() const;

    [[nodiscard]] const GVector2D& getCenter() const { return center; }
    [[nodiscard]] const Polygon2D& getAreaBounds() const { return sizeData.urbanSiteArea; }

    void calculateDistricts(const std::vector<DistrictCreationInfo>& info);
private:
    [[nodiscard]] GVector2D findInitialSeed(bool bUseZAxis = false) const;
    [[nodiscard]] BoundingBox findBestBoxForInitialSeed() const;

    [[nodiscard]] int getDefaultNumberOfDistricts(const EUrbanSize& urbanSize) const;
    [[nodiscard]] std::vector<GVector2D> calculateNonCentralSeeds() const;
    [[nodiscard]] std::vector<GVector2D> getSeedsInRingsDistribution(const Polygon2D& area, const GVector2D& center, 
        const int numberOfSeeds, const int numberOfRings) const;

    [[nodiscard]] std::vector<ERoadPattern> getDistrictsRoadPatterns(const EUrbanSize& urbanSize) const;

    BoundingBox areaBoundingBox;

    QSize networkSize = QSize(0, 0);

    float pointDensity = 20.0f;

    GVector2D center;

    Voronoi::PolygonDiagram diagram;
    std::vector<District> districts;

    NetworkTopologyData sizeData;

    bool computePerimeterEdges = true;

    friend Generation::UrbanSite;
    FRIEND_OMNIBIN_NS(DistrictNetwork);
};

inline void omniSave(const DistrictNetwork& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.areaBoundingBox;
    omniBin << object.networkSize;
    omniBin << object.pointDensity;
    omniBin << object.center;
    omniBin << object.sizeData.urbanSiteArea;
    omniBin << object.diagram;
    omniBin << object.districts;
}

inline void omniLoad(DistrictNetwork& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.areaBoundingBox;
    omniBin >> object.networkSize;
    omniBin >> object.pointDensity;
    omniBin >> object.center;
    omniBin >> object.sizeData.urbanSiteArea;
    omniBin >> object.diagram;
    omniBin >> object.districts;
}
