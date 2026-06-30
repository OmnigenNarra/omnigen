#pragma once
#include "Scene/Generation/Stages/FeatureGeneration/ClusterMeshMarker.h"
#include "Scene/Generation/Stages/UrbanSites/Buildings/BuildingGenerator.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/Roads/RoadMarker.h"
#include "Utils/Polygon.h"
#include "Utils/OmniBin/OmniBinQt.h"

class RoadPainter;

void omniSave(const RoadPainter& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(RoadPainter& object, OmniBin<std::ios::in>& omniBin);


class RoadPainter
{
public:
    RoadPainter() = default;

    //Paint roads in the terrain based on the data from the plotters.
    void paintRoad(const QSharedPointer<DRoadMarker>& road);
    void revertRoadPainting();
protected:
    //Paint a terrain vertex to look like part of a road. Returns new and old pack param values as a pair.
    std::pair<float, float> paintVertex(TerrainMeshVertex* v, const float weight) const;

    std::unordered_map<TerrainVertexData, std::pair<float, float>> paintMap;

    FRIEND_OMNIBIN_NS(RoadPainter);
};

inline void omniSave(const RoadPainter& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.paintMap;
}

inline void omniLoad(RoadPainter& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.paintMap;
}

class LotExtractor
{
public:

    LotExtractor(const std::vector<Polygon2D>& inBlocks, const float desiredLotSize = 600.f);

    [[nodiscard]] std::vector<BuildingLotInfo> getLots() const;

private:
    std::vector<BuildingLotInfo> extract(const Polygon2D& inBlock) const;
    static GVector2D getDeviatedMidpoint(const Segment2D& forEdge);

    std::vector<Polygon2D> blocks;
    float lotSize;
};
