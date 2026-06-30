#pragma once
#include "Source/Utils/GeometryData.h"
#include "Utils/OmniBin/OmniBin.h"

struct MeshAssetVertex
{
    QVector3D position = { -1.f, -1.f, -1.f };
    QVector3D normal = { 0.f, -1.f, 0.f };
    QVector2D uv = { -1.f, -1.f };
    int materialId = 0;
};

template<>
constexpr bool serializeAsPOD<MeshAssetVertex> = true;

struct MeshAssetInstanceData
{
    QMatrix4x4 world;
    QVector3D surfaceNormal;
};

template<>
constexpr bool serializeAsPOD<MeshAssetInstanceData> = true;

using MeshAssetGeometry = InstancedRenderGeometryData<MeshAssetVertex, MeshAssetInstanceData>;

struct VertexAttributeDesc
{
    int byteOffset;
    decltype(GL_FLOAT) componentType;
    int componentCount;
};
