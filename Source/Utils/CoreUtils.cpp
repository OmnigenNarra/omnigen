#include "stdafx.h"
#include "CoreUtils.h"
#include "CircularVectorView.h"
#include "Mathematics/DistSegmentSegment.h"
#include "Mathematics/IntrSegment2Segment2.h"
#include "Mathematics/IntrRay2Circle2.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/Polygon.h"

#include <mutex>
#include <Mathematics/ContPointInPolygon2.h>
#include <Mathematics/DistSegmentSegment.h>

#include <QDateTime>
#include <QLayout>
#include <QLayoutItem>
#include <QWidget>

QSet<GVector2D> circleCircleIntersection(GVector2D p1, double r1, GVector2D p2, double r2)
{
    double a, dx, dy, d, h, rx, ry;
    double x2, y2;

    /* dx and dy are the vertical and horizontal distances between
     * the circle centers.
     */
    dx = p2.x - p1.x;
    dy = p2.z - p1.z;

    /* Determine the straight-line distance between the centers. */
    d = std::hypot(dx, dy);

    /* Check for solvability. */
    if (d > (r1 + r2))
    {
        /* no solution. circles do not intersect. */
        return {};
    }
    if (d < fabs(r1 - r2))
    {
        /* no solution. one circle is contained in the other */
        return {};
    }

    /* 'point 2' is the point where the line through the circle
     * intersection points crosses the line between the circle
     * centers.
     */

     /* Determine the distance from point 0 to point 2. */
    a = ((r1 * r1) - (r2 * r2) + (d * d)) / (2.0 * d);

    /* Determine the coordinates of point 2. */
    x2 = p1.x + (dx * a / d);
    y2 = p1.z + (dy * a / d);

    /* Determine the distance from point 2 to either of the
     * intersection points.
     */
    h = sqrt((r1 * r1) - (a * a));

    /* Now determine the offsets of the intersection points from
     * point 2.
     */
    rx = -dy * (h / d);
    ry = dx * (h / d);

    /* Determine the absolute intersection points. */
    QSet<GVector2D> results;
    results << GVector2D(x2 + rx, y2 + ry) << GVector2D(x2 - rx, y2 - ry);

    return results;
}

qint64 makeGuid()
{
    static std::mutex guard;
    static qint64 id = 0;

    std::scoped_lock lock(guard);
    qint64 guid = QDateTime::currentDateTime().toMSecsSinceEpoch() + (++id);
    return guid;
}

void appendFace(std::vector<IndexType>& allIndices, const std::vector<IndexType>& vertexIndices, bool backface /*= false*/)
{
    for (auto it = vertexIndices.begin(); it != vertexIndices.end(); ++it)
        allIndices << *it;

    if (backface)
        for (auto rit = vertexIndices.rbegin(); rit != vertexIndices.rend(); ++rit)
            allIndices << *rit;
}

void appendLines(std::vector<IndexType>& allIndices, const std::vector<IndexType>& lineIndices, bool loop /*= false*/)
{
    for (IndexType i = 0; i < int(lineIndices.size()) - 1; ++i)
        allIndices << lineIndices[i] << lineIndices[i + 1];

    if (loop)
        allIndices << lineIndices.back() << lineIndices.front();
}

std::string prettifyName(std::string name)
{
    for (int i = 0; i < int(name.size()) - 1; ++i)
        if (std::islower(name[i]) && std::isupper(name[i + 1]))
            name.insert(name.begin() + i + 1, ' ');

    return name;
}

void omnigen_assert(const QString& message)
{
    OmniLog(ELoggingLevel::Critical) <<= message;
    Q_ASSERT(false);
}

void hideAllLayoutContents(QLayout* l)
{
    for (int i = 0; i < l->count(); i++) 
    {
        auto item = l->itemAt(i);
        if (item->layout())
            hideAllLayoutContents(item->layout());

        if (item->widget())
            item->widget()->hide();
    }
}

void clearLayout(QLayout* l)
{
    while (QLayoutItem* item = l->takeAt(0))
    {
        // If item is layout itself, we should clear that layout as well.
        if (item->layout())
        {
            clearLayout(item->layout());

            // After clearing layout, we delete it.
            delete item->layout();
        }

        // We check if item has some widget. If so, we also delete that widget.
        if (item->widget())
            item->widget()->deleteLater();

        // Finally, we remove item itself.
        delete item;
    }
}

std::vector<QVector3D> reducePathPointsDistance(const std::vector<QVector3D>& path, float distanceBetweenPoints, bool isCircular)
{
    std::vector<QVector3D> newPath;

    auto cPath = asCircular(path);
    for (int i = 0; i < path.size(); i++)
    {
        newPath << path[i];

        if (!isCircular && i == path.size() - 1)
            break;

        auto&& p1 = path[i];
        auto&& p2 = path[cPath.findIdx(i, 1)];
        auto dist = p1.distanceToPoint(p2);
        auto dir = (p2 - p1).normalized();
        int pointsToAdd = dist / distanceBetweenPoints - 1;

        for (int j = 0; j < pointsToAdd; j++)
            newPath << (newPath.back() + dir * distanceBetweenPoints);
    }

    return newPath;
}

void simplifieLine(std::vector<QVector3D>* line, float e)
{
    std::vector<QVector3D> simplifiedLine;
    for (int n = 0; n < 3; n++)
    {
        int idx = 0;
        simplifiedLine.clear();
        simplifiedLine << (*line)[0];
        while (idx + 2 < line->size())
        {
            auto segment3D = std::vector{ (*line)[idx], (*line)[idx + 2] };
            if (std::get<float>(directionalBoundDistance(segment3D, (*line)[idx + 1])) < e)
            {
                simplifiedLine << (*line)[idx + 2];
                idx += 2;
            }
            else
            {
                simplifiedLine << (*line)[idx + 1];
                idx += 1;
            }
        }
        if (idx + 2 == line->size())
            simplifiedLine << line->back();
        (*line) = simplifiedLine;
    }
}

void removeSelfIntersections(std::vector<GVector2D>* line)
{
    for (int i = 0; i < line->size() - 1; i++)
    {
        Segment2D segment((*line)[i], (*line)[i + 1]);

        for (int j = i + 1; j < line->size() - 1; j++)
        {
            Segment2D segment2((*line)[j], (*line)[j + 1]);

            if (segment.intersects(segment2, false))
            {
                auto dir = ((*line)[i] + (*line)[j + 1]).normalized();
                auto dist = (*line)[i].dist((*line)[j + 1]);

                float t = 0.0f;
                for (int k = i + 1; k < j + 1; k++)
                {
                    t = (k - i) / (float)(j + 1 - i);
                    (*line)[k] = (*line)[i] + dir * dist * t;
                }

                i = j + 1;
                break;
            }
        }
    }
}

std::tuple<QVector3D, QVector3D, float> distance(const std::array<QVector3D, 2>& S1, const std::array<QVector3D, 2>& S2, bool returnSquared)
{
    gte::DCPSegment3Segment3<float> query;
    auto result = query(gte::Segment3<float>{ QtoV3(S1[0]), QtoV3(S1[1]) }, gte::Segment3<float>{ QtoV3(S2[0]), QtoV3(S2[1]) });
    return { VtoQ3(result.closest[0]), VtoQ3(result.closest[1]), returnSquared ? result.sqrDistance : result.distance };
}

std::tuple<GVector2D, GVector2D, float> distance(const std::array<GVector2D, 2>& S1, const std::array<GVector2D, 2>& S2, bool returnSquared)
{
    gte::DCPSegment2Segment2<float> query;
    auto result = query(gte::Segment2<float>{ GtoV2(S1[0]), GtoV2(S1[1]) }, gte::Segment2<float>{ GtoV2(S2[0]), GtoV2(S2[1]) });
    return { VtoG2(result.closest[0]), VtoG2(result.closest[1]), returnSquared ? result.sqrDistance : result.distance };
}

std::tuple<GVector2D, float> distance(const Segment2D& segment, const GVector2D& p, bool returnSquared)
{
    auto&& [v, w] = segment;

    // Return minimum distance between line segment vw and point p
    const float l2 = distanceSquared(v, w);  // i.e. |w-v|^2 -  avoid a sqrt
    if (l2 == 0.0)
        return { v, returnSquared ? distanceSquared(p, v) : distance(p, v) };   // v == w case

    // Consider the line extending the segment, parameterized as v + t (w - v).
    // We find projection of point p onto the line. 
    // It falls where t = [(p-v) . (w-v)] / |w-v|^2
    // We clamp t from [0,1] to handle points outside the segment vw.
    const float t = std::max(0.0f, std::min(1.0f, GVector2D::dotProduct(p - v, w - v) / l2));
    const GVector2D projection = std::lerp(v, w, t);  // Projection falls on the segment

    return { projection, returnSquared ? distanceSquared(p, projection) : distance(p, projection) };
}

float distance(const GVector2D& p1, const GVector2D& p2)
{
    return std::hypot(p1.x - p2.x, p1.z - p2.z);
}

float distance(const QVector3D& p1, const QVector3D& p2)
{
    return std::hypot(p1.x() - p2.x(), p1.y() - p2.y(), p1.z() - p2.z());
}

EIntersectionResult lineIntersection2D(const GVector2D& v0_p0, const GVector2D& v0_p1, const GVector2D& v1_p0, const GVector2D& v1_p1)
{
    gte::TIQuery<float, gte::Segment2<float>, gte::Segment2<float>> query;
    auto result = query(gte::Segment2<float>{ GtoV2(v0_p0), GtoV2(v0_p1) }, gte::Segment2<float>{ GtoV2(v1_p0), GtoV2(v1_p1) });
    return static_cast<EIntersectionResult>(result.numIntersections);
}

std::optional<GVector2D> getLineIntersectionPoint(const GVector2D& pt1, const GVector2D& dir1, const GVector2D& pt2, const GVector2D& dir2)
{
    gte::FIQuery<float, gte::Line2<float>, gte::Line2<float>> query;
    const auto result = query(gte::Line2<float>{ GtoV2(pt1), GtoV2(dir1) }, gte::Line2<float>{ GtoV2(pt2), GtoV2(dir2) });

    if (result.intersect && result.numIntersections == 1)
        return {VtoG2(result.point)};

    return std::nullopt;
}

std::optional<GVector2D> getRayCircleIntersection(const GVector2D& rayStart, const GVector2D& rayDirection, const GVector2D& center, float radius)
{
    gte::FIQuery<float, gte::Line2<float>, gte::Circle2<float>> query;
    const auto result = query(gte::Line2<float>(GtoV2(rayStart), GtoV2(rayDirection)), gte::Circle2<float>(GtoV2(center), radius));

    if (result.intersect)
        return {VtoG2(result.point.front())};

    return std::nullopt;
}

std::tuple<GVector2D, float> circularBoundDistance(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared)
{
    if (bounds.empty())
        return { QVector3D(), -1.0f };

    float minD = std::numeric_limits<float>::max();
    std::vector<GVector2D> nearest;
    auto cPts = asCircular(bounds);

    for (int i=0; i<bounds.size(); ++i)
    {
        int i2 = cPts.findIdx(i, 1);

        auto [v1, d] = distance({bounds[i], bounds[i2]}, p, true);
        if (d < minD)
        {
            minD = d;
            nearest = { v1 };
        }
        else if (d == minD)
        {
            nearest << v1;
        }
    }

    std::sort(nearest.begin(), nearest.end());

    return { nearest.front(), returnSquared ? minD : sqrt(minD) };
}

std::tuple<QVector3D, float> circularBoundDistance(const std::vector<Segment2D>& bounds, const GVector2D& p, bool returnSquared /*= false*/)
{
    if (bounds.empty())
        return { QVector3D(), -1.0f };

    float minD = std::numeric_limits<float>::max();
    std::vector<GVector2D> nearest;

    for (auto&& s : bounds)
    {
        auto [v1, d] = distance(s, p, true);
        if (d < minD)
        {
            minD = d;
            nearest = { v1 };
        }
        else if (d == minD)
        {
            nearest << v1;
        }
    }

    std::sort(nearest.begin(), nearest.end());

    return { nearest.front(), returnSquared ? minD : sqrt(minD) };
}


std::tuple<GVector2D, int, float> circularBoundDistanceAdv(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared /*= false*/)
{
	if (bounds.empty())
		return { QVector3D(), -1, -1.0f };

    struct PointWithIndex
    {
        GVector2D p;
        int i;
    };

	float minD = std::numeric_limits<float>::max();
	std::vector<PointWithIndex> nearest;
	auto cPts = asCircular(bounds);

	for (int i = 0; i < bounds.size(); ++i)
	{
		int i2 = cPts.findIdx(i, 1);

		auto [v1, d] = distance({ bounds[i], bounds[i2] }, p, true);
		if (d < minD)
		{
			minD = d;
			nearest = { PointWithIndex(v1, i) };
		}
		else if (d == minD)
		{
			nearest <<= PointWithIndex(v1, i);
		}
	}

    std::sort(nearest.begin(), nearest.end(), [](auto&& A, auto&& B) { return A.i < B.i; });

	return { nearest.front().p, nearest.front().i, returnSquared ? minD : sqrt(minD) };
}

std::tuple<GVector2D, float, int> directionalBoundDistance(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared /*= false*/)
{
    if (bounds.empty())
        return { GVector2D(), -1.0f, -1 };

    float minD = std::numeric_limits<float>::max();
    std::vector<std::tuple<GVector2D, int>> nearest;

    for (int i = 0; i < int(bounds.size()) - 1; ++i)
    {
        auto [v1, d] = distance({ bounds[i], bounds[i+1] }, p, true);
        if (d < minD)
        {
            minD = d;
            nearest = { {v1, i} };
        }
        else if (d == minD)
        {
            nearest.push_back({v1, i});
        }
    }

    std::sort(nearest.begin(), nearest.end());

    return { std::get<GVector2D>(nearest.front()), returnSquared ? minD : sqrt(minD), std::get<int>(nearest.front()) };
}

std::tuple<QVector3D, float, int> directionalBoundDistance(const std::vector<QVector3D>& bounds, const QVector3D& p, bool returnSquared /*= false*/)
{
    if (bounds.empty())
        return { QVector3D(), -1.0f, -1 };

    float minD = std::numeric_limits<float>::max();
    std::vector<std::tuple<QVector3D, int>> nearest;

    for (int i = 0; i < int(bounds.size()) - 1; ++i)
    {
        auto [v1, v2, d] = distance({ bounds[i], bounds[i + 1] }, std::array{ p, p }, true);
        if (d < minD)
        {
            minD = d;
            nearest = { {v1, i} };
        }
        else if (d == minD)
        {
            nearest.push_back({ v1, i });
        }
    }

    std::sort(nearest.begin(), nearest.end());

    return { std::get<QVector3D>(nearest.front()), returnSquared ? minD : sqrt(minD), std::get<int>(nearest.front()) };
}

std::tuple<QVector3D, QVector3D, float> line2LineDistance(const std::vector<QVector3D>& l1, const std::vector<QVector3D>& l2, bool returnSquared /*= false*/, bool excludeEnds /*= false*/)
{
    float minD = std::numeric_limits<float>::max();
    QVector3D V1;
    QVector3D V2;

    for(int i1=1; i1<l1.size(); ++i1)
        for (int i2 = 1; i2 < l2.size(); ++i2)
        {
            auto [v1, v2, d] = distance(std::array{ l1[i1], l1[i1 - 1] }, { l2[i2], l2[i2 - 1] }, true);
            if (excludeEnds)
            {
                if (v1 == l1[i1])
                    v1 = std::lerp(l1[i1], l1[i1 - 1], 0.05f);
                else if (v1 == l1[i1 - 1])
                    v1 = std::lerp(l1[i1], l1[i1 - 1], 0.95f);

                if (v2 == l2[i2])
                    v2 = std::lerp(l2[i2], l2[i2 - 1], 0.05f);
                else if (v2 == l2[i2 - 1])
                    v2 = std::lerp(l2[i2], l2[i2 - 1], 0.95f);

                d = distanceSquared(v1, v2);
            }

            if (d < minD)
            {
                minD = d;
                V1 = v1;
                V2 = v2;
            }
        }

    return { V1, V2, returnSquared ? minD : std::sqrt(minD) };
}

std::tuple<GVector2D, GVector2D, float> line2LineDistance(const std::vector<GVector2D>& l1, const std::vector<GVector2D>& l2, bool returnSquared /*= false*/, bool excludeEnds /*= false*/)
{
    float minD = std::numeric_limits<float>::max();
    GVector2D V1;
    GVector2D V2;

    for (int i1 = 1; i1 < l1.size(); ++i1)
        for (int i2 = 1; i2 < l2.size(); ++i2)
        {
            auto [v1, v2, d] = distance(std::array{ l1[i1], l1[i1 - 1] }, { l2[i2], l2[i2 - 1] }, true);
            if (excludeEnds)
            {
                if (v1 == l1[i1])
                    v1 = std::lerp(l1[i1], l1[i1 - 1], 0.05f);
                else if (v1 == l1[i1 - 1])
                    v1 = std::lerp(l1[i1], l1[i1 - 1], 0.95f);

                if (v2 == l2[i2])
                    v2 = std::lerp(l2[i2], l2[i2 - 1], 0.05f);
                else if (v2 == l2[i2 - 1])
                    v2 = std::lerp(l2[i2], l2[i2 - 1], 0.95f);

                d = distanceSquared(v1, v2);
            }

            if (d < minD)
            {
                minD = d;
                V1 = v1;
                V2 = v2;
            }
        }

    return { V1, V2, returnSquared ? minD : std::sqrt(minD) };
}

float angle360(const QVector3D& v1, const QVector3D& v2)
{
    // [0-180]
    float result = angle180(v1, v2);

    // expand to [0-360]
    if (QVector3D::crossProduct(v1, v2).y() > 0)
        return 360.0 - result;
    else
        return result;
}

float angle180S(const QVector3D& v1, const QVector3D& v2)
{
    // [0-180]
    float result = angle180(v1, v2);

    // expand to [0-360]
    if (QVector3D::crossProduct(v1, v2).y() > 0)
        return -result;
    else
        return result;
}

float angle180(const QVector3D& v1, const QVector3D& v2)
{
    return acosf(std::clamp(QVector3D::dotProduct(v1, v2), -1.0f, 1.0f)) * 180.0 / std::numbers::pi;
}

float angleTo180Range(float angle)
{
    const float angleTruncated = fmodf(angle, 360.f);
    return fabsf(angleTruncated) <= 180.f ? angleTruncated : (angleTruncated < 0) ? angleTruncated + 360.f : angleTruncated - 360.f;
}

float angleTo360Range(float angle)
{
    const float angleTruncated = fmodf(angle, 360.f);
    return angleTruncated >= 0 ? angleTruncated : angleTruncated + 360.f;
}

float distanceFromPointToInfiniteLine(const GVector2D& linePt, const GVector2D& lineDirNormalized, const GVector2D& pt, bool returnSquared)
{
    Q_ASSERT(lineDirNormalized.x != 0.f || lineDirNormalized.z != 0.f);

    const GVector2D& linePt2 = linePt + lineDirNormalized;
    const float t = GVector2D::dotProduct(pt - linePt, linePt2 - linePt);
    const GVector2D projection = std::lerp(linePt, linePt2, t);
    return (returnSquared ? distanceSquared(pt, projection) : distance(pt, projection));
}

GVector2D::GVector2D(const QVector3D& qvec3d) 
    : x(qvec3d.x())
    , z(qvec3d.z())
{
}

float GVector2D::length() const
{
    return sqrt(x * x + z * z);
}

float GVector2D::lengthSquared() const
{
    return x * x + z * z;
}

float GVector2D::dist(const GVector2D& other,bool returnSquared) const
{
    return returnSquared ? distanceSquared2D(*this, other) : distance(*this, other);
}

void GVector2D::normalize()
{
    float len = length();
    if (fCmp(len, 0) == 0)
        return;

    x /= len;
    z /= len;
}

GVector2D GVector2D::normalized() const
{
    float len = length();
    if (fCmp(len, 0) == std::strong_ordering::equal)
        return GVector2D(0, 0);

    return GVector2D(x / len, z / len);
}

bool GVector2D::isNull() const
{
    return ((x == 0.0f) && (z == 0.0f));
}

GVector2D GVector2D::rotatedRight90() const
{
    return { z, -x };
}

GVector2D GVector2D::rotatedLeft90() const
{
    return { -z, x };
}

GPoint GVector2D::floor() const
{
    return { int(std::floor(x)), int(std::floor(z)) };
}

GPoint GVector2D::ceil() const
{
    return { int(std::ceil(x)), int(std::ceil(z)) };
}


GVector2D GVector2D::clamp(const GVector2D& min, const GVector2D& max) const
{
    return { std::clamp(x, min.x, max.x), std::clamp(z, min.z, max.z) };
}

float GVector2D::angle(const GVector2D& other) const
{
    return angle360(*this, other);
}

bool GVector2D::isInsidePolygon(const std::vector<GVector2D>& polygon) const
{
    gte::PointInPolygon2<float> query(polygon.size(), reinterpret_cast<const gte::Vector2<float>*>(polygon.data()));
    return query.Contains(reinterpret_cast<const gte::Vector2<float>&>(*this));
}

std::tuple<bool, int, float> GVector2D::isInsidePolygon(const std::vector<QVector3D>& polygon) const
{
    int sum = 0;
    float minD = std::numeric_limits<float>::max();
    int minP1, minP2;

    auto circ = asCircular(polygon);

    for (int i = 0; i < circ.getSize(); i++)
    {
        int i2 = circ.findIdx(i, 1);
        auto&& p1 = circ[i];
        auto&& p2 = circ[i2];
        if (auto [pt, d] = distance({ p1, p2 }, *this, true); d < minD)
        {
            minP1 = i;
            minP2 = i2;
            minD = d;
        }

        if (p1.z() == p2.z() || std::min(p1.z(), p2.z()) >= z || std::max(p1.z(), p2.z()) < z)
            continue;

        if (p1.z() > p2.z())
        {
            if (crossProduct(p1, p2, *this) > 0)
                sum++;
        }
        else 
        {
            if (crossProduct(p1, p2, *this) < 0)
                sum--;
        }
    }

    float d1 = distanceSquared(*this, circ[minP1]);
    float d2 = distanceSquared(*this, circ[minP2]);
    int minP = d1 < d2 ? minP1 : minP2;

    return { sum != 0, minP, std::sqrt(minD) };
}


std::tuple<bool, int, float> GVector2D::isInsidePolygon(const CircularVectorView<std::vector, QVector3D>& polygon) const
{
	int sum = 0;
	float minD = std::numeric_limits<float>::max();
	int minP1, minP2;

	for (int i = 0; i < polygon.getSize(); i++)
	{
		int i2 = polygon.findIdx(i, 1);
		GVector2D p1 = polygon[i];
        GVector2D p2 = polygon[i2];
		if (auto [pt, d] = distance({ p1, p2 }, *this, true); d < minD)
		{
			minP1 = i;
			minP2 = i2;
			minD = d;
		}

		if (p1.z == p2.z || std::min(p1.z, p2.z) >= z || std::max(p1.z, p2.z) < z)
			continue;

		if (p1.z > p2.z)
		{
			if (crossProduct(p1, p2, *this) > 0)
				sum++;
		}
		else
		{
			if (crossProduct(p1, p2, *this) < 0)
				sum--;
		}
	}

	float d1 = distanceSquared(*this, polygon[minP1]);
	float d2 = distanceSquared(*this, polygon[minP2]);
	int minP = d1 < d2 ? minP1 : minP2;

	return { sum != 0, minP, std::sqrt(minD) };
}

GVector2D GVector2D::operator+(const GVector2D& other) const noexcept
{
    return GVector2D(x + other.x, z + other.z);
}

GVector2D& GVector2D::operator+=(const GVector2D& other) noexcept
{
    x += other.x;
    z += other.z;
    return *this;
}

GVector2D GVector2D::operator-(const GVector2D& other) const noexcept
{
    return GVector2D(x - other.x, z - other.z);
}

GVector2D GVector2D::operator-() const noexcept
{
    return *this * -1.f;
}

GVector2D& GVector2D::operator-=(const GVector2D& other) noexcept
{
    x -= other.x;
    z -= other.z;
    return *this;
}

GVector2D GVector2D::operator*(const double f) const noexcept
{
    return GVector2D(double(x) * f, double(z) * f);
}

GVector2D operator*(float f, const GVector2D& vec)
{
    return vec * f;
}

GVector2D& GVector2D::operator*=(const float f) noexcept
{
    x *= f;
    z *= f;
    return *this;
}

GVector2D GVector2D::operator/(const double f) const noexcept
{
    return GVector2D(double(x) / f, double(z) / f);
}

GVector2D& GVector2D::operator/=(const float f) noexcept
{
    x /= f;
    z /= f;
    return *this;
}

GVector2D GVector2D::operator*(const float f) const noexcept
{
    return GVector2D(x * f, z * f);
}

GVector2D GVector2D::operator/(const float f) const noexcept
{
    return GVector2D(x / f, z / f);
}


float GVector2D::dotProduct(const GVector2D& u, const GVector2D& v)
{
    return u.x * v.x + u.z * v.z;
}

float GVector2D::crossProduct(const GVector2D& u, const GVector2D& v)
{
    return v.x * u.z - v.z * u.x;
}

// absolute return value = 2 * area of triangle(u, v, w)
// negative return value -> u, v, w are oriented clockwise
// positive return value -> u, v, w are oriented counterclockwise
float GVector2D::crossProduct(const GVector2D& u, const GVector2D& v, const GVector2D& w)
{
    return crossProduct(v - u, w - u);
}

GVector2D GVector2D::rotateRadians(const GVector2D& originalV, const double radians)
{
    const auto ca = std::cos(radians);
    const auto sa = std::sin(radians);
    return GVector2D(ca * originalV.x - sa * originalV.z, sa * originalV.x + ca * originalV.z);
}

GVector2D GVector2D::rotateDegrees(const GVector2D& originalV, const double degrees)
{
    const auto degToRadians = std::numbers::pi / 180;

    return rotateRadians(originalV, (degrees * degToRadians));
}

GVector2D GVector2D::lookAtVec2D(const GVector2D& vecToLookAt) const
{
    const auto vecCurr = this->normalized();
    const auto vecTarget = vecToLookAt.normalized();

    const auto nCurr = (vecCurr - *this).normalized();
    const auto nTarget = (vecTarget - *this).normalized();

    const float dotProd = dotProduct(nCurr, nTarget);

    const float thetaDev = std::acos(dotProd);

    return rotateRadians(*this, thetaDev);
}

GPoint GVector2D::toGPoint() const
{
    return { int(x / GRID_SEGMENT_WIDTH), int(z / GRID_SEGMENT_WIDTH) };
}

std::vector<double> softmax(const std::vector<double>& inValues)
{
    Q_ASSERT(!inValues.empty());

    std::vector<double> softmaxedValues;

    double m = std::numeric_limits<double>::lowest();
    for (auto&& val : inValues)
    {
        if (m < val)
            m = val;
    }

    double sum = 0.0;
    for (auto&& val : inValues)
    {
        sum += std::exp(val - m);
    }

    const double constant = m + std::log(sum);
    for (auto&& val : inValues)
    {
        softmaxedValues << std::exp(val - constant);
    }

    return softmaxedValues;
}

std::optional<Segment2D> Segment2D::operator&(const Segment2D& other) const
{
    // shortcuts
    float mx = first.x;
    float mz = first.z;
    float mX = second.x;
    float mZ = second.z;

    float x = other.first.x;
    float z = other.first.z;
    float X = other.second.x;
    float Z = other.second.z;

    std::vector<float> zets = { z, Z, mz, mZ };
    std::vector<float> xs = { x, X, mx, mX };

    std::sort(zets.begin(), zets.end());
    std::sort(xs.begin(), xs.end());

    // -x side
    if (mx == X)
        if ((zets != std::vector{ z, Z, mz, mZ }) && (zets != std::vector{ mz, mZ, z, Z }) && (zets[1] != zets[2]))
            return Segment2D{ GVector2D(X, zets[1]), GVector2D(X, zets[2]) };
        else
            return {};

    // +x side
    if (mX == x)
        if ((zets != std::vector{ z, Z, mz, mZ }) && (zets != std::vector{ mz, mZ, z, Z }) && (zets[1] != zets[2]))
            return Segment2D{ GVector2D(x, zets[1]), GVector2D(x, zets[2]) };
        else
            return {};

    // -z side
    if (mz == Z)
        if ((xs != std::vector{ x, X, mx, mX }) && (xs != std::vector{ mx, mX, x, X }) && (xs[1] != xs[2]))
            return Segment2D{ GVector2D(xs[1], Z), GVector2D(xs[2], Z) };
        else
            return {};

    // +z side
    if (mZ == z)
        if ((xs != std::vector{ x, X, mx, mX }) && (xs != std::vector{ mx, mX, x, X }) && (xs[1] != xs[2]))
            return Segment2D{ GVector2D(xs[1], z), GVector2D(xs[2], z) };
        else
            return {};

    return {};
}

// **********************************************************************************
// Exrernal impl based on https://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/
// Given three colinear points p, q, r, the function checks if 
// point q lies on line segment 'pr' 
bool Segment2D::onSegment(const GVector2D& p, const GVector2D& q, const GVector2D& r)
{
    if (q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x) &&
        q.z <= std::max(p.z, r.z) && q.z >= std::min(p.z, r.z))
        return true;

    return false;
}

// To find orientation of ordered triplet (p, q, r). 
// The function returns following values 
// 0 --> p, q and r are colinear 
// 1 --> Clockwise 
// 2 --> Counterclockwise 
int Segment2D::orientation(const GVector2D& p, const GVector2D& q, const GVector2D& r)
{
    // See https://www.geeksforgeeks.org/orientation-3-ordered-points/ 
    // for details of below formula. 

    double val = double(q.z - p.z) * double(r.x - q.x) -
        double(q.x - p.x) * double(r.z - q.z);

    if (val == 0) return 0; // colinear 

    return (val > 0) ? 1 : 2; // clock or counterclock wise 
}

std::tuple<bool, std::pair<int, int>, float> Segment2D::isInsidePolygon(const std::vector<QVector3D>& polygon) const
{
    bool isInside = PolygonUtils::contains(first, polygon) && !PolygonUtils::intersects(*this, polygon);
    std::pair<int, int> closestSegmentIdx{-1, -1};
    float closestDist = std::numeric_limits<float>::max();

    std::array<GVector2D, 2> segment{first, second};
    auto cPolygon = asCircular(polygon);
    for (int i = 0; i < polygon.size(); i++)
    {
        int i2 = cPolygon.findIdx(i, 1);
        std::array<GVector2D, 2> polySegment{ polygon[i], polygon[i2] };

        if (auto dist = std::get<float>(distance(segment, polySegment)); dist < closestDist)
        {
            closestDist = dist;
            closestSegmentIdx = { i, i2 };
        }
    }

    return {isInside, closestSegmentIdx, closestDist};
}


bool Segment2D::intersects(const Segment2D& other, bool includeEnds) const
{
    if (!includeEnds)
    {
        if (!intersects(other, true))
            return false;

        if (onSegment(first, other.first, second) && (GVector2D::crossProduct(second - first, other.first - first) == 0))
            return false;

        if (onSegment(first, other.second, second) && (GVector2D::crossProduct(second - first, other.second - first) == 0))
            return false;

        if (onSegment(other.first, first, other.second) && (GVector2D::crossProduct(other.second - other.first, first - other.first) == 0))
            return false;

        if (onSegment(other.first, second, other.second) && (GVector2D::crossProduct(other.second - other.first, second - other.first) == 0))
            return false;

        return true;
    }

    const GVector2D& p1 = first;
    const GVector2D& q1 = second;
    const GVector2D& p2 = other.first;
    const GVector2D& q2 = other.second;

    // Find the four orientations needed for general and 
    // special cases 
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);

    // General case 
    if (o1 != o2 && o3 != o4)
        return true;

    // Special Cases 
    // p1, q1 and p2 are colinear and p2 lies on segment p1q1 
    if (o1 == 0 && onSegment(p1, p2, q1)) return true;

    // p1, q1 and q2 are colinear and q2 lies on segment p1q1 
    if (o2 == 0 && onSegment(p1, q2, q1)) return true;

    // p2, q2 and p1 are colinear and p1 lies on segment p2q2 
    if (o3 == 0 && onSegment(p2, p1, q2)) return true;

    // p2, q2 and q1 are colinear and q1 lies on segment p2q2 
    if (o4 == 0 && onSegment(p2, q1, q2)) return true;

    return false; // Doesn't fall in any of the above cases 
}

std::optional<GVector2D> Segment2D::getIntersectionPoint(const Segment2D& other) const
{
    gte::FIQuery<float, gte::Segment2<float>, gte::Segment2<float>> query;
    const auto seg1 = gte::Segment2<float>{ GtoV2(first), GtoV2(second) };
    const auto seg2 = gte::Segment2<float>{ GtoV2(other.first), GtoV2(other.second) };
    const auto result = query(seg1, seg2);

    if (result.intersect && result.numIntersections == 1)
        return {VtoG2(result.point[0])};

    return std::nullopt;
}

float Segment2D::length() const
{
    return (first - second).length();
}

// end of extern impl
// ***********************************************************************************

float Segment2D::dist(const GVector2D& point) const
{
    float ret = std::min(point.dist(first), point.dist(second));
    if (GVector2D::dotProduct((second - first).normalized(), (point - first).normalized()) > 0 &&
        GVector2D::dotProduct((first - second).normalized(), (point - second).normalized()) > 0)
        ret = std::min(ret, ((QVector3D)point).distanceToLine(first, (second - first).normalized()));
    return ret;
}

float Segment2D::dist(const Segment2D& segment) const
{
    return std::get<float>(distance(std::array<GVector2D, 2>{ this->first, this->second }, std::array<GVector2D, 2>{ segment.first, segment.second}, false));
}

GVector2D Segment2D::closestPoint(const GVector2D& inPoint) const
{
    const GVector2D segVector = this->second - this->first;
    float t = GVector2D::dotProduct((inPoint - this->first), segVector);

    if (t <= 0.0f)
    {
        return this->first;
    }

    const float denominator = GVector2D::dotProduct(segVector, segVector);
    if (t >= denominator)
        return this->second;

    t /= denominator;
    return GVector2D{ this->first + GVector2D(t * segVector) };
}

bool Segment2D::hasPoint(const GVector2D& inPoint) const
{
    const float cross = GVector2D::crossProduct({ this->second - this->first }, { inPoint - this->first });
    if (!std::is_eq(fCmp(cross, 0)))
        return false;

    const float dot = GVector2D::dotProduct({ this->second - this->first }, { inPoint - this->first });
    if (dot < 0.f)
        return false;

    const float squareDistance = (second.x - first.x) * (second.x - first.x) + (second.z - first.z) * (second.z - first.z);
    if (dot > squareDistance)
        return false;

    return true;
}

bool vContains(const std::vector<QVector3D>& container, const QVector3D& value)
{
    for (auto&& p : container)
        if (vEq(p, value))
            return true;

    return false;
}

std::tuple<std::vector<QVector3D>, std::vector<uint>, std::vector<IndexType>, std::vector<IndexType>, std::vector<Segment2D>> computePerimeterForSquares(const QSet<GPoint>& squares)
{
    std::vector<QVector3D> vertices;
    std::vector<uint> indices;
    std::vector<IndexType> wireframeIndices;
    std::vector<IndexType> boundsIndices;
    std::vector<Segment2D> perimeter;

    // Find all faces with same x facing south (-Z) and north (+Z);
    // Find all faces with same z facing east (-X) and west (+X);
    QMap<int, std::vector<int>> southFacingSquares, northFacingSquares, eastFacingSquares, westFacingSquares;
    for (auto&& [x, z] : squares)
    {
        // Insert into sorted vectors.
        if (squares.find({ x, z - 1 }) == squares.end())
        {
            auto& target = southFacingSquares[z];
            target.insert(std::upper_bound(target.begin(), target.end(), x), x);
        }
        if (squares.find({ x, z + 1 }) == squares.end())
        {
            auto& target = northFacingSquares[z];
            target.insert(std::upper_bound(target.begin(), target.end(), x), x);
        }
        if (squares.find({ x - 1, z }) == squares.end())
        {
            auto& target = eastFacingSquares[x];
            target.insert(std::upper_bound(target.begin(), target.end(), z), z);
        }
        if (squares.find({ x + 1, z }) == squares.end())
        {
            auto& target = westFacingSquares[x];
            target.insert(std::upper_bound(target.begin(), target.end(), z), z);
        }
    }

#define xyz QVector3D(x * GRID_SEGMENT_WIDTH, 0, z * GRID_SEGMENT_WIDTH)
#define Xyz QVector3D(X * GRID_SEGMENT_WIDTH, 0, z * GRID_SEGMENT_WIDTH)
#define XyZ QVector3D(X * GRID_SEGMENT_WIDTH, 0, Z * GRID_SEGMENT_WIDTH)
#define xyZ QVector3D(x * GRID_SEGMENT_WIDTH, 0, Z * GRID_SEGMENT_WIDTH)
#define xYz QVector3D(x * GRID_SEGMENT_WIDTH, domainHeight, z * GRID_SEGMENT_WIDTH)
#define XYz QVector3D(X * GRID_SEGMENT_WIDTH, domainHeight, z * GRID_SEGMENT_WIDTH)
#define XYZ QVector3D(X * GRID_SEGMENT_WIDTH, domainHeight, Z * GRID_SEGMENT_WIDTH)
#define xYZ QVector3D(x * GRID_SEGMENT_WIDTH, domainHeight, Z * GRID_SEGMENT_WIDTH)

    // Make the final faces.
    const float domainHeight = GRID_SEGMENT_WIDTH * 0.5f;

    auto addToPerimeter = [&perimeter](std::array<QVector3D, 2> verts)
    {
        std::sort(verts.begin(), verts.end());
        perimeter << Segment2D{ GVector2D(verts[0].x(), verts[0].z()), GVector2D(verts[1].x(), verts[1].z()) };
    };

#define x beginX
#define X (endX+1)
#define Z (z+1)
    for (auto it = southFacingSquares.keyValueBegin(); it != southFacingSquares.keyValueEnd(); ++it)
    {
        auto&& [z, xs] = *it;

        for (int i = 0; i < xs.size(); ++i)
        {
            // Mark the beginning of a face
            int beginX = xs[i];
            int endX = beginX;

            // Find the face end.
            for (int j = i + 1; j < xs.size(); ++j)
                if (xs[j] == (endX + 1))
                {
                    ++endX;
                    i = j;
                }

            // Append face.
            vertices << Xyz << XYz << xYz << xyz;
            addToPerimeter({ Xyz, xyz });

            vertices[vertices.size() - 4] += QVector3D(-DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 3] += QVector3D(-DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 2] += QVector3D(DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 1] += QVector3D(DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);

            appendFace(indices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(wireframeIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(boundsIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 1) });
        }
    }
    for (auto it = northFacingSquares.keyValueBegin(); it != northFacingSquares.keyValueEnd(); ++it)
    {
        auto&& [z, xs] = *it;

        for (int i = 0; i < xs.size(); ++i)
        {
            // Mark the beginning of a face
            int beginX = xs[i];
            int endX = beginX;

            // Find the face end.
            for (int j = i + 1; j < xs.size(); ++j)
                if (xs[j] == (endX + 1))
                {
                    ++endX;
                    i = j;
                }

            // Append face.
            vertices << xyZ << xYZ << XYZ << XyZ;
            addToPerimeter({ xyZ, XyZ });

            vertices[vertices.size() - 4] += QVector3D(DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 3] += QVector3D(DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 2] += QVector3D(-DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 1] += QVector3D(-DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);

            appendFace(indices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(wireframeIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(boundsIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 1) });
        }
    }
#undef x
#undef Z
#undef X

#define z beginZ
#define Z (endZ+1)
#define X (x+1)
    for (auto it = eastFacingSquares.keyValueBegin(); it != eastFacingSquares.keyValueEnd(); ++it)
    {
        auto&& [x, zets] = *it;

        for (int i = 0; i < zets.size(); ++i)
        {
            // Mark the beginning of a face
            int beginZ = zets[i];
            int endZ = beginZ;

            // Find the face end.
            for (int j = i + 1; j < zets.size(); ++j)
                if (zets[j] == (endZ + 1))
                {
                    ++endZ;
                    i = j;
                }

            // Append face.
            vertices << xyz << xYz << xYZ << xyZ;
            addToPerimeter({ xyz, xyZ });

            vertices[vertices.size() - 4] += QVector3D(DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 3] += QVector3D(DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 2] += QVector3D(DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 1] += QVector3D(DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);

            appendFace(indices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(wireframeIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(boundsIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 1) });
        }
    }
    for (auto it = westFacingSquares.keyValueBegin(); it != westFacingSquares.keyValueEnd(); ++it)
    {
        auto&& [x, zets] = *it;

        for (int i = 0; i < zets.size(); ++i)
        {
            // Mark the beginning of a face
            int beginZ = zets[i];
            int endZ = beginZ;

            // Find the face end.
            for (int j = i + 1; j < zets.size(); ++j)
                if (zets[j] == (endZ + 1))
                {
                    ++endZ;
                    i = j;
                }

            // Append face.
            vertices << XyZ << XYZ << XYz << Xyz;
            addToPerimeter({ XyZ, Xyz });

            vertices[vertices.size() - 4] += QVector3D(-DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 3] += QVector3D(-DOMAIN_INNER_MARGIN, 0, -DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 2] += QVector3D(-DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);
            vertices[vertices.size() - 1] += QVector3D(-DOMAIN_INNER_MARGIN, 0, DOMAIN_INNER_MARGIN);

            appendFace(indices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(wireframeIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 3), IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) }, true);
            appendLines(boundsIndices, { IndexType(vertices.size() - 4), IndexType(vertices.size() - 1) });
        }
    }
#undef z
#undef Z
#undef X

#undef xyz
#undef Xyz
#undef xYz
#undef xyZ
#undef xYZ
#undef XyZ
#undef XYz
#undef XYZ

    return std::make_tuple(vertices, indices, wireframeIndices, boundsIndices, perimeter);
}

QVector3D computeFaceNormal(const std::array<QVector3D, 3>& face)
{
    return -QVector3D::crossProduct((face[1] - face[0]), (face[face.size() - 1] - face[0])).normalized();
}

// Source: https://gamedev.net/forums/topic/621589-extremely-fast-sin-approximation/4920989/
double fastSin(double x) 
{
    int k;
    double y;
    double z;

    z = x;
    z *= 0.3183098861837907;
    z += 6755399441055744.0;
    k = *((int*)&z);
    z = k;
    z *= 3.1415926535897932;
    x -= z;
    y = x;
    y *= x;
    z = 0.0073524681968701;
    z *= y;
    z -= 0.1652891139701474;
    z *= y;
    z += 0.9996919862959676;
    x *= z;
    k &= 1;
    k += k;
    z = k;
    z *= x;
    x -= z;

    return x;
}

double fastCos(double x)
{
    return fastSin(std::numbers::pi * 0.5 + x);
}

double fastTan(double x)
{
    const float cosX = fastCos(x);
    return isZero(cosX) ? 0.f : fastSin(x) / cosX;
}

float getRandomFloat(float min, float max)
{
    return std::uniform_real_distribution<>(min, max)(Generation::gRandomEngine);
}

int getRandomInt(int min, int max)
{
    return std::uniform_int_distribution<>(min, max)(Generation::gRandomEngine);
}

bool GPoint::contains(const GVector2D& pt, bool includeEdge /*= true*/, float width /*= GRID_SEGMENT_WIDTH*/) const
{
    float gp_x = x * width;
    float gp_X = x * width + width;
    float gp_z = z * width;
    float gp_Z = z * width + width;

    bool xok = (pt.x > gp_x) && (pt.x < gp_X);
    if (includeEdge)
        xok |= ((pt.x == gp_x) || (pt.x == gp_X));

    bool zok = (pt.z >= gp_z) && (pt.z <= gp_Z);
    if (includeEdge)
        zok |= ((pt.z == gp_z) || (pt.z == gp_Z));

    return xok && zok;
}

bool GPoint::isNeighbor(const GPoint& other, bool includeCorners) const
{
    if (other == *this)
        return false;

    int xDiff = qAbs(x - other.x);
    int zDiff = qAbs(z - other.z);

    bool softNeighbor = (xDiff <= 1) && (zDiff <= 1);
    if (includeCorners)
        return softNeighbor;
    else
        return softNeighbor && (xDiff + zDiff == 1);
}

GVector2D GPoint::midPoint() const
{
    return {x * GRID_SEGMENT_WIDTH + GRID_SEGMENT_WIDTH  * 0.5f, z * GRID_SEGMENT_WIDTH + GRID_SEGMENT_WIDTH  * 0.5f};
}


GPoint GPoint::clamp(const GPoint& min, const GPoint& max) const
{
    return { std::clamp(x, min.x, max.x), std::clamp(z, min.z, max.z) };
}

QSet<GPoint> GPoint::shaveMargin(const QSet<GPoint>& set)
{
    QSet<GPoint> toShave;

    for (auto&& sq : set)
        if (!set.contains({ sq.x + 1, sq.z }) || !set.contains({ sq.x - 1, sq.z }) || !set.contains({ sq.x, sq.z + 1 }) || !set.contains({ sq.x, sq.z - 1 }))
            toShave += sq;

    return set - toShave;
}

QSet<GPoint> GPoint::growMargin(const QSet<GPoint>& set)
{
    QSet<GPoint> newSet;

    for (int x = -1; x <= 1; x++)
        for (int z = -1; z <= 1; z++)
            for (auto&& sq : set)
                newSet += GPoint(sq.x + x, sq.z + z);

    return newSet;
}

// Cubic Hermite interpolation (https://en.wikipedia.org/wiki/Cubic_Hermite_spline)
float cubicInterpolation(float h0, float h1, float h2, float h3, float t)
{
    float m1 = (h2 - h0) / 2; // tangent for left point
    float m2 = (h3 - h1) / 2; // tangent for right point
    return h1 * (2 * t * t * t - 3 * t * t + 1) +
        h2 * (t * t * (3 - 2 * t)) +
        m1 * (t * (t * t - 2 * t + 1)) +
        m2 * (t * t * (t - 1));
}

// Derivative of cubic hermite curve
float cubicDerivative(float h0, float h1, float h2, float h3, float t)
{
    float m1 = (h2 - h0) / 2; // tangent for left point
    float m2 = (h3 - h1) / 2; // tangent for right point
    return h1 * (6 * t * (t - 1)) +
        h2 * (6 * t * (1 - t)) +
        m1 * (3 * t * t - 4 * t + 1) +
        m2 * (t * (3 * t - 2));
}

N_Ellipse::N_Ellipse(const std::vector<GVector2D>& inFoci, float r) 
    : foci(inFoci)
{
    GVector2D center;
    for (auto&& focus : foci)
        center += focus;

    center /= foci.size();
    radius = r;

    for (auto&& focus : foci)
        radius += distance(focus, center);
}

bool N_Ellipse::contains(const GVector2D& p)
{
    float distanceSum = 0.0f;
    for (auto&& focus : foci)
        distanceSum += distance(p, focus);

    return distanceSum <= radius;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------

BezierCurve2D::BezierCurve2D(const std::vector<GVector2D>& pts)
{
    std::vector<gte::Vector<2, double>> curvePts;
    curvePts.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        curvePts <<= gte::Vector<2, double>({pts[i].x, pts[i].z});
    curve = std::make_unique<gte::BezierCurve<2, double>>((int)curvePts.size() - 1, curvePts.data());
}

GVector2D BezierCurve2D::evaluate(double t) const
{
    gte::Vector<2, double> p;
    curve->Evaluate(t, 0, &p);
    return GVector2D(p[0], p[1]);
}

std::vector<GVector2D> BezierCurve2D::getPoints(int numSteps) const
{
    std::vector<GVector2D> result;
    result.reserve(numSteps + 1);
    for (int i = 0; i <= numSteps; ++i)
        result <<= evaluate(i / (double)numSteps);
    return result;
}

float BezierCurve2D::getLength(int approxPtsCount) const
{
    const std::vector<GVector2D> pts = getPoints(approxPtsCount);
    float length = 0.f;
    for (int i = 0; i < pts.size() - 1; ++i)
        length += (pts[i + 1] - pts[i]).length();
    return length;
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------
BSplineCurve::BSplineCurve(const std::vector<QVector3D>& pts, int degree)
{
    samplePts.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        samplePts <<= gte::Vector3<float>({pts[i].x(), pts[i].y(), pts[i].z()});

    spline = std::make_unique<gte::BSplineCurveFit<float>>(3, static_cast<int32_t>(samplePts.size()),
        reinterpret_cast<float const*>(&samplePts[0]), degree, std::max(degree + 1, (int)pts.size() / 2));
}

QVector3D BSplineCurve::evaluate(float t) const
{
    gte::Vector3<float> p;
    spline->GetPosition(t, reinterpret_cast<float*>(&p));
    return QVector3D(p[0], p[1], p[2]);
}

std::vector<QVector3D> BSplineCurve::getPoints(int numSteps) const
{
    std::vector<QVector3D> result;
    result.reserve(numSteps + 1);
    for (int i = 0; i <= numSteps; ++i)
        result <<= evaluate(i / (float)numSteps);
    return result;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------

