#pragma once
#include "MarkerDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"
#include "Utils/SerializableNurbs.h"
#include "Utils/Polygon.h"

// Converts parametric NURBS to exact mesh in constructor
// See also generateNurbMesh
class DNurbsMarker : public DSharedMeshMarker<>
{
public:
	DNurbsMarker() = default;
	DNurbsMarker(const SerializableNurbs<3, float>& inNurbSurface, int rowsN, int columnsN,
				 const QVector4D& inColor = QVector4D(1, 1, 1, 1), float renderHeightOffset = 0.0f, ERenderType inRenderMode = ERenderType::Wireframe);

    const auto& getSurface() const { return nurbSurface; }

protected:
	int rows;
	int columns;
	SerializableNurbs<3, float> nurbSurface;

	FRIEND_OMNIBIN(DNurbsMarker);
};

template<template<typename T = QVector3D> typename GeometryType>
QSharedPointer<GeometryType<>> generateNurbMesh(const gte::NURBSSurface<3, float>& nurb, int rows, int cols)
{
    auto geometry = QSharedPointer<GeometryType<>>::create();
    auto & vertices = geometry->vertices;
    auto& indices = geometry->indices;

    QVector3D pt;
    gte::Vector<3, float> p;
    for (float x = 0; x <= rows; x += 1.f)
    {
        for (float y = 0; y <= cols; y += 1.f)
        {
            nurb.Evaluate(1.f / rows * x, 1.f / cols * y, 0, &p);
            pt.setX(p[0]);
            pt.setY(p[1]);
            pt.setZ(p[2]);
            vertices << pt;
        }
    }

    for (int l = 0; l < rows; ++l)
        for (int k = 0; k < cols; ++k)
        {
            indices << k + (l * (cols + 1));
            indices << k + ((l + 1) * (cols + 1));
            indices << k + ((l + 1) * (cols + 1)) + 1;
            indices << k + (l * (cols + 1)) + 1;
        }

    return geometry;
}

void omniSave(const DNurbsMarker& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DNurbsMarker& object, OmniBin<std::ios::in>& omniBin);