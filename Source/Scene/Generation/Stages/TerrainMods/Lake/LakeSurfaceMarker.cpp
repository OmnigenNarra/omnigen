#include "stdafx.h"
#include "LakeSurfaceMarker.h"

DLakeSurfaceMarker::DLakeSurfaceMarker(const Polygon2D& poly, float height)
    : DPolygonMarker({ poly.getPts().begin(), poly.getPts().end() }, height, QVector4D(0, 0.5, 1, 0.35))
{
}