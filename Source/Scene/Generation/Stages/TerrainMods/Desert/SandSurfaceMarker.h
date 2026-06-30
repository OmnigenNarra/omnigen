#pragma once
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Utils/Polygon.h"

const QVector4D sandColor = QVector4D(0.95f, 0.64f, 0.37f, 0.5f);

class DSandSurfaceMarker : public DPolygonMarker
{
public:

    DSandSurfaceMarker(const std::vector<QVector3D>& inControlPoints, float height = 0.f)
        : DPolygonMarker(inControlPoints, height, sandColor)
    {
    }

    DSandSurfaceMarker(std::vector<QVector3D>&& inControlPoints, float height = 0.f)
        : DPolygonMarker(std::move(inControlPoints), height, sandColor)
    {
    }

    IMPLEMENT_SHOULD_DRAW();
};
