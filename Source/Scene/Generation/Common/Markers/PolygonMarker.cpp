#include "stdafx.h"
#include "PolygonMarker.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Utils/CircularVectorView.h"
#include <Source/Utils/Triangulation/Triangulation.h>
#include "Utils/Triangulation/Earcut.hpp"

DPolygonMarker::DPolygonMarker(const std::vector<QVector3D>& inControlPoints, float inHeight, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/)
    : color(inColor)
    , height(inHeight)
{
    createDefaultLodLevel(inControlPoints);
}

void DPolygonMarker::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
}

void DPolygonMarker::cacheBoundingBox()
{
    cachedBoundingBox = BoundingBox::fromPoints(getControlPoints());
}

void DPolygonMarker::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();
    QVector4D currentColor = getColor();
    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, selected || hovered ? (currentColor - QVector4D(0.15, 0.15, 0.15, 0)) : currentColor);
    ShaderPipeline::current->setUniformValue(EShaderUniform::Height, height);

    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, indices.data());

    DMarker::draw();
}

void DPolygonMarker::unbindShader()
{
    shaderPipeline.release();
}

void DPolygonMarker::setSelected(bool isSelected)
{
    selected = isSelected;
}

void DPolygonMarker::setHovered(bool isHovered)
{
    hovered = isHovered;
}

void DPolygonMarker::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    // Compute final vertex position.
    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 viewProjection;\n\n"
        "uniform float height;\n\n"

        "in vec4 vertex;\n\n"

        "void main(void)\n"
        "{\n"
        "    vec4 pos = vertex;\n"
        "    pos.y += height;\n"
        "    gl_Position = viewProjection * pos;\n"
        "}\n";

    // The final color depends on the height. Simple, but brings a lot.
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform vec4 color;\n"
        "uniform int objectID;\n\n"

        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"

        "void main(void)\n"
        "{\n"
        "   fragColor = color;\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
        "};\n";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();

    shaderPipeline.set(EShaderAttribute::Position, "vertex");
    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::Color0, "color");
    shaderPipeline.set(EShaderUniform::Height, "height");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DPolygonMarker::createDefaultLodLevel(const std::vector<QVector3D>& inControlPoints)
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& triangles = data->indices;

    vertices = inControlPoints;
    triangles = std::get<1>(constrainedTriangulation2D(std::vector<GVector2D>(vertices.begin(), vertices.end())));

    // Fix winding order
    for (int i = 0; i < triangles.size(); i += 3)
    {
        IndexType& i0 = triangles[i];
        IndexType i1 = triangles[i + 1];
        IndexType& i2 = triangles[i + 2];

        if (GVector2D::crossProduct(vertices[i0], vertices[i1], vertices[i2]) > 0.f)
            std::swap(i0, i2);
    }

    GVector2D center;
    for (auto&& p : vertices)
        center += p;
    center /= float(vertices.size());

    vertices.push_back(center);

    assignLodLevel(ELOD::Last, data);
}


DPolygonWithHolesMarker::DPolygonWithHolesMarker(const std::vector<QVector3D>& inControlPoints, const std::vector<std::vector<QVector3D>>& inCutPolygons, float inHeight, const QVector4D& inColor)
    : mainPolygon(inControlPoints)
    , cutPolygons(inCutPolygons)
{
    DPolygonMarker::color = inColor;
    DPolygonMarker::height = inHeight;

    createDefaultLodLevel(inControlPoints);
}

void DPolygonWithHolesMarker::createDefaultLodLevel(const std::vector<QVector3D>& inControlPoints)
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& triangles = data->indices;

    vertices = mainPolygon;
    for (auto&& poly : cutPolygons)
        vertices.insert(vertices.end(), poly.begin(), poly.end());

    std::vector<std::vector<QVector3D>> polygonWithCuts{ mainPolygon };
    polygonWithCuts.insert(polygonWithCuts.end(), cutPolygons.begin(), cutPolygons.end());
    triangles = mapbox::earcut(polygonWithCuts);

    // Fix winding order
    for (int i = 0; i < triangles.size(); i += 3)
    {
        IndexType& i0 = triangles[i];
        IndexType i1 = triangles[i + 1];
        IndexType& i2 = triangles[i + 2];

        if (GVector2D::crossProduct(vertices[i0], vertices[i1], vertices[i2]) > 0.f)
            std::swap(i0, i2);
    }

    GVector2D center;
    for (auto&& p : vertices)
        center += p;
    center /= float(vertices.size());

    vertices.push_back(center);

    assignLodLevel(ELOD::Last, data);
}
