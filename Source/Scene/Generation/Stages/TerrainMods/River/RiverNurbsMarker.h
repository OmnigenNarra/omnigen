#pragma once
#include "Scene/Generation/Common/Markers/NurbsMarker.h"
#include "TerrainModRiverData.h"

class DRiverNurbsMarker : public DNurbsMarker
{
public:
    DRiverNurbsMarker() = default;
    DRiverNurbsMarker(const std::vector<Generation::RiverRowInfo>& riverbed, bool fallsIntoSea, bool clampEnd, bool clampStart, const QVector4D& debugColor);

    const auto& getEdges() const { return nurbsEdges; };
    const auto& getPolygon() const { return nurbsPolygon; };
    const auto& getOrigin() const { return origin; };

    std::optional<float> sampleHeight(const QVector3D& pos) const;

private:
    std::array<std::vector<QVector3D>, 2> nurbsEdges;
    Polygon2D nurbsPolygon;
    QVector3D origin;

    SerializableNurbs<3, float> createSurfaceFromRiver(const std::vector<Generation::RiverRowInfo>& riverbed, bool fallsIntoSea, bool clampEnd, bool clampStart);
    void calculateRiverOrigin();
    void calculateRiverBounds(const std::vector<Generation::RiverRowInfo>& rowData);

    static inline int columnCount = 10;

    FRIEND_OMNIBIN(DRiverNurbsMarker);
};

void omniSave(const DRiverNurbsMarker& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DRiverNurbsMarker& object, OmniBin<std::ios::in>& omniBin);
