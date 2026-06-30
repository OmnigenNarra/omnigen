#pragma once
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Utils/Polygon.h"

class DLakeSurfaceMarker : public DPolygonMarker
{
public:
    DLakeSurfaceMarker(const Polygon2D& poly, float height);

    IMPLEMENT_SHOULD_DRAW();
};
