#include "stdafx.h"
#include "ManipulationGizmoSelection.h"

namespace Design
{
    void SelectionManipulationGizmo::endGizmoMove()
    {
        DManipulationGizmo::get()->updateArrowPoints();
    }

    std::optional<EArrowType> SelectionManipulationGizmo::checkIfGizmo(int mousePosX, int mousePosY)
    {
        auto axis = DManipulationGizmo::get()->isMouseOverGizmo(mousePosX, mousePosY);
        if (axis)
            return *axis;

        return {};
    }

    void SelectionManipulationGizmo::showGizmo(QVector3D newPos, bool drawX /*= true*/, bool drawY /*= true*/, bool drawZ /*= true*/)
    {
        DManipulationGizmo::get()->showAtPos(newPos, drawX, drawY, drawZ);
    }

    void SelectionManipulationGizmo::grabAxis(int mousePosX, int mousePosY, EArrowType arrow)
    {
        DManipulationGizmo::get()->grabGizmoAxis(mousePosX, mousePosY, arrow);
    }

    QVector3D SelectionManipulationGizmo::moveGizmo(int mousePosX, int mousePosY)
    {
        return DManipulationGizmo::get()->moveAlongAxis(mousePosX, mousePosY);
    }
}
