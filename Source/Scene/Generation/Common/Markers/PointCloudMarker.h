#pragma once
#include "MarkerDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"

// Deprecated, implement BatchingPointMarker
class DPointCloudMarker : public DMarker
{
protected:
    static inline ShaderPipeline shaderPipeline;

public:
    DPointCloudMarker(const std::vector<QVector3D>& inPoints, const QVector4D& inColor = QVector4D(1,1,1,1));
    
    // Drawable
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    // Rendering
    IMPLEMENT_SHOULD_DRAW();

    const auto& getControlPoints() const { return getActiveGeometry()->vertices; }

protected:
    DPointCloudMarker() = default;
    virtual void createShader() override;
    virtual void createDefaultLodLevel(const std::vector<QVector3D>& inPoints);

    QVector4D color;
};