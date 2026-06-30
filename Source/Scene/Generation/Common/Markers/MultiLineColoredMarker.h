#pragma once
#include "LineMarker.h"


class DMultiLineColoredMarker : public DLineMarker
{
public:

    struct LineData
    {
        std::vector<QVector3D> line;
        QVector4D color;
    };

    struct PointData
    {
        QVector3D position = {0.f, 0.f, 0.f};
        QVector3D color = {1.f, 1.f, 1.f};
    };

    DMultiLineColoredMarker(const std::vector<LineData>& inLines); // each line with different color
    DMultiLineColoredMarker(const std::vector<std::vector<PointData>>& inLines); // each point with different color

    virtual void initialize() override;

    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;

    virtual void draw() override;
    virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };


protected:
    DMultiLineColoredMarker() = default;
    virtual void createShader() override;

protected:

    QSharedPointer<RenderGeometryData<PointData>> linesGeometry;

    static inline ShaderPipeline multiLineShaderPipeline;
    static inline GLfloat lastMultiLineWidth;

};


