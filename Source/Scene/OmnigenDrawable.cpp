#include "stdafx.h"
#include "OmnigenDrawable.h"
#include <QDateTime>
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Utils/CoreUtils.h"
#include <QOpenGLFunctions>

OmnigenDrawable::OmnigenDrawable()
    : guid(makeGuid())
{
}

void OmnigenDrawable::updateCullStatus(const OmnigenCamera& camera, int vIdx)
{
    bIsCulled = !(shouldDraw(vIdx) && camera.isBoxInFrustum(getBoundingBox()));
}

void OmnigenDrawable::updateCullStatusInstanced(const OmnigenCamera& camera, int vIdx)
{
    if (!shouldDraw(vIdx))
    {
        bIsCulled = true;
        return;
    }

    auto&& geom = getActiveBaseGeometry();

    std::mutex guard;
    std::vector<quint32> visibleInstances;
    tbb::parallel_for(0, int(geom->instanceCount()), [&](int i)
        {
            auto&& transform = geom->getInstanceTransform(i);
            auto pos = QVector3D(transform(0, 3), transform(1, 3), transform(2, 3));
            if (distance(camera.getPosition(), pos) < getCullDistance())
                if (camera.isBoxInFrustum(getBoundingBox().transformed(transform)))
                {
                    std::scoped_lock lock(guard);
                    visibleInstances << i;
                }
        });

    geom->setVisibleInstances(visibleInstances);
    bIsCulled = visibleInstances.empty();
}

void OmnigenDrawable::setActiveLOD(ELOD ll)
{
    while (!geometry.contains(ll))
        ll = ELOD(int(ll) + 1);

    activeLOD = ll;
}

void OmnigenDrawable::setGuid(qint64 id)
{
    guid = id;
}

void OmnigenDrawable::initialize()
{
    createShader();
    createShaderResources();

    if (!geometry.contains(ELOD::Last))
        createDefaultLodLevel();

    Q_ASSERT(geometry.contains(ELOD::Last));

    // Data is loaded, only update vbos.
    // Update Vbo has to run on main thread
    for (ELOD ll = ELOD::Zero; ll <= ELOD::Last; ll = ELOD(int(ll) + 1))
        if (geometry.contains(ll))
            updateVbo(ll);
}

void OmnigenDrawable::assignLodLevel(ELOD ll, QSharedPointer<GeometryDataBase> newData)
{
    geometry[ll] = newData;
}

void OmnigenDrawable::clearLodLevel(ELOD ll)
{
    geometry.remove(ll);
}

void OmnigenDrawable::updateVbo(ELOD ll)
{
    if (geometry[ll].isNull())
        return;

    geometry[ll]->fillVbo();
}
