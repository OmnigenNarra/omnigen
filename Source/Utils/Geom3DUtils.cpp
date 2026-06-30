#include "stdafx.h"
#include "Geom3DUtils.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"


namespace geom3dUtils
{

float Segment3D::length() const
{
    return std::hypot(first.x() - second.x(), first.y() - second.y(), first.z() - second.z());
}


float Segment3D::lengthSq() const
{
    return powf(first.x() - second.x(), 2) + powf(first.x() - second.x(), 2) + powf(first.z() - second.z(), 2);
}


float Segment3D::distToPoint(const QVector3D& point, bool return_squared) const
{
    return distFromPointToSegment(point, first, second, return_squared);
}


std::tuple<float /*distance*/, QVector3D /*projection*/> Segment3D::distFromPointToInfiniteLine(const QVector3D& point, bool return_squared) const
{
    return distanceFromPointToInfiniteLine(point, first, second, return_squared);
}


std::tuple<float /*distance*/, QVector3D /*projection*/> distanceFromPointToInfiniteLine(const QVector3D& point, const QVector3D& line_pt1, const QVector3D& line_pt2, bool return_squared)
{
    const float l2 = distanceSquared(line_pt1, line_pt2);
    if (isZero(l2))
        return {(return_squared ? distanceSquared(point, line_pt1) : distance(point, line_pt1)), line_pt1 };  // line_pt1 == line_pt2 case

    if (vEq(point, line_pt1))
        return {0.f, line_pt1};

    const float t = QVector3D::dotProduct(point - line_pt1, line_pt2 - line_pt1) / (sqrtf(l2) * distance(point, line_pt1));
    const QVector3D projection = std::lerp(line_pt1, line_pt2, t);
    return {(return_squared ? distanceSquared(point, projection) : distance(point, projection)), projection };
}


float distFromPointToSegment(const QVector3D& point, const QVector3D& seg_start, const QVector3D& seg_end, bool return_squared)
{
    const float l2 = distanceSquared(seg_start, seg_end);
    if (isZero(l2))
        return (return_squared ? distanceSquared(point, seg_start) : distance(point, seg_start));  // seg_start == seg_end case

    if (vEq(point, seg_start))
        return 0.f;

    const float t = QVector3D::dotProduct(point - seg_start, seg_end - seg_start) / (sqrtf(l2) * distance(point, seg_start));
    if (t < 0.f)
        return (return_squared ? distanceSquared(point, seg_start) : distance(point, seg_start));
    else if ( t > 1.f)
        return (return_squared ? distanceSquared(point, seg_end) : distance(point, seg_end));

    const QVector3D projection = std::lerp(seg_start, seg_end, t);
    return return_squared ? distanceSquared(point, projection) : distance(point, projection);
}


std::optional<GVector2D> raycastDemApproximately(const QVector3D& from, const QVector3D& dir, float minStep, float step)
{
    const auto& dem = Generation::Data::get()->getDEM();

    QVector3D prev = from;
    QVector3D curr = from;
    while (true)
    {
        if (dem->heightData.sample(curr) > curr.y())
        {
            if (curr == from)
                return {from};

            const float newStep = 0.2f * step;
            if (newStep < minStep)
                return {(GVector2D)((prev + curr) * 0.5f)};
            else
                return raycastDemApproximately(prev, dir, minStep, newStep);
        }

        if (!dem->heightData.getBoundingBox().contains(curr))
            return std::nullopt;

        prev = curr;
        curr = curr + dir * step;
    }

    return std::nullopt;
}


} //namespace geom3dUtils
