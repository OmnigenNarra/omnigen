#pragma once
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Omnigen.h"
#include "Utils/ManipulationGizmo.h"

class DManipulationGizmo;

namespace Design
{
    class SelectionManipulationGizmo : public SelectionBase
    {
    public:
        virtual void grabGizmo(int mousePosX, int mousePosY) {};
        virtual void moveObject(int mousePosX, int mousePosY) {};
        virtual void endGizmoMove();

    protected:
        static std::optional<EArrowType> checkIfGizmo(int mousePosX, int mousePosY);
        static void showGizmo(QVector3D newPos, bool drawX = true, bool drawY = true, bool drawZ = true);
        void grabAxis(int mousePosX, int mousePosY, EArrowType arrow);

        // Moves the gizmo to match cursor position, and return the gizmo's movement made
        QVector3D moveGizmo(int mousePosX, int mousePosY);
    };
}