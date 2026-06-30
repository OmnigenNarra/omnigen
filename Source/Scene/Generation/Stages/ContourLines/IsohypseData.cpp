#include "stdafx.h"
#include "IsohypseData.h"
#include "IsohypseBatchingMarker.h"

DIsohypseBound::DIsohypseBound(const std::vector<QVector3D>& inControlPoints, const QVector4D color)
    : DLineMarker(inControlPoints, color)
{
}

bool DIsohypseBound::isCrossing(QSharedPointer<Isohypse> ih) const
{
    auto ihc = ih->getCircularPoints();
    auto boundCircular = asCircular(getControlPoints());

    for (int i = 0; i < ihc.getSize(); ++i)
    {
        int i2 = ihc.findIdx(i, 1);
        GVector2D ihPointX({ ihc[i].x(), ihc[i].z() });
        GVector2D ihPointZ({ ihc[i2].x(), ihc[i2].z() });
        Segment2D ihSegment({ ihPointX, ihPointZ });

        for (int j = 0; j < boundCircular.getSize(); ++j)
        {
            int j2 = boundCircular.findIdx(j, 1);
            Segment2D controlSegment({ boundCircular[j].x(), boundCircular[j].z() }, { boundCircular[j2].x(), boundCircular[j2].z() });

            if (controlSegment.intersects(ihSegment, true))
                return true;
        }
    }

    return false;
}

void IHProtoData::computeMergingData() const
{
    if (radius > 0)
        return;

    for (auto&& p : pts)
        center += p;

    center /= float(pts.size());

    for (auto&& p : pts)
        radius = std::max(radius, distance(center, GVector2D(p)));

    for (auto&& inc : increments)
        maxIncrement = std::max(maxIncrement, inc);
}
