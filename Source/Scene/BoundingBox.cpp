#include "stdafx.h"
#include "BoundingBox.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"

BoundingBox BoundingBox::merge(const BoundingBox& BB1, const BoundingBox& BB2)
{
    BoundingBox result = BB1;
    result.expandToContain(BB2.nbl);
    result.expandToContain(BB2.nbl + BB2.sizes);

    return result;
}

BoundingBox BoundingBox::transformed(const QMatrix4x4& transform) const
{
    BoundingBox result;
    float scale = transform(1, 1);
    result.sizes = sizes * std::numbers::sqrt2 * scale;

    auto transformedCenter = transform * (nbl + sizes * 0.5);
    result.nbl = transformedCenter - result.sizes * 0.5;

    return result;
}

void BoundingBox::expandToContain(const QVector3D& p)
{
    if (sizes.isNull())
    {
        nbl = p - QVector3D(1, 1, 1);
        sizes = { 2,2,2 };
        return;
    }

    auto ftr = nbl + sizes;

    // Enlarge bounds a little bit to prevent misses with corner cases
    constexpr float enlargeOffset = 0.1f;

    if (p.x() < nbl.x() + enlargeOffset)
        nbl.setX(p.x() - enlargeOffset);
    else if (p.x() > ftr.x() - enlargeOffset)
        ftr.setX(p.x() + enlargeOffset);

    if (p.y() < nbl.y() + enlargeOffset)
        nbl.setY(p.y() - enlargeOffset);
    else if (p.y() > ftr.y() - enlargeOffset)
        ftr.setY(p.y() + enlargeOffset);

    if (p.z() < nbl.z() + enlargeOffset)
        nbl.setZ(p.z() - enlargeOffset);
    else if (p.z() > ftr.z() - enlargeOffset)
        ftr.setZ(p.z() + enlargeOffset);

    sizes = ftr - nbl;
}

void BoundingBox::show()
{
    auto& v = nbl;
    auto V = nbl + sizes;

    // bot
    QVector3D xyz = { v.x(), v.y(), v.z() };
    QVector3D Xyz = { V.x(), v.y(), v.z() };
    QVector3D XyZ = { V.x(), v.y(), V.z() };
    QVector3D xyZ = { v.x(), v.y(), V.z() };

    // top
    QVector3D xYz = { v.x(), V.y(), v.z() };
    QVector3D XYz = { V.x(), V.y(), v.z() };
    QVector3D XYZ = { V.x(), V.y(), V.z() };
    QVector3D xYZ = { v.x(), V.y(), V.z() };

    std::vector<std::vector<QVector3D>> lines;
    lines.push_back(std::vector{ xyz, Xyz, XyZ, xyZ, xyz});
    lines.push_back(std::vector{ xYz, XYz, XYZ, xYZ, xYz});

    lines.push_back(std::vector{ xyz, xYz });
    lines.push_back(std::vector{ Xyz, XYz });
    lines.push_back(std::vector{ XyZ, XYZ });
    lines.push_back(std::vector{ xyZ, xYZ });

    spawn<DMultiLineMarker>(lines);
}
