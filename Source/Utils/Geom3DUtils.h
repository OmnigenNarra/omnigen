#pragma once

#include "Utils/CoreUtils.h"


namespace geom3dUtils
{
    struct Segment3D : QPair<QVector3D, QVector3D>
    {
        Segment3D() = default;
        Segment3D(const QVector3D& a, const QVector3D& b) : QPair<QVector3D, QVector3D>{ a, b } {};

        float length() const;
        float lengthSq() const;

        float distToPoint(const QVector3D& point, bool return_squared = false) const;
        std::tuple<float /*distance*/, QVector3D /*projection*/> distFromPointToInfiniteLine(const QVector3D& point, bool return_squared = false) const;

        inline QVector3D midpoint() const { return (first + second) * 0.5f; }

    };


    std::tuple<float /*distance*/, QVector3D /*projection*/> distanceFromPointToInfiniteLine(const QVector3D& point, const QVector3D& line_pt1, const QVector3D& line_pt2, bool return_squared = false);

    float distFromPointToSegment(const QVector3D& point, const QVector3D& seg_start, const QVector3D& seg_end, bool return_squared = false);

    std::optional<GVector2D> raycastDemApproximately(const QVector3D& from, const QVector3D& dir, float minStep = 100.f, float step = 3000.f);


}
