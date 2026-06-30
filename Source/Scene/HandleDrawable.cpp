#include "stdafx.h"
#include "HandleDrawable.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"

void DHandle::cacheBoundingBox()
{
    static const QVector3D v = { getSpriteSize(), getSpriteSize(), getSpriteSize() };
    cachedBoundingBox = { getPosition() - (v * 0.5), v };
}

void DHandle::setSelected(bool b)
{
    bIsSelected = b;
}

const QVector3D& DHandle::getPosition() const
{
    static QVector3D errorPos(-1, -1, -1);
    auto&& vertices = getActiveGeometry()->vertices;
    return vertices.size() ? vertices.front() : errorPos;
}

void DHandle::adjustPosition(const QVector3D& delta)
{
    prePositionChange();

    auto& vertices = getActiveGeometry()->vertices;
    vertices.front() += delta;
    updateVbo(activeLOD);
    hasAdjustedPosition = true;

    postPositionChange();
}

void DHandle::createDefaultLodLevel()
{
    assignLodLevel(ELOD::Last, QSharedPointer<RenderGeometryData<>>::create());
}
