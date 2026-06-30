#include "stdafx.h"
#include "Polygon.h"
#include "CircularVectorView.h"
#include "Omnigen.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Interpolation.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"

Polygon2D::Polygon2D(std::vector<GVector2D> inPts, const bool forceCW /* = false */)
    : pts(std::move(inPts))
{
    if (forceCW && !isCW())
        reverseOrder();
}

Polygon2D::Polygon2D(const std::vector<QVector3D>& inPts, const bool forceCW /* = false */)
    : pts(std::vector<GVector2D>(inPts.begin(), inPts.end()))
{
    if (forceCW && !isCW())
        reverseOrder();
}

Polygon2D::Polygon2D(Polygon2D&& other)
    : pts(std::move(other.pts))
    , lazyRadius(other.lazyRadius)
    , lazyCenter(other.lazyCenter)
    , clockwise(other.clockwise)
{
}

Polygon2D& Polygon2D::operator=(Polygon2D&& other)
{
    if (this == &other)
        return *this;

    pts = std::move(other.pts);
    lazyRadius = std::exchange(other.lazyRadius, -1.f);
    lazyCenter = std::exchange(other.lazyCenter, GVector2D(0.f, 0.f));
    clockwise = std::exchange(other.clockwise, 0.f);

    return *this;
}

void Polygon2D::setPoints(const std::vector<GVector2D>& points)
{
    pts = points;
    invalidateCache();
}

void Polygon2D::setPoints(std::vector<GVector2D>&& points)
{
    pts = std::move(points);
    invalidateCache();
}

void Polygon2D::invalidateCache()
{
    lazyRadius = -1.f;
    lazyCenter.x = 0.f;
    lazyCenter.z = 0.f;
    clockwise = 0.f;
}

Polygon2D Polygon2D::inflatePolygon(const Polygon2D& poly, const float amount)
{
    ClipperLib::ClipperOffset newClipper;

    std::vector<ClipperLib::IntPoint> polygonSubject;
    for (auto&& pt : poly)
        polygonSubject.emplace_back(pt.x, pt.z);
    newClipper.AddPath(polygonSubject, ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etClosedPolygon);

    ClipperLib::Paths enlargedPoly;
    newClipper.Execute(enlargedPoly, amount);

    std::vector<GVector2D> newPts;
    for (auto&& vec : enlargedPoly)
        for (auto&& pt : vec)
            newPts.emplace_back(pt.X, pt.Y);

    return Polygon2D(newPts);
}

Polygon2D Polygon2D::inflatePolygonByScale(const Polygon2D& poly, const float amount)
{
    const float localAmount = poly.getRadius() - (poly.getRadius() * amount);

    ClipperLib::ClipperOffset newClipper;

    std::vector<ClipperLib::IntPoint> polygonSubject;
    for (auto&& pt : poly)
        polygonSubject.emplace_back(pt.x, pt.z);
    newClipper.AddPath(polygonSubject, ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etClosedPolygon);

    ClipperLib::Paths enlargedPoly;
    newClipper.Execute(enlargedPoly, amount < 1 ? -localAmount : localAmount);

    std::vector<GVector2D> newPts;
    for (auto&& vec : enlargedPoly)
        for (auto&& pt : vec)
            newPts.emplace_back(pt.X, pt.Y);

    return Polygon2D(newPts);
}

std::vector<Polygon2D> Polygon2D::simplifyPolygon(const Polygon2D& poly)
{
    ClipperLib::Path polygonSubject;
    for (auto&& pt : poly)
        polygonSubject.emplace_back(pt.x, pt.z);

    ClipperLib::Paths simplifiedPolys;
    ClipperLib::SimplifyPolygon(polygonSubject, simplifiedPolys, ClipperLib::pftNonZero);

    std::vector<Polygon2D> newPolys;

    for (auto&& path : simplifiedPolys)
    {
        std::vector<GVector2D> newPts;
        for (auto&& pt : path)
            newPts.emplace_back(pt.X, pt.Y);

        newPolys << Polygon2D(newPts);
    }

    return newPolys;
}

Polygon2D Polygon2D::cleanPolygon(const Polygon2D& poly)
{
    ClipperLib::Path polygonSubject;
    for (auto&& pt : poly)
        polygonSubject.emplace_back(pt.x, pt.z);

   ClipperLib::CleanPolygon(polygonSubject);

   std::vector<GVector2D> newPts;
   for (auto&& pt : polygonSubject)
       newPts.emplace_back(pt.X, pt.Y);

   return { newPts };
}

Polygon2D Polygon2D::findConvexHull(const std::vector<GVector2D>& inPts)
{
    auto isLeft = [](const GVector2D& p0, const GVector2D& p1, const GVector2D& p2)
    {
        return (p1.x - p0.x) * (p2.z - p0.z) - (p2.x - p0.x) * (p1.z - p0.z);
    };

    std::vector<GVector2D> P = inPts;
    std::vector<GVector2D> H;

    // Sort P by x and y
    for (int i = 0; i < P.size(); i++) {
        for (int j = i + 1; j < P.size(); j++) {
            if (P[j].x < P[i].x || (P[j].x == P[i].x && P[j].z < P[i].z)) {
                GVector2D tmp = P[i];
                P[i] = P[j];
                P[j] = tmp;
            }
        }
    }

    // the output array H[] will be used as the stack
    int i;                 // array scan index

    // Get the indices of points with min x-coord and min|max y-coord
    int minmin = 0, minmax;
    double xmin = P[0].x;
    for (i = 1; i < P.size(); i++)
        if (P[i].x != xmin) break;
    minmax = i - 1;
    if (minmax == P.size() - 1) {       // degenerate case: all x-coords == xmin
        H.push_back(P[minmin]);
        if (P[minmax].z != P[minmin].z) // a  nontrivial segment
            H.push_back(P[minmax]);
        H.push_back(P[minmin]);            // add polygon endpoint
        return H;
    }

    // Get the indices of points with max x-coord and min|max y-coord
    int maxmin, maxmax = P.size() - 1;
    double xmax = P.back().x;
    for (i = P.size() - 2; i >= 0; i--)
        if (P[i].x != xmax) break;
    maxmin = i + 1;

    // Compute the lower hull on the stack H
    H.push_back(P[minmin]);      // push  minmin point onto stack
    i = minmax;
    while (++i <= maxmin)
    {
        // the lower line joins P[minmin]  with P[maxmin]
        if (isLeft(P[minmin], P[maxmin], P[i]) >= 0 && i < maxmin)
            continue;           // ignore P[i] above or on the lower line

        while (H.size() > 1)         // there are at least 2 points on the stack
        {
            // test if  P[i] is left of the line at the stack top
            if (isLeft(H[H.size() - 2], H.back(), P[i]) > 0)
                break;         // P[i] is a new hull  vertex
            H.pop_back();         // pop top point off  stack
        }
        H.push_back(P[i]);        // push P[i] onto stack
    }

    // Next, compute the upper hull on the stack H above  the bottom hull
    if (maxmax != maxmin)      // if  distinct xmax points
        H.push_back(P[maxmax]);  // push maxmax point onto stack
    int bot = H.size();                  // the bottom point of the upper hull stack
    i = maxmin;
    while (--i >= minmax)
    {
        // the upper line joins P[maxmax]  with P[minmax]
        if (isLeft(P[maxmax], P[minmax], P[i]) >= 0 && i > minmax)
            continue;           // ignore P[i] below or on the upper line

        while (H.size() > bot)     // at least 2 points on the upper stack
        {
            // test if  P[i] is left of the line at the stack top
            if (isLeft(H[H.size() - 2], H.back(), P[i]) > 0)
                break;         // P[i] is a new hull  vertex
            H.pop_back();         // pop top point off stack
        }
        H.push_back(P[i]);        // push P[i] onto stack
    }
    if (minmax != minmin)
        H.push_back(P[minmin]);  // push  joining endpoint onto stack

    return { H };
}

std::vector<Polygon2D> Polygon2D::boolOp(const Polygon2D& A, const Polygon2D& B, const EBoolOp op)
{
    std::vector<ClipperLib::IntPoint> APts;
    for (auto&& pt : A)
        APts.emplace_back(pt.x, pt.z);

    std::vector<ClipperLib::IntPoint> BPts;
    for (auto&& pt : B)
    {
        BPts.emplace_back(pt.x, pt.z);
    }

    ClipperLib::Clipper newClipper{};
    newClipper.AddPath(APts, ClipperLib::PolyType::ptSubject, true);
    newClipper.AddPath(BPts, ClipperLib::PolyType::ptClip, true);

    ClipperLib::Paths intersectionResult;

    std::vector<Polygon2D> polysToReturn;

    ClipperLib::ClipType opToUse{};
    switch (op)
    {
        using enum EBoolOp;

        case Intersection:
            opToUse = ClipperLib::ClipType::ctIntersection;
            break;
        case Difference:
            opToUse = ClipperLib::ClipType::ctDifference;
            break;
        case XOR:
            opToUse = ClipperLib::ClipType::ctXor;
            break;
        case Union:
            opToUse = ClipperLib::ClipType::ctUnion;
            break;
    }

    if (!newClipper.Execute(opToUse, intersectionResult, ClipperLib::pftNonZero, ClipperLib::pftNonZero))
        return {};

    std::vector<Polygon2D> polygons;

    for (auto&& path : intersectionResult)
    {
        std::vector<GVector2D> newPts;

        for (auto&& pt : path)
            newPts.emplace_back(pt.X, pt.Y);

        polysToReturn << Polygon2D(newPts);
    }

    return polysToReturn;
}

bool Polygon2D::areAdjacentOrOverlapping(const Polygon2D& A, const Polygon2D& B, const bool inflate)
{
    if (inflate)
    {
        const auto polyA = inflatePolygonByScale(A, 1.05f);
        const auto polyB = inflatePolygonByScale(B, 1.05f);

        return !boolOp(polyA, polyB, EBoolOp::Intersection).empty();
    }

    return !boolOp(A, B, EBoolOp::Intersection).empty();
}

auto Polygon2D::computeRandomPath(const GVector2D& p1, const GVector2D& p2, int numSegments, std::optional<InterpolationParams> params) const
    -> std::tuple<std::vector<GVector2D>, InterpolationParams>
{
    auto coords1 = getBCCoords(p1);
    auto coords2 = getBCCoords(p2);

    static auto allowed01methods = std::vector{ EInterpolation01::Linear, EInterpolation01::Smoothstep, EInterpolation01::InverseSmoothstep, EInterpolation01::Power, EInterpolation01::InversePower };
    static auto allowed010methods = std::vector{ EInterpolation010::Zero, EInterpolation010::Sine, EInterpolation010::DoublePeakSine };
    static std::uniform_int_distribution<int> paramGen(0, 2);
    static std::uniform_int_distribution<int> methodGen01(0, int(allowed01methods.size()) - 1);
    static std::uniform_int_distribution<int> methodGen010(0, int(allowed010methods.size()) - 1);

    if (!params)
    {
        InterpolationParams coordParams(coords1.size());
        for (int i = 0; i < coords1.size(); ++i)
        {
            if (coords1[i] == 0.0f && coords2[i] == 0.0f)
            {
                // 010 type
                coordParams[i] = QSharedPointer<Interpolation::TechniqueNode>::create(
                    EInterpolation010Constexpr::UseIn<Interpolation::EAC::MakeTechnique010>(
                        EInterpolation010(methodGen010(Generation::gRandomEngine)),
                        paramGen(Generation::gRandomEngine),
                        paramGen(Generation::gRandomEngine)
                    ));
            }
            else
            {
                coordParams[i] = QSharedPointer<Interpolation::TechniqueNode>::create(
                    EInterpolation01Constexpr::UseIn<Interpolation::EAC::MakeTechnique01>(
                        EInterpolation01(methodGen01(Generation::gRandomEngine)),
                        paramGen(Generation::gRandomEngine),
                        paramGen(Generation::gRandomEngine)
                        ));
            }
        }

        params = std::move(coordParams);
    }

    // Path gen
    std::vector<GVector2D> path(numSegments + 1);

    path.front() = p1;
    path.back() = p2;

    for (int step = 1; step < numSegments; ++step)
    {
        // Due to different interpolation methods, weights will need to be renormalized
        std::vector<double> usedWeights(coords1.size());
        double weightSum = 0.0f;

        double t = double(step) / double(numSegments);
        for (int i = 0; i < coords1.size(); ++i)
        {
            const double tt = params->at(i)->interpolate(t);
            if (coords1[i] == 0.0f && coords2[i] == 0.0f) // 010 type
                usedWeights[i] = std::lerp(0.f, 1.f, (float)tt);
            else
                usedWeights[i] = std::lerp(coords1[i], coords2[i], tt);

            weightSum += usedWeights[i];
        }

        for (auto&& uw : usedWeights)
            uw /= weightSum;

        // Final pos calculation
        path[step] = fromBCCoords(usedWeights);
    }

    return { path, *params };
}

bool Polygon2D::contains(const GVector2D& p, bool includeEdges) const
{
    if (p.isInsidePolygon(getPts()))
        return true;

    if (!includeEdges)
        return false;

    for (int i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
    {
        const GVector2D& U0 = pts[i];
        const GVector2D& U1 = pts[j];

        if (vEq(p, U0)) // point is a corner
            return true;

        const float lhs = (p.z - U0.z) * (U1.x - U0.x);
        const float rhs = (p.x - U0.x) * (U1.z - U0.z);

        if (isZero(lhs - rhs)) // point is on boundary
            return true;
    }

    return false;
}

bool Polygon2D::contains(const Segment2D& s) const
{
    if (contains(s.first) || contains(s.second))
        return true;

    auto cPts = asCircular(pts);

    for (int i = 0; i < pts.size(); ++i)
    {
        int i2 = cPts.findIdx(i, 1);

        if (s.intersects({ pts[i], pts[i2] }, true))
            return true;
    }

    return false;
}

bool Polygon2D::containsAll(const std::vector<GVector2D>& pts) const
{
    return std::all_of(pts.begin(), pts.end(), [this](const auto& p) {return contains(p); });
}

bool Polygon2D::containsAny(const std::vector<GVector2D>& pts) const
{
    return std::any_of(pts.begin(), pts.end(), [this](const auto& p) {return contains(p); });
}

std::vector<std::tuple<int, int, GVector2D>> Polygon2D::getClosestEdges(const GVector2D& p) const
{
    auto cPts = asCircular(pts);

    std::vector<std::tuple<int, int, GVector2D>> result = {};
    float minD = std::numeric_limits<float>::max();

    for (int i = 0; i < pts.size(); ++i)
    {
        int i2 = cPts.findIdx(i, 1);
        auto [v1, d] = distance({ pts[i], pts[i2] }, p, true);
        if (d < minD)
        {
            minD = d;
            result = { { i, i2, v1 } };
        }
        else if (d == minD)
        {
            result << std::tuple{ i, i2, v1 };
        }
    }

    return result;
}

bool Polygon2D::addPoint(int a, int b, const GVector2D& p)
{
    auto cPts = asCircular(pts);

    int dCW = cPts.distCW(a, b);
    int dCCW = cPts.distCCW(a, b);
    int d = std::min(dCW, dCCW);
    if (d != 1)
        return false;

    if (d == dCW)
        pts.insert(pts.begin() + b, p);
    else
        pts.insert(pts.begin() + a, p);

    invalidateCache();

    return true;
}

void Polygon2D::addPoint(int ix, const GVector2D& p, const bool forceCW /* = false */) {
    pts.insert(pts.begin() + ix, p);

    invalidateCache();
}

void Polygon2D::removePoint(int ix) {
    pts.erase(pts.begin() + ix);

    invalidateCache();
}

void Polygon2D::setPoint(int ix, const GVector2D& p) {
    pts[ix] = p;
    invalidateCache();
}

GVector2D Polygon2D::getRandomPointInsidePolygon() const
{
    std::vector<double> bcc(pts.size());
    std::uniform_real_distribution<> distr(0.0001, 0.9999);

    double sum = 0.;
    for (double& w: bcc)
    {
        w = distr(Generation::gRandomEngine);
        sum += w;
    }

    for (double& w: bcc)
        w /= sum;

    return fromBCCoords(bcc);
}

bool Polygon2D::containsConcave(const GVector2D& p, bool boundsContained /* = true */) const
{
    std::vector<ClipperLib::IntPoint> polyPoints;
    for (auto&& pt : pts)
        polyPoints.emplace_back(pt.x, pt.z);

    ClipperLib::IntPoint point = ClipperLib::IntPoint(p.x, p.z);

    const int result = ClipperLib::PointInPolygon(point, polyPoints);

    if (result == 0)
        return false;

    if (result == 1)
        return true;

    if (result == -1)
        return boundsContained;

    return false;
}

Polygon2D::VertexType Polygon2D::getVertexType(int ix) const
{
    const bool isClockwise = isCW();
    const auto cPts = asCircular(pts);
    const GVector2D& currPt = cPts[ix];
    const GVector2D& prevPt = cPts.getPrev(ix);
    const GVector2D& nextPt = cPts.getNext(ix);

    const float crossProduct = GVector2D::crossProduct(prevPt, currPt, nextPt);

    // If vectors coordinates are too bid - floating point error has tendention to grow and default epsilon offset isn't enough
    // factor is asin(alpha) = 0.01, therefore alpha approximately <= 0.6°
    const float zeroThreshold = distanceSquared2D(prevPt, currPt) * 0.01f;

    if (fabs(crossProduct) < zeroThreshold)
        return VertexType::Collinear;

    if ((isClockwise && crossProduct < 0.f) || (!isClockwise && crossProduct > 0.f))
        return VertexType::Concave;

    return VertexType::Convex;
}

bool Polygon2D::isConvexOrCollinearVertex(int ix) const
{
    const VertexType type = getVertexType(ix);
    return type == VertexType::Convex || type == VertexType::Collinear;
}

bool Polygon2D::isCollinearVertex(int ix) const
{
    return getVertexType(ix) == VertexType::Collinear;
}

bool Polygon2D::isConcaveVertex(int ix) const
{
    return getVertexType(ix) == VertexType::Concave;
}

bool Polygon2D::isConvexVertex(int ix) const
{
    return getVertexType(ix) == VertexType::Convex;
}

void Polygon2D::calculateSignedArea() const
{
    for (int i = 0; i < pts.size(); ++i)
    {
        const int next = i == pts.size() - 1 ? 0 : i + 1;
        clockwise += (pts[next].x - pts[i].x) * (pts[next].z + pts[i].z);
    }
}

bool Polygon2D::isDegeneratedPolygon() const
{
    if (pts.size() < 3)
        return true;

    if (!isZero(clockwise))
        return false;

    calculateSignedArea();
    return isZero(clockwise);
}

bool Polygon2D::isConcavePolygon() const
{
    if (pts.size() == 3)
        return false;

    for (int i = 0; i < pts.size(); ++i)
        if (!isConvexOrCollinearVertex(i))
            return true;
    return false;
}

bool Polygon2D::isConvexPolygon() const
{
    for (int i = 0; i < pts.size(); ++i)
        if (!isConvexVertex(i))
            return false;
    return true;
}

bool Polygon2D::isDiagonalIntersected(int start, int end) const
{
    if (abs(start - end) <= 1)
        return true;

    const auto cpts = asCircular(pts);
    const Segment2D diagonalSegment = {cpts[start], cpts[end]};

    for (int i = 0; i < cpts.getSize(); ++i)
    {
        const int currStart = i;
        const int currEnd = cpts.findIdx(i, 1);
        if (currStart == start || currStart == end || currEnd == start || currEnd == end)
            continue;

        const Segment2D segToCheck = {cpts[currStart], cpts[currEnd]};
        if (diagonalSegment.intersects(segToCheck, true))
            return true;
    }

    return false;
}

bool Polygon2D::hasPolygonSelfIntersections() const
{
    const auto cpts = asCircular(pts);

    std::vector<Segment2D> segments;
    segments.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        segments.emplace_back(cpts[i], cpts.getNext(i));

    const auto csegments = asCircular(segments);

    for (int i = 0; i < csegments.getSize(); ++i)
    {
        const Segment2D& currSegment = csegments[i];
        for (int j = i + 2; j < csegments.getSize() - 1; ++j)
        {
            const Segment2D& segToCheck = csegments[j];
            if (currSegment.intersects(segToCheck, true))
                return true;
        }
    }

    return false;
}

bool Polygon2D::intersects(const Segment2D& s, bool includeEnds) const
{
    const auto cPts = asCircular(pts);

    for (int i = 0; i < pts.size(); ++i)
    {
        const int i2 = cPts.findIdx(i, 1);
        if (s.intersects({ pts[i], pts[i2] }, includeEnds))
            return true;
    }
    return false;
}

float Polygon2D::getRadiusOfInscribedCircleAtPoint(const GVector2D& pt) const
{
    float minDistanceToEdge = std::numeric_limits<float>::max();
    const auto cPts = asCircular(pts);

    for (int i = 0; i < pts.size(); ++i)
    {
        const int i2 = cPts.findIdx(i, 1);
        const Segment2D seg = { pts[i], pts[i2] };
        const float dist = seg.dist(pt);
        if (dist < minDistanceToEdge)
            minDistanceToEdge = dist;
    }

    return minDistanceToEdge;
}

std::vector<Polygon2D> Polygon2D::dividePolygonIntoConvex()
{
    std::vector<short> needRemove(pts.size());
    const auto needRemoveCircular = asCircular(needRemove);
    std::fill(needRemove.begin(), needRemove.end(), 0);

    bool isConvex = true;
    for (int i = 0; i < pts.size(); ++i)
    {
        if (!isConvexOrCollinearVertex(i))
        {
            isConvex = false;
            needRemove[needRemoveCircular.findIdx(i, -1)] = 1;
            needRemove[needRemoveCircular.findIdx(i,  1)] = 1;
        }
    }

    if (isConvex)
        return {};

    std::vector<Polygon2D> result;
    result.reserve(pts.size());
    result.resize(1); // reserved for main polygon

    std::vector<GVector2D> main;
    std::vector<GVector2D> remaining;
    main.reserve(pts.size());
    remaining.reserve(pts.size());

    int startIndex = -1;
    bool needNewRemainingPolygon = true;

    for (int i = 0; i < pts.size(); ++i)
    {
        if (startIndex == -1)
        {
            if (!needRemove[i])
                startIndex = i;
            else
                continue;
        }

        if (needRemove[i])
        {
            if (needNewRemainingPolygon)
            {
                remaining.reserve(pts.size());
                remaining << pts[i - 1];
                needNewRemainingPolygon = false;
            }
            remaining << pts[i];
        }
        else
        {
            main << pts[i];
            if (!remaining.empty())
            {
                remaining << pts[i];
                result.emplace_back(std::move(remaining));
                remaining.clear();
                needNewRemainingPolygon = true;
            }
        }
    }

    if (remaining.empty())
        remaining << pts.back();

    for (int i = 0; i < startIndex; ++i)
        remaining << pts[i];
    remaining << pts[startIndex];
    if (remaining.size() > 2)
        result.emplace_back(std::move(remaining));
    result.front().setPoints(std::move(main));

    std::vector<Polygon2D> tempResult;
    Polygon2D& currentPolygon = result.front();

    while (true)
    {
        if (!currentPolygon.isConcavePolygon())
            break;

        tempResult = currentPolygon.dividePolygonIntoConvex();
        result.reserve(result.size() + tempResult.size());
        result.insert(result.end(), ++tempResult.begin(), tempResult.end());
        currentPolygon = std::move(tempResult.front());

        if (currentPolygon.getPts().size() < 3)
        {
            currentPolygon = result.back();
            result.resize(result.size() - 1);
            break;
        }
    }
    result.front() = std::move(currentPolygon);


    result.shrink_to_fit();

    return result;
}

float Polygon2D::getArea() const
{
    std::vector<ClipperLib::IntPoint> polyPoints;
    for (auto&& pt : pts)
        polyPoints.emplace_back(pt.x, pt.z);

    return std::abs(ClipperLib::Area(polyPoints));
}

bool Polygon2D::isCW() const
{
    if (clockwise != 0.f)
        return clockwise > 0.f;

    calculateSignedArea();
    Q_ASSERT(!std::is_eq(fCmp(clockwise, 0.f)));
 #if !NDEBUG
    if (isZero(clockwise))
    {
        debugPlot(Colors::robinEggBlue, 1000.f);
        spawn<DLineMarker>((QVector3D)pts.front(), 1000.f, Colors::robinEggBlue);
    }
#endif

    return clockwise > 0.f;
}

void Polygon2D::reverseOrder()
{
    if (pts.empty())
        return;

    if (clockwise != 0.f)
    {
        clockwise *= -1.f;
    }

    std::ranges::reverse(pts);
}

QMap<float, std::tuple<GVector2D, std::array<int, 2>>> Polygon2D::rayIntersections(const Segment2D& ray) const
{
    QMap<float, std::tuple<GVector2D, std::array<int, 2>>> results;
    auto cPts = getCPts();

    for (int i = 0; i < cPts.getSize(); ++i)
    {
        int i2 = cPts.findIdx(i, 1);

        auto [v1, v2, d] = distance(std::array<GVector2D, 2>{ ray.first, ray.second }, { cPts[i], cPts[i2] });
        if (d < 0.1f)
        {
            float distFromSrc = distance(ray.first, GVector2D(v2));
            results[distFromSrc] = { v2, {i, i2} };
        }
    }

    return std::move(results);
}

std::tuple<float, std::vector<GVector2D>> Polygon2D::perimeterDistanceCW(const GVector2D& p1, const GVector2D& p2) const
{
    if (!isCW())
    {
        Q_ASSERT_X(false, "Polygon2D::perimeterDistanceCW", "The polygon is not CW ordered!");
        return std::tuple<float, std::vector<GVector2D>>();
    }

    std::vector<GVector2D> crossedPoints;
    float totalDistance = 0.f;

    auto cPts = getCPts();
    const auto point1Edge = findPointOnIndexedEdge(p1);
    const auto point2Edge = findPointOnIndexedEdge(p2);

    if (!point1Edge || !point2Edge)
    {
        Q_ASSERT_X(false, "Polygon2D::perimeterDistanceCW", "One or more of the given points is not on the polygon perimeter!");
        return std::tuple<float, std::vector<GVector2D>>();
    }

    int a, b;

    if (point1Edge->second == 0)
    {
        a = 0;
    }
    else
    {
        a = point1Edge->first > point1Edge->second ? point1Edge->first : point1Edge->second;
    }

    if (point2Edge->second == 0)
    {
        b = point2Edge->first;
    }
    else
    {
        b = point2Edge->first < point2Edge->second ? point2Edge->first : point2Edge->second;
    }

    totalDistance += distance(p1, cPts[a]);

    if (a != b)
    {
        cPts.forRangeCW(a, b, [&](int ptIdx) {
            crossedPoints.push_back(cPts[ptIdx]);
        });

        if (crossedPoints.size() >= 2)
        {
            for (int j = 1; j < crossedPoints.size(); j++)
            {
                totalDistance += distance(crossedPoints[j - 1], crossedPoints[j]);
            }
        }
    }
    else
    {
        crossedPoints.push_back(cPts[a]);
    }

    totalDistance += distance(cPts[b], p2);

    return std::make_tuple(totalDistance, crossedPoints);
}

std::tuple<float, std::vector<GVector2D>> Polygon2D::perimeterDistanceCCW(const GVector2D& p1, const GVector2D& p2) const
{
    if (!isCW())
    {
        Q_ASSERT_X(false, "Polygon2D::perimeterDistanceCCW", "The polygon is not CW ordered!");
        return std::tuple<float, std::vector<GVector2D>>();
    }

    std::vector<GVector2D> crossedPoints;
    float totalDistance = 0.f;

    auto cPts = getCPts();
    const auto point1Edge = findPointOnIndexedEdge(p1);
    const auto point2Edge = findPointOnIndexedEdge(p2);

    if (!point1Edge || !point2Edge)
    {
        Q_ASSERT_X(false, "Polygon2D::perimeterDistanceCCW", "One or more of the given points is not on the polygon perimeter!");
        return std::tuple<float, std::vector<GVector2D>>();
    }

    int a, b;

    if (point1Edge->first == 0)
    {
        a = point1Edge->second;
    }
    else
    {
        a = point1Edge->first < point1Edge->second ? point1Edge->first : point1Edge->second;
    }

    if (point2Edge->first == 0)
    {
        b = 0;
    }
    else
    {
        b = point2Edge->first > point2Edge->second ? point2Edge->first : point2Edge->second;
    }

    totalDistance += distance(p1, cPts[a]);

    if (a != b)
    {
        cPts.forRangeCCW(a, b, [&](int ptIdx) {
            crossedPoints.push_back(cPts[ptIdx]);
        });

        if (crossedPoints.size() >= 2)
        {
            for (int j = 1; j < crossedPoints.size(); j++)
            {
                totalDistance += distance(crossedPoints[j - 1], crossedPoints[j]);
            }
        }
    }
    else
    {
        crossedPoints.push_back(cPts[a]);
    }

    totalDistance += distance(cPts[b], p2);

    return std::make_tuple(totalDistance, crossedPoints);
}

std::optional<std::pair<int, int>> Polygon2D::findPointOnIndexedEdge(const GVector2D& p, float maxOffset /*= 2.f*/) const
{
    const auto cPts = getCPts();
    for (int i = 0; i < cPts.getSize(); i++)
    {
        int i2 = cPts.findIdx(i, 1);
        const Segment2D testSegment = Segment2D(cPts[i], cPts[i2]);
        auto dist = std::get<float>(distance(testSegment, p));
        if (std::get<float>(distance(testSegment, p)) < maxOffset)
        {
            return std::make_pair(i, i2);
        }
    }

    return {};
}

float Polygon2D::getRadius() const
{
    static std::mutex guard;
    std::scoped_lock lock(guard);

    if (lazyRadius == -1)
    {
        for(auto&& p : pts)
            if (float d = p.dist(getCenter()); d > lazyRadius)
                lazyRadius = d;
    }

    return lazyRadius;
}

GVector2D Polygon2D::getCenter() const
{
    static std::mutex guard;
    std::scoped_lock lock(guard);

    if (lazyCenter.isNull())
    {
        for (auto&& p : pts)
            lazyCenter = lazyCenter + p;

        lazyCenter = lazyCenter / float(pts.size());
    }

    return lazyCenter;
}

GVector2D Polygon2D::getNormal(int index) const
{
    const bool isClockwise = isCW();
    const auto cPoints = getCPts();

    const GVector2D pt = cPoints[index];
    const GVector2D prev = cPoints.getPrev(index);
    const GVector2D next = cPoints.getNext(index);

    const float crossProduct = GVector2D::crossProduct(prev, pt, next);
    const GVector2D bisectVec = (fCmp(crossProduct, 0.f) == std::strong_ordering::equal) 
        ? (next - pt).rotatedLeft90()
        : (next - pt).normalized() + (prev - pt).normalized();
    const GVector2D newBisectVec = bisectVec.normalized() * ((!isClockwise && crossProduct <= 0.f) || (isClockwise && crossProduct > 0.f) ? -1.f : 1.f);
    return newBisectVec;
}

void Polygon2D::debugPlot3D(const QVector4D& color, float height, bool debugColinear) const
{
    std::vector<QVector3D> ptsWithHeight;
    for (auto&& pt : pts)
        ptsWithHeight << Generation::Utils::castPointTo3D(pt);

    if (!debugColinear)
    {
        Generation::Data::get()->createMarker<DLineMarker>(ptsWithHeight, color, true, height);
    }
    else
    {
        static std::uniform_real_distribution dist(0.0f, 50.0f);

        std::vector<QVector3D> pts2 = { ptsWithHeight.begin(), ptsWithHeight.end() };
        for (auto&& p : pts2)
            p.setY(p.y() + dist(Generation::gRandomEngine));

        Generation::Data::get()->createMarker<DLineMarker>(pts2, color, true, height);
    }
}

void Polygon2D::debugPlot(const QVector4D& color, float height, bool debugColinear) const
{
    if (pts.empty())
        return;

    if (!debugColinear)
    {
        Generation::Data::get()->createMarker<DLineMarker>(pts, color, true, height);
    }
    else
    {
        static std::uniform_real_distribution dist(0.0f, 50.0f);

        std::vector<QVector3D> pts2 = { pts.begin(), pts.end() };
        for (auto&& p : pts2)
            p.setY(dist(Generation::gRandomEngine));

        Generation::Data::get()->createMarker<DLineMarker>(pts2, color, true, height);
    }
}

BoundingBox Polygon2D::getEnclosingBB() const
{
    float xMin = std::numeric_limits<float>::max();
    float xMax = -1.f;
    float zMin = std::numeric_limits<float>::max();
    float zMax = -1.f;

    for (auto&& p : pts)
    {
        if (p.x < xMin)
            xMin = p.x;
        if (p.x > xMax)
            xMax = p.x;
        if (p.z < zMin)
            zMin = p.z;
        if (p.z > zMax)
            zMax = p.z;
    }

    return BoundingBox(QVector3D(xMin, 0, zMin), QVector3D(xMax - xMin, 0, zMax - zMin));
}

std::vector<Segment2D> Polygon2D::getPtsAsSegments() const
{
    std::vector<Segment2D> segs;

    const auto cPts = getCPts();
    for (auto i = 0; i < pts.size(); i++)
    {
        auto i2 = cPts.findIdx(i, 1);
        segs << Segment2D(pts[i], pts[i2]);
    }

    return segs;
}

Polygon2D Polygon2D::operator*(float scale) const
{
    std::vector<GVector2D> newPoints;
    newPoints.reserve(pts.size());

    auto&& c = getCenter();

    for (auto&& p : pts)
        newPoints << c + (p - c) * scale;

    return Polygon2D(newPoints);
}

std::vector<double> Polygon2D::getBCCoords(const GVector2D& p) const
{
    auto cPts = asCircular(pts);

    std::vector<double> weights(cPts.getSize());
    double weightSum = 0.0f;

    // Point is vertex?
    for (int i = 0; i < cPts.getSize(); ++i)
        if (vEq(p, cPts[i]))
        {
            weights[i] = 1.0f;
            return weights;
        }

    // Point lies on an edge?
    for (int i = 0; i < cPts.getSize(); ++i)
    {
        int i2 = cPts.findIdx(i, 1);

        if (std::get<float>(distance({ cPts[i], cPts[i2] }, p)) > 1.0f)
            continue;

        double p1 = distance(cPts[i], p);
        double p2 = distance(cPts[i2], p);
        double dist = distance(cPts[i], cPts[i2]);
        weightSum = p1 + p2;

        // The lower the distance the greater the weight
        weights[i] = p2 / weightSum;
        weights[i2] = p1 / weightSum;

        return weights;
    }

    static auto cot = [](auto&& A, auto&& B, auto&& C) -> double
    {
        auto BA = (A - B).normalized();
        auto BC = (C - B).normalized();
        return std::max(GVector2D::dotProduct(BC, BA), 0.0f);
    };

    for (int i = 0; i < cPts.getSize(); ++i)
    {
        int _i = cPts.findIdx(i, -1);
        int i_ = cPts.findIdx(i, 1);

        weights[i] += cot(p, cPts[i], cPts[_i]);
        weights[i] += cot(p, cPts[i], cPts[i_]);

        double d = distanceSquared(p, cPts[i]);
        Q_ASSERT(d > 0);

        weights[i] /= d;

        weightSum += weights[i];
    }

    double invWeightSum = 1.0f / weightSum;
    for (auto&& weight : weights)
    {
        weight *= invWeightSum;
        Q_ASSERT(weight >= 0);
    }

    return weights;
}

GVector2D Polygon2D::fromBCCoords(const std::vector<double>& coords) const
{
    GVector2D result;

    for (int i = 0; i < coords.size(); ++i)
        result = result + pts[i] * coords[i];

    return result;
}

void Polygon2D::collapseShortEdges(float minEdgeLength)
{
    auto cPts = getCPts();
    while (true)
    {
        bool merged = false;

        //We don't want to end up with degenerated polygons, that's why we need at least 4 verts
        for (int i = 0; i < int(cPts.getSize()) - 3 ; ++i)
        {
            int i2 = cPts.findIdx(i, 1);
            if (distance(cPts[i], cPts[i2]) >= minEdgeLength)
                continue;

            // Merge
            auto mid = std::lerp(cPts[i], cPts[i2], 0.5);

            removePoint(i);
            if (i2 == 0)
                pts[i2] = mid;
            else
                pts[i] = mid;
            
            merged = true;
            break;
        }

        if (!merged)
            break;
    }
}

void Polygon2D::mergeColinearEdges(float angleThreshold)
{
    auto&& cPts = getCPts();

    while (true)
    {
        bool merged = false;

        for (int i = 0; i < cPts.getSize(); ++i)
        {
            int prev = cPts.findIdx(i, -1);
            int next = cPts.findIdx(i, 1);

            if (angle180((cPts[next] - cPts[i]).normalized(), (cPts[i] - cPts[prev]).normalized()) < angleThreshold)
            {
                removePoint(i);
                merged = true;
                break;
            }
        }

        if (!merged)
            break;
    }
}

std::vector<Polygon2D> mergePolygons(const std::vector<Polygon2D>& polys)
{
    ClipperLib::Clipper newClipper{};

    for (auto&& poly : polys)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : poly)
            APts.emplace_back(pt.x, pt.z);

        newClipper.AddPath(APts, ClipperLib::PolyType::ptClip, true);
    }

    ClipperLib::Paths mergedPolys;
    newClipper.Execute(ClipperLib::ClipType::ctUnion, mergedPolys, ClipperLib::PolyFillType::pftNonZero);

    std::vector<Polygon2D> mergedPolygons;

    for (auto&& poly : mergedPolys)
    {
        std::vector<GVector2D> newPts;

        for (auto&& pt : poly)
            newPts.emplace_back(pt.X, pt.Y);

        mergedPolygons << Polygon2D(newPts);
    }

    return mergedPolygons;
}

void Polygon2D::debugPrint() const
{
    debugPrintPoints(pts);
}

void debugPrintPoints(const std::vector<GVector2D>& pts)
{
#if !NDEBUG
    QString result;
    for (const auto& pt: pts)
        result.append(QString("(%1, %2)%3 ").arg(pt.x).arg(pt.z).arg(pt == pts.back() ? "" : ","));
    OmniLog() <<= result;
#endif
}

BoundingBox PolygonUtils::calculateBB(const std::vector<QVector3D>& polygon)
{
    float xMin = std::numeric_limits<float>::max();
    float xMax = -1.f;
    float zMin = std::numeric_limits<float>::max();
    float zMax = -1.f;

    for (auto&& p : polygon)
    {
        if (p.x() < xMin)
            xMin = p.x();
        if (p.x() > xMax)
            xMax = p.x();
        if (p.z() < zMin)
            zMin = p.z();
        if (p.z() > zMax)
            zMax = p.z();
    }

    return BoundingBox(QVector3D(xMin, 0, zMin), QVector3D(xMax - xMin, 0, zMax - zMin));
}

float PolygonUtils::calculateArea(const std::vector<QVector3D>& polygon)
{
    std::vector<ClipperLib::IntPoint> polyPoints;
    for (auto&& pt : polygon)
        polyPoints.emplace_back(pt.x(), pt.z());

    return std::abs(ClipperLib::Area(polyPoints));
}

bool PolygonUtils::intersects(const Segment2D& segment, const std::vector<QVector3D>& polygon, bool includeEnds /*= true*/)
{
    const auto cPts = asCircular(polygon);

    for (int i = 0; i < polygon.size(); ++i)
    {
        const int i2 = cPts.findIdx(i, 1);
        if (segment.intersects({ polygon[i], polygon[i2] }, includeEnds))
            return true;
    }

    return false;
}

bool PolygonUtils::contains(const GVector2D& point, const std::vector<QVector3D>& polygon, const bool boundsContained /*= true*/)
{
    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    std::vector<ClipperLib::IntPoint> polyPoints;
    for (auto&& pt : polygon)
        polyPoints.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

    const int result = ClipperLib::PointInPolygon(ClipperLib::IntPoint((double)(point.x) * 1000., (double)(point.z) * 1000.), polyPoints);

    if (result == 0)
        return false;

    if (result == 1)
        return true;

    if (result == -1)
        return boundsContained;

    return false;
}

bool PolygonUtils::containsAll(const std::vector<QVector3D>& points, const std::vector<QVector3D>& polygon, const bool boundsContained /*= true*/)
{
    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    std::vector<ClipperLib::IntPoint> polyPoints;
    for (auto&& pt : polygon)
        polyPoints.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

    for (auto&& point : points)
    {
        const int result = ClipperLib::PointInPolygon(ClipperLib::IntPoint((double)(point.x()) * 1000., (double)(point.z()) * 1000.), polyPoints);

        if (result == 0 || (!boundsContained && result == -1))
            return false;
    }

    return true;
}

bool PolygonUtils::containsAny(const std::vector<QVector3D>& points, const std::vector<QVector3D>& polygon, const bool boundsContained /*= true*/)
{
    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    std::vector<ClipperLib::IntPoint> polyPoints;
    for (auto&& pt : polygon)
        polyPoints.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

    for (auto&& point : points)
    {
        const int result = ClipperLib::PointInPolygon(ClipperLib::IntPoint((double)(point.x()) * 1000., (double)(point.z()) * 1000.), polyPoints);

        if (result == 1 || (boundsContained && result == -1))
            return true;
    }

    return false;
}

Polygon2D::VertexType PolygonUtils::getVertexType(const std::vector<QVector3D> polygon,  int idx)
{
    const bool isClockwise = PolygonUtils::isCW(polygon);
    const auto cPts = asCircular(polygon);
    const GVector2D& currPt = cPts[idx];
    const GVector2D& prevPt = cPts.getPrev(idx);
    const GVector2D& nextPt = cPts.getNext(idx);

    const float crossProduct = GVector2D::crossProduct(prevPt, currPt, nextPt);

    // If vectors coordinates are too bid - floating point error has tendention to grow and default epsilon offset isn't enough
    // factor is asin(alpha) = 0.01, therefore alpha approximately <= 0.6°
    const float zeroThreshold = distanceSquared2D(prevPt, currPt) * 0.01f;

    if (fabs(crossProduct) < zeroThreshold)
        return Polygon2D::VertexType::Collinear;

    if ((isClockwise && crossProduct < 0.f) || (!isClockwise && crossProduct > 0.f))
        return Polygon2D::VertexType::Concave;

    return Polygon2D::VertexType::Convex;
}

std::vector<QVector3D> PolygonUtils::findPathInsidePolygon(const GVector2D& startPoint, const GVector2D& endPoint, const std::vector<QVector3D>& polygon, const std::vector<std::vector<QVector3D>>& innerPolygons)
{
    std::vector<std::tuple<QVector3D, std::vector<int>>> nodes;

    nodes.push_back({ startPoint, {} });
    nodes.push_back({ endPoint, {} });
    for (int i = 0; i < polygon.size(); i++)
        if (getVertexType(polygon, i) == Polygon2D::VertexType::Concave && (GVector2D)polygon[i] != startPoint && (GVector2D)polygon[i] != endPoint)
            nodes.push_back({ polygon[i], {} });

    for(auto&& innerPolygon : innerPolygons)
        for (int i = 0; i < innerPolygon.size(); i++)
            if (getVertexType(innerPolygon, i) == Polygon2D::VertexType::Convex && (GVector2D)innerPolygon[i] != startPoint && (GVector2D)innerPolygon[i] != endPoint)
                nodes.push_back({ innerPolygon[i], {} });

    // Find every connection inside polygon and outside inner polygons
    for(int n1 = 0; n1 < nodes.size(); n1++)
        for (int n2 = n1; n2 < nodes.size(); n2++)
            if (n1 != n2)
        {
            auto&& [pt1, edges1] = nodes[n1];
            auto&& [pt2, edges2] = nodes[n2];
            auto&& dir = (pt1 - pt2).normalized();

            if (!PolygonUtils::intersects({ pt1 - dir, pt2 + dir }, polygon) && 
                PolygonUtils::contains((pt1 + pt2) * 0.5f, polygon) &&
                std::all_of(innerPolygons.begin(), innerPolygons.end(), [&](auto& p) { return !PolygonUtils::intersects({ pt1 - dir, pt2 + dir }, p); }) &&
                std::none_of(innerPolygons.begin(), innerPolygons.end(), [&](auto& p) { return PolygonUtils::contains((pt1 + pt2) * 0.5f, p); }))
            {
                edges1 << n2;
                edges2 << n1;
            }
        }

    // bfs find path
    std::vector<bool> visited(nodes.size(), false);
    std::vector<int> parent(nodes.size(), -1);
    std::queue<int> queue;
    queue.push(0);
    visited[0] = true;

    while (!queue.empty())
    {
        auto node1 = queue.front();
        auto&& [pos1, edges] = nodes[node1];
        queue.pop();

        if (node1 == 1)
            break;

        for(auto&& node2 : edges)
            if (!visited[node2])
            {
                auto&& [pos2, _] = nodes[node2];

                visited[node2] = true;
                parent[node2] = node1;
                queue.push(node2);
            }
    }

    std::vector<QVector3D> path;
    int parentIdx = 1;
    path.push_back(std::get<QVector3D>(nodes[parentIdx]));
    while (parent[parentIdx] != -1)
    {
        path.push_back(std::get<QVector3D>(nodes[parent[parentIdx]]));
        parentIdx = parent[parentIdx];
    }

    return path;
}

bool PolygonUtils::isInDistanceOfEdges(const GVector2D& point, float range, const std::vector<QVector3D>& polygon)
{
    auto&& CPts = asCircular(polygon);

    for (int i = 0; i < polygon.size(); i++)
        if (Segment2D(polygon[i], polygon[CPts.findIdx(i, 1)]).dist(point) < range)
            return true;

    return false;
}

bool PolygonUtils::isCW(const std::vector<QVector3D>& polygon)
{
    float clockwise = 0.0f;

    for (int i = 0; i < polygon.size(); ++i)
    {
        const int next = i == polygon.size() - 1 ? 0 : i + 1;
        clockwise += (polygon[next].x() - polygon[i].x()) * (polygon[next].z() + polygon[i].z());
    }
    Q_ASSERT(!std::is_eq(fCmp(clockwise, 0.f)));

    return clockwise > 0.0f;
}

std::vector<std::vector<QVector3D>> PolygonUtils::calculatePolygonsFromGridSquares(const QSet<GPoint>& set)
{
    std::vector<std::vector<QVector3D>> polygons;

    auto perimeter = std::get<std::vector<Segment2D>>(computePerimeterForSquares(set));

    QSet<Segment2D> segmentsToCheck;
    for (auto&& segment : perimeter)
        segmentsToCheck << segment;

    std::vector<QVector3D> polygon;
    Segment2D segment = *segmentsToCheck.begin();
    GVector2D point = segment.first;

    while (!segmentsToCheck.empty())
    {
        polygon << point;
        segmentsToCheck.remove(segment);

        auto result = std::find_if(perimeter.begin(), perimeter.end(), [&](auto& s) { return segmentsToCheck.contains(s) && (s.first == point || s.second == point); });
        if (result != perimeter.end())
        {
            segment = *result;
            point = result->first == point ? result->second : result->first;
        }
        else
        {
            polygons << simplifyPolygon(polygon);
            polygon.clear();
            segment = !segmentsToCheck.empty() ? *segmentsToCheck.begin() : Segment2D();
            point = segment.first;
        }
    }

    return polygons;
}

QSet<GPoint> PolygonUtils::calculateGridSquaresOfPolygon(const std::vector<QVector3D>& polygon, bool onlyPerimeter /*= false*/)
{
    static float minPathDistance = GRID_SEGMENT_WIDTH * 0.5f;
    auto&& reducedDistancePolygon = reducePathPointsDistance(polygon, minPathDistance, true);

    QSet<GPoint> squares;

    for (auto&& pt : reducedDistancePolygon)
        squares += ((GVector2D)pt).toGPoint();

    if (onlyPerimeter)
        return squares;

    int minX = std::numeric_limits<int>::max(), minZ = std::numeric_limits<int>::max(), maxX = 0, maxZ = 0;

    for (auto&& sq : squares)
    {
        minX = std::min(minX, sq.x);
        minZ = std::min(minZ, sq.z);
        maxX = std::max(maxX, sq.x);
        maxZ = std::max(maxZ, sq.z);
    }

    // find all squares inside bounds of polygon perimeter
    QSet<GPoint> potentialInnerSquares;
    for (int x = minX; x <= maxX; x++)
        for (int z = minZ; z <= maxZ; z++)
            potentialInnerSquares += GPoint(x, z);
    potentialInnerSquares -= squares;

    auto&& potentialInnerSquareSets = Omnigen::get()->partitionSquares(potentialInnerSquares);

    // add squares if they do not touch bounds of polygon perimeter (pressumably are inside polygon)
    for (auto&& potentialSquares : potentialInnerSquareSets)
        if (std::none_of(potentialSquares.begin(), potentialSquares.end(), [&](auto& sq) { return sq.x == minX || sq.x == maxX || sq.z == minZ || sq.z == maxZ; }))
            squares += potentialSquares;

    return squares;
}

std::vector<float> PolygonUtils::coverage(const std::vector<QVector3D>& polygon, const std::vector<std::vector<QVector3D>>& polygons, float reductionDist /*= 5000.0f*/)
{
    float minSizeToReduction = reductionDist * 5.0f;

    auto getClipperPolygon = [&](const std::vector<QVector3D>& polygon)
    {
        std::vector<ClipperLib::IntPoint> clipperPolygon;

        float storedDist = 0;
        auto cPolygon = asCircular(polygon);

        bool useReduction = false;

        for(int i = 0; i < polygon.size(); i++)
            if (auto&& dist = ((GVector2D)polygon[i]).dist(polygon[cPolygon.findIdx(i, -1)]); (storedDist += dist) > minSizeToReduction)
            {
                useReduction = true;
                break;
            }

        if (useReduction)
        {
            storedDist = 0;
            for (int i = 0; i < polygon.size(); i++)
                if (auto&& dist = ((GVector2D)polygon[i]).dist(polygon[cPolygon.findIdx(i, -1)]); (storedDist += dist) > reductionDist)
                {
                    clipperPolygon.emplace_back(polygon[i].x(), polygon[i].z());
                    storedDist = 0;
                }
        }
        else
            for (int i = 0; i < polygon.size(); i++)
                clipperPolygon.emplace_back(polygon[i].x(), polygon[i].z());

        return clipperPolygon;
    };

    auto clipperPolygon = getClipperPolygon(polygon);
    auto clipperPolygonArea = std::abs(ClipperLib::Area(clipperPolygon));

    std::vector<float> polygonSimilarities(polygons.size());

    for (int i = 0; i < polygonSimilarities.size(); i++)
    {
        ClipperLib::Clipper newClipper{};
        newClipper.AddPath(clipperPolygon, ClipperLib::PolyType::ptSubject, true);

        auto clipperPoly = getClipperPolygon(polygons[i]);
        newClipper.AddPath(clipperPoly, ClipperLib::PolyType::ptClip, true);

        ClipperLib::Paths intersectedPolygonPaths;
        newClipper.Execute(ClipperLib::ClipType::ctIntersection, intersectedPolygonPaths, ClipperLib::PolyFillType::pftNonZero);

        auto originAreas = clipperPolygonArea + std::abs(ClipperLib::Area(clipperPoly));
        double intersectAreas = 0;
        for (auto&& intersectPoly : intersectedPolygonPaths)
            intersectAreas += std::abs(ClipperLib::Area(intersectPoly));
        intersectAreas *= 2.;

        auto biggerArea = originAreas > intersectAreas ? originAreas : intersectAreas;
        auto smallerArea = originAreas < intersectAreas ? originAreas : intersectAreas;

        polygonSimilarities[i] = smallerArea / std::max(1., biggerArea);
    }

    return polygonSimilarities;
}

std::vector<std::vector<QVector3D>> PolygonUtils::mergePolygons(const std::vector<std::vector<QVector3D>>& polygons)
{
    ClipperLib::Clipper newClipper{};

    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    for (auto&& polygon : polygons)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        newClipper.AddPath(APts, ClipperLib::PolyType::ptClip, true);
    }

    ClipperLib::Paths mergedPolygonPaths;
    newClipper.Execute(ClipperLib::ClipType::ctUnion, mergedPolygonPaths, ClipperLib::PolyFillType::pftNonZero);

    std::vector<std::vector<QVector3D>> mergedPolygons;

    for (auto&& mergedPolygonPath : mergedPolygonPaths)
    {
        std::vector<QVector3D> mergedPolygon;

        for (auto&& pt : mergedPolygonPath)
            mergedPolygon.emplace_back(pt.X * 0.001, 0, pt.Y * 0.001);

        mergedPolygons << mergedPolygon;
    }

    return mergedPolygons;
}

std::vector<std::vector<QVector3D>> PolygonUtils::intersectPolygons(const std::vector<QVector3D>& polygon, const std::vector<std::vector<QVector3D>>& polygons)
{
    ClipperLib::Clipper newClipper{};

    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        newClipper.AddPath(APts, ClipperLib::PolyType::ptSubject, true);
    }

    for (auto&& polygon : polygons)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        newClipper.AddPath(APts, ClipperLib::PolyType::ptClip, true);
    }

    ClipperLib::Paths mergedPolygonPaths;
    newClipper.Execute(ClipperLib::ClipType::ctIntersection, mergedPolygonPaths, ClipperLib::PolyFillType::pftNonZero);

    std::vector<std::vector<QVector3D>> mergedPolygons;

    for (auto&& mergedPolygonPath : mergedPolygonPaths)
    {
        std::vector<QVector3D> mergedPolygon;

        for (auto&& pt : mergedPolygonPath)
            mergedPolygon.emplace_back(pt.X * 0.001, 0, pt.Y * 0.001);

        mergedPolygons << mergedPolygon;
    }

    return mergedPolygons;
}

std::vector<std::vector<QVector3D>> PolygonUtils::intersectPolygons(const std::vector<std::vector<QVector3D>>& polygons1, const std::vector<std::vector<QVector3D>>& polygons2)
{
    ClipperLib::Clipper newClipper{};

    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    for(auto&& polygon : polygons1)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        newClipper.AddPath(APts, ClipperLib::PolyType::ptSubject, true);
    }

    for (auto&& polygon : polygons2)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        newClipper.AddPath(APts, ClipperLib::PolyType::ptClip, true);
    }

    ClipperLib::Paths mergedPolygonPaths;
    newClipper.Execute(ClipperLib::ClipType::ctIntersection, mergedPolygonPaths, ClipperLib::PolyFillType::pftNonZero);

    std::vector<std::vector<QVector3D>> mergedPolygons;

    for (auto&& mergedPolygonPath : mergedPolygonPaths)
    {
        std::vector<QVector3D> mergedPolygon;

        for (auto&& pt : mergedPolygonPath)
            mergedPolygon.emplace_back(pt.X * 0.001, 0, pt.Y * 0.001);

        mergedPolygons << mergedPolygon;
    }

    return mergedPolygons;
}

std::vector<std::vector<QVector3D>> PolygonUtils::cutPolygons(const std::vector<std::vector<QVector3D>>& polygonsToCut, const std::vector<std::vector<QVector3D>>& cuttingPolygons, bool simplyfy /*= false*/)
{
    ClipperLib::Clipper cutClipper{};

    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    for (auto&& polygon : polygonsToCut)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        cutClipper.AddPath(APts, ClipperLib::PolyType::ptSubject, true);
    }

    for (auto&& polygon : cuttingPolygons)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        cutClipper.AddPath(APts, ClipperLib::PolyType::ptClip, true);
    }

    return cutPolygons(&cutClipper, simplyfy);
}

std::vector<std::vector<QVector3D>> PolygonUtils::cutPolygon(const std::vector<QVector3D>& polygonToCut, const std::vector<std::vector<QVector3D>>& cuttingPolygons, bool simplyfy /*= false*/)
{
    ClipperLib::Clipper cutClipper{};

    // Increasing precision by increasing value by magnitude of 10^3 (overflow vulnerable)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygonToCut)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        cutClipper.AddPath(APts, ClipperLib::PolyType::ptSubject, true);
    }

    for (auto&& polygon : cuttingPolygons)
    {
        std::vector<ClipperLib::IntPoint> APts;
        for (auto&& pt : polygon)
            APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

        cutClipper.AddPath(APts, ClipperLib::PolyType::ptClip, true);
    }

    return cutPolygons(&cutClipper, simplyfy);
}

std::vector<std::vector<QVector3D>> PolygonUtils::cutPolygons(ClipperLib::Clipper* cutClipper, bool simplyfy)
{
    ClipperLib::Paths cutPolygonPaths;
    cutClipper->Execute(ClipperLib::ClipType::ctDifference, cutPolygonPaths, ClipperLib::PolyFillType::pftNonZero);

    // Removes situations where 2 polygons which shares 1 edge would not be connected
    ClipperLib::Paths simplifiedPolys;
    if (simplyfy)
        ClipperLib::SimplifyPolygons(cutPolygonPaths, simplifiedPolys, ClipperLib::pftNonZero);

    std::vector<std::vector<QVector3D>> cutPolygons;

    auto&& pathToUse = simplyfy ? simplifiedPolys : cutPolygonPaths;
    for (auto&& polyPath : pathToUse)
    {
        std::vector<QVector3D> newPts;

        for (auto&& pt : polyPath)
            newPts.emplace_back(pt.X * 0.001, 0, pt.Y * 0.001);

        cutPolygons << newPts;
    }

    return cutPolygons;
}

std::vector<std::vector<QVector3D>> PolygonUtils::inflatePolygon(const std::vector<QVector3D>& polygon, const double amount)
{
    ClipperLib::ClipperOffset newClipper;

    std::vector<ClipperLib::IntPoint> APts;
    for (auto&& pt : polygon)
        APts.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);
    newClipper.AddPath(APts, ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etClosedPolygon);

    ClipperLib::Paths inflatedPolygonPaths;
    newClipper.Execute(inflatedPolygonPaths, amount * 1000.);

    std::vector<std::vector<QVector3D>> inflatedPolygons;

    for (auto&& inflatedPolygonPath : inflatedPolygonPaths)
    {
        std::vector<QVector3D> inflatedPolygon;

        for (auto&& pt : inflatedPolygonPath)
            inflatedPolygon.emplace_back(pt.X * 0.001, 0, pt.Y * 0.001);

        inflatedPolygons << inflatedPolygon;
    }

    return inflatedPolygons;
}

std::vector<std::vector<QVector3D>> PolygonUtils::simplifyPolygon(const std::vector<QVector3D>& polygon)
{
    ClipperLib::Path polygonSubject;
    for (auto&& pt : polygon)
        polygonSubject.emplace_back((double)(pt.x()) * 1000., (double)(pt.z()) * 1000.);

    ClipperLib::Paths simplifiedPolygonPaths;
    ClipperLib::SimplifyPolygon(polygonSubject, simplifiedPolygonPaths, ClipperLib::pftNonZero);

    std::vector<std::vector<QVector3D>> simplifiedPolygons;

    for (auto&& simplifiedPolygonPath : simplifiedPolygonPaths)
    {
        std::vector<QVector3D> simplifiedPolygon;

        for (auto&& pt : simplifiedPolygonPath)
            simplifiedPolygon.emplace_back(pt.X * 0.001, 0, pt.Y * 0.001);

        simplifiedPolygons << simplifiedPolygon;
    }

    return simplifiedPolygons;
}

