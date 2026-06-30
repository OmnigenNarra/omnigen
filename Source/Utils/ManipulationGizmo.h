#pragma once
#include "Scene/OmnigenDrawable.h"

namespace Design
{
    class SelectionManipulationGizmo;
}

enum class EArrowType
{
    YArrow,
    XArrow,
    ZArrow
};

class DManipulationGizmo : public OmnigenDrawable
{
    static inline DManipulationGizmo* sInstance = nullptr;

    static inline ShaderPipeline shaderPipeline;
    static inline GLfloat lastLineWidth;

public:
    static inline DManipulationGizmo* get()
    {
        if (!sInstance)
            sInstance = new DManipulationGizmo();

        return sInstance;
    }

    DManipulationGizmo() = default;

    //Drawable
    virtual void updateCullStatus(const OmnigenCamera& camera, int vIdx) override { bIsCulled = !shouldDraw(vIdx); };
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Gizmo; };
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void draw() override;
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void unbindShader() override;
    virtual void cacheBoundingBox() override;
    virtual bool shouldDraw(int vIdx) const override { return bVisible; }
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    void setVisible(bool b) { bVisible = b; };

    const auto& isVisible() const { return bVisible; };


protected:
    virtual void createShader() override;
    virtual void createDefaultLodLevel() override;
    void createGizmoArrowsVbo();
    void updateArrowPoints();
    void grabGizmoAxis(int mousePosX, int mousePosY, EArrowType axis);
    void showAtPos(QVector3D newPos, bool drawX = true, bool drawY = true, bool drawZ = true);
    QVector3D moveAlongAxis(int mousePosX, int mousePosY);

    std::optional<EArrowType> isMouseOverGizmo(int mousePosX, int mousePosY);

    QVector3D getMovementDelta(int mousePosX, int mousePosY);

    static const QMap<EArrowType, QVector4D> arrowColors;
    QMap<EArrowType, std::vector<QVector3D>> arrowPoints;
    QVector3D gizmoBasePos;
    QVector3D mouseGripOffset;
    QVector3D gizmoOffset;
    EArrowType arrowSelected;

    bool bVisible = false;
    QMap<EArrowType, bool> useArrow;

    QSharedPointer<RenderGeometryData<>> xArrow;
    QSharedPointer<RenderGeometryData<>> zArrow;

    friend class Design::SelectionManipulationGizmo;
};