#pragma once
#include "qevent.h"
#include "Utils/QuadTreeLite.h"

class DLineMarker;

namespace Design::DrawUtils
{
    std::optional<QVector3D> findPoint(QPoint mousePosition);
    void clearBrush(QSharedPointer<DLineMarker>& brushMarker);
    void drawBrushCircle(QMouseEvent* mEvent, int circleDetail, float brushRadius, QSharedPointer<DLineMarker>& brushMarker);

    std::vector<const tml::node<float, IndexType>*> getCellsUnderBrush(int centerCell, float brushRadius);
}
