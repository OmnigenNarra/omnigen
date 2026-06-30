#pragma once
#include <Mathematics/NURBSCurve.h>

#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanUtils.h"
#include "Utils/Polygon.h"
#include "Utils/QuadTreeLite.h"

/*
 * Simple wrapper for a terrain vertex to store cluster and vertex ids.
 */
using TerrainVertexData = std::pair<int, int>;

//Store the 2 NURBS curves along with the index of the original marker
using RoadNurbs = std::shared_ptr<gte::NURBSCurve<2, float>>;
using RoadSplineData = std::pair<RoadNurbs, RoadNurbs>;
using RoadNurbsLines = std::pair<std::vector<QVector3D>, std::vector<QVector3D>>;

class DRoadMarker : public DLineMarker
{
public:
    DRoadMarker() = default;
    DRoadMarker(const std::vector<QVector3D>& inControlPoints, Segment2D inEdge);
    ~DRoadMarker() override;

    // Rendering
    //IMPLEMENT_SHOULD_DRAW();
    virtual bool shouldDraw(int vIdx) const override { return false; }

    bool operator==(const DRoadMarker& other) const
    {
        return OmnigenDrawable::operator==(other);
    }

    //Return the average height of the 3D nodes.
    [[nodiscard]] float getNodeHeightAverage();

    [[nodiscard]] std::vector<std::pair<TerrainVertexData, float>>  getVertexModifications() const;

    [[nodiscard]] static std::vector<QVector3D> sampleNurbs(const RoadNurbs& spline, const int sampleAmount);
    [[nodiscard]] Polygon2D getBounds() const { return bounds; }

    void setTerrainVertices(std::vector<TerrainVertexData> vs) { terrainVs = std::move(vs); }
    const auto& getTerrainVertices() const { return terrainVs; }
    QVector3D getClosestPoint(const QVector3D& fromPt) const;

    Segment2D parentEdge = {};
    ERoadWidth desiredWidth = ERoadWidth::MainRoad;
    bool isPrimary = false;
private:
    void cacheTerrainGeometry();

    [[nodiscard]] std::tuple<std::vector<GVector2D>, std::vector<GVector2D>> getGeometryBounds() const;
    [[nodiscard]] RoadNurbs getCurve(const std::vector<GVector2D>& pts) const;

    [[nodiscard]] std::pair<TerrainVertexData, float> getVertexWeight(const TerrainVertexData& vert) const;

    int computedAverageHeightsNum = -1;
    float heightAverage = 0.f;

    static const inline QVector4D color = QVector4D(0, 1, 0, 1);

    int id = -1;
    float roadStep = 0.f;

    RoadNurbs curve;
    RoadSplineData splines;
    RoadNurbsLines lines;

    std::shared_ptr<tml::qtree<float, IndexType>> boundQTree;

    Polygon2D bounds;
    std::vector<TerrainVertexData> terrainVs;

    const int imax = 512;

    //Store nurbs markers
    std::array<qint64, 2> nurbsMarkersIds = {};

    FRIEND_OMNIBIN(DRoadMarker);
};

inline void omniSave(const DRoadMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DLineMarker&>(object);
    omniBin << object.parentEdge;
    omniBin << object.desiredWidth;
}

inline void omniLoad(DRoadMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DLineMarker&>(object);
    omniBin >> object.parentEdge;
    omniBin >> object.desiredWidth;
}