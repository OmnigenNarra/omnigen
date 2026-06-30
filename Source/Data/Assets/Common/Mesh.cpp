#include "stdafx.h"
#include "Mesh.h"

void MeshAssetBase::setMesh(const MeshComponent& inMesh)
{
    mesh = inMesh;
}

void omniSave(const MeshAssetBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const OmnigenAssetBase&>(object);
    omniBin << object.mesh;
}

void omniLoad(MeshAssetBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<OmnigenAssetBase&>(object);
    omniBin >> object.mesh;
}

void MeshComponent::setGeometry(ELOD lod, const QSharedPointer<MeshAssetGeometry>& inGeometry)
{
    geometry[lod] = inGeometry;
}

void MeshComponent::setScaleRange(const std::array<float, 2>& inRange)
{
    scaleRange = inRange;
}

void omniSave(const MeshComponent& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.geometry;
    omniBin << object.scaleRange;
}

void omniLoad(MeshComponent& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.geometry;
    omniBin >> object.scaleRange;
}