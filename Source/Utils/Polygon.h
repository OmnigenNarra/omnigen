#pragma once
#include "CoreUtils.h"
#include "CircularVectorView.h"
#include "Scene/OmnigenDrawable.h"
#include "Utils/Clipper/clipper.hpp"
#include <QString>

namespace Interpolation
{
    struct TechniqueNode;
}

using InterpolationParams = std::vector<QSharedPointer<Interpolation::TechniqueNode>>;

class Polygon2D
{
public:
    Polygon2D() = default;
    Polygon2D(std::vector<GVector2D> pts, const bool forceCW = false);
    Polygon2D(const std::vector<QVector3D>& pts, const bool forceCW = false);

    Polygon2D(const Polygon2D& other) = default;
    Polygon2D& operator=(const Polygon2D& other) = default;
    Polygon2D(Polygon2D&& other);
    Polygon2D& operator=(Polygon2D&& other);

    void setPoints(const std::vector<GVector2D>& points);
    void setPoints(std::vector<GVector2D>&& points);

    static Polygon2D inflatePolygon(const Polygon2D& poly, const float amount);
    static Polygon2D inflatePolygonByScale(const Polygon2D& poly, const float amount);
    //Removes self-intersections from the supplied polygon. Polygons with non-contiguous duplicate vertices will be split into two polygons.
    static std::vector<Polygon2D> simplifyPolygon(const Polygon2D& poly);
    static Polygon2D cleanPolygon(const Polygon2D& poly);

    //Computes the convex hull of a list of 2D points and returns it as a polygon.
    static Polygon2D findConvexHull(const std::vector<GVector2D>& inPts);

    //Boolean operations
    enum class EBoolOp
    {
        Intersection,
        Difference,
        XOR,
        Union
    };
    /*
     * Perform a boolean operation on 2 polygons where A is the subject polygon and B is the clip polygon.
     * You can choose between intersection, difference, xor and union.
     * The method returns an array of resulting polygons in case the operation creates more than 1 continuous polygon and an empty vector if the op failed.
     */
    static std::vector<Polygon2D> boolOp(const Polygon2D& A, const Polygon2D& B, const EBoolOp op);

    /*
     * Returns whether the 2 given polygons adjacent or overlapping.
     * Set the inflate flag to true if you want to test for polygons that are right next to each other but not overlapping.
     */
    static bool areAdjacentOrOverlapping(const Polygon2D& A, const Polygon2D& B, const bool inflate = false);

    // Returns random path inside convex polygon
    std::tuple<std::vector<GVector2D>, InterpolationParams> computeRandomPath(const GVector2D& p1, const GVector2D& p2, int numSegments, std::optional<InterpolationParams> params = {}) const;
    bool contains(const GVector2D& p, bool includeEdges = true) const;
    bool contains(const Segment2D& s) const;
    bool containsAll(const std::vector<GVector2D>& pts) const;
    bool containsAny(const std::vector<GVector2D>& pts) const;
    std::vector<std::tuple<int, int, GVector2D>> getClosestEdges(const GVector2D& p) const;
    bool addPoint(int a, int b, const GVector2D& p);
    void addPoint(int ix, const GVector2D& p, const bool forceCW = false);
    void removePoint(int ix);
    void setPoint(int ix, const GVector2D& p);

    // Returns random point inside convex polygon
    GVector2D getRandomPointInsidePolygon() const;

    //Included a shrink to fit call to really clean up the memory
    void clear() { pts.clear(); pts.shrink_to_fit(); lazyRadius = -1; };

    // A version of the contains method that is accurate for concave polygons.
    bool containsConcave(const GVector2D& p, bool boundsContained = true) const;

    enum class VertexType
    {
        Convex,
        Collinear,
        Concave
    };

    VertexType getVertexType(int ix) const;
    bool isConvexOrCollinearVertex(int ix) const;
    bool isCollinearVertex(int ix) const;
    bool isConcaveVertex(int ix) const;
    bool isConvexVertex(int ix) const;
    bool isDegeneratedPolygon() const;

    bool isConvexPolygon() const;
    bool isConcavePolygon() const;

    // Check intersections
    bool isDiagonalIntersected(int start, int end) const;
    bool hasPolygonSelfIntersections() const;
    bool intersects(const Segment2D& s, bool includeEnds = true) const;

    // Point must be inside in polygon!
    float getRadiusOfInscribedCircleAtPoint(const GVector2D& pt) const;

    // Returns vector of polygons - First is main polygon, and others are remaining pieces
    // If polygon is convex - returns empty vector
    std::vector<Polygon2D> dividePolygonIntoConvex();

    float getArea() const;

    // Returns true if this polygon is CW oriented
    // If it returns false and assuming you constructed the polygon with a valid point format then the polygon is CCW oriented
    bool isCW() const;

    // Reverses the order of the polygon.
    // If it is CW it is turned into CCW and vice versa
    void reverseOrder();

    // Computes all intersections of input [ray] with bounds of this polygon
    // @returns [intersection distance from ray origin] -> [intersection point] + [vertex indices of intersected segment]
    QMap<float, std::tuple<GVector2D, std::array<int, 2>>> rayIntersections(const Segment2D& ray) const;

    // Computes the distance of the 2 given points on the perimeter of the polygon by traversing it in CW direction
    // @returns [actualDistance] + [vector of polygon points crossed]
    std::tuple<float, std::vector<GVector2D>> perimeterDistanceCW(const GVector2D& p1, const GVector2D& p2) const;

    // Computes the distance of the 2 given points on the perimeter of the polygon by traversing it in CCW direction
    // @returns [actualDistance] + [vector of polygon points crossed]
    std::tuple<float, std::vector<GVector2D>> perimeterDistanceCCW(const GVector2D& p1, const GVector2D& p2) const;

    std::optional<std::pair<int, int>> findPointOnIndexedEdge(const GVector2D& p, float maxOffset = 2.f) const;
    std::vector<double> getBCCoords(const GVector2D& p) const;
    GVector2D fromBCCoords(const std::vector<double>& coords) const;
    void collapseShortEdges(float minEdgeLength);
    void mergeColinearEdges(float angleThreshold);

    const auto& getPts() const { return pts; }
    auto getCPts() const { return asCircular(pts); }
    float getRadius() const;
    GVector2D getCenter() const;

    GVector2D getNormal(int index) const;

    // Debug helpers
    void debugPlot3D(const QVector4D& color = { 1,1,1,1 }, float height = 0.0f, bool debugColinear = false) const;
    void debugPlot(const QVector4D& color = {1,1,1,1}, float height = 0.0f, bool debugColinear = false) const;
    void debugPrint() const;

    BoundingBox getEnclosingBB() const;
    std::vector<Segment2D> getPtsAsSegments() const;

    auto begin() const
    {
        return pts.begin();
    }

    auto end() const
    {
        return pts.end();
    }

    const GVector2D& operator[](int i) const
    {
        return pts[i];
    }

    Polygon2D operator*(float scale) const;

private:
    void invalidateCache();
    void calculateSignedArea() const;

private:

    std::vector<GVector2D> pts;

    mutable float lazyRadius = -1;
    mutable GVector2D lazyCenter;
    mutable float clockwise = 0.f;

    FRIEND_OMNIBIN(Polygon2D)
};

std::vector<Polygon2D> mergePolygons(const std::vector<Polygon2D>& polys);

inline void omniSave(const Polygon2D& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.pts;
}

inline void omniLoad(Polygon2D& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.pts;
}

void debugPrintPoints(const std::vector<GVector2D>& pts);

// Utils class for polygons using QVector3D, but are meant to be 2D polygons with Z as second axis
class PolygonUtils
{
public:
    // Calculate bounding box for given polygon
    static BoundingBox calculateBB(const std::vector<QVector3D>& polygon);

    // Calculate area of given polygon
    static float calculateArea(const std::vector<QVector3D>& polygon);

    // Check if segment is intersecting polygon
    static bool intersects(const Segment2D& segment, const std::vector<QVector3D>& polygon, bool includeEnds = true);

    // Check if point is inside polygon (concave)
    static bool contains(const GVector2D& point, const std::vector<QVector3D>& polygon, const bool boundsContained = true);

    // Check if all points are inside polygon (concave)
    static bool containsAll(const std::vector<QVector3D>& points, const std::vector<QVector3D>& polygon, const bool boundsContained = true);

    // Check if any points are inside polygon (concave)
    static bool containsAny(const std::vector<QVector3D>& points, const std::vector<QVector3D>& polygon, const bool boundsContained = true);

    // Get type of vertex
    static Polygon2D::VertexType getVertexType(const std::vector<QVector3D> polygon, int idx);

    // Find path inside polygon
    static std::vector<QVector3D> findPathInsidePolygon(const GVector2D& startPoint, const GVector2D& endPoint, const std::vector<QVector3D>& polygon, const std::vector<std::vector<QVector3D>>& innerPolygons);

    // Check if point is in distance of polygon edges
    static bool isInDistanceOfEdges(const GVector2D& point, float range, const std::vector<QVector3D>& polygon);

    // check is polygon is clockwise
    static bool isCW(const std::vector<QVector3D>& polygon);

    // Calculates polygons which represents bounds made by grid squares
    static std::vector<std::vector<QVector3D>> calculatePolygonsFromGridSquares(const QSet<GPoint>& set);

    // Calculates grid squares which represents polygons perimeter and it's inside
    static QSet<GPoint> calculateGridSquaresOfPolygon(const std::vector<QVector3D>& polygon, bool onlyPerimeter = false);

    // Find covarage percentages between given polygon and other polygons, from 0.0f - 1.0f
    static std::vector<float> coverage(const std::vector<QVector3D>& polygon, const std::vector<std::vector<QVector3D>>& polygons, float reductionDist = 5000.0f);

    // Merge all given polygons
    static std::vector<std::vector<QVector3D>> mergePolygons(const std::vector<std::vector<QVector3D>>& polygons);

    // Return intersection of given polygon with different polygons
    static std::vector<std::vector<QVector3D>> intersectPolygons(const std::vector<QVector3D>& polygon, const std::vector<std::vector<QVector3D>>& polygons);

    static std::vector<std::vector<QVector3D>> intersectPolygons(const std::vector<std::vector<QVector3D>>& polygons1, const std::vector<std::vector<QVector3D>>& polygons2);

    // Cut given polygons by cutting polygons
    static std::vector<std::vector<QVector3D>> cutPolygons(const std::vector<std::vector<QVector3D>>& polygonsToCut, const std::vector<std::vector<QVector3D>>& cuttingPolygons, bool simplyfy = false);

    // Cut given polygon by cutting polygons
    static std::vector<std::vector<QVector3D>> cutPolygon(const std::vector<QVector3D>& polygonToCut, const std::vector<std::vector<QVector3D>>& cuttingPolygons, bool simplyfy = false);

    // Use clipper to cut polygons, assuming values increased by magnitude of 10^3
    static std::vector<std::vector<QVector3D>> cutPolygons(ClipperLib::Clipper* cutClipper, bool simplyfy);

    // Change size of polygon by given amount
    static std::vector<std::vector<QVector3D>> inflatePolygon(const std::vector<QVector3D>& polygon, const double amount);

    // Removes self-intersections from the supplied polygon. Polygons with non-contiguous duplicate vertices will be split into two polygons.
    static std::vector<std::vector<QVector3D>> simplifyPolygon(const std::vector<QVector3D>& polygon);
};
