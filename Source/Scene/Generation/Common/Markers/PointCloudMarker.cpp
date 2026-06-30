#include "stdafx.h"
#include "PointCloudMarker.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"

DPointCloudMarker::DPointCloudMarker(const std::vector<QVector3D>& inPoints, const QVector4D& inColor)
    : color(inColor)
{
    createDefaultLodLevel(inPoints);
}

void DPointCloudMarker::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
}

void DPointCloudMarker::cacheBoundingBox()
{
    cachedBoundingBox = BoundingBox::fromPoints(getControlPoints());
}

void DPointCloudMarker::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, color);
    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_POINTS, indices.size(), GL_UNSIGNED_INT, indices.data());

    DMarker::draw();
}

void DPointCloudMarker::unbindShader()
{
    shaderPipeline.release();
}

void DPointCloudMarker::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    // Compute final vertex position.
    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 viewProjection;\n\n"

        "in vec4 vertex;\n\n"

        "void main(void)\n"
        "{\n"
        "    gl_Position = viewProjection * vertex;\n"
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

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DPointCloudMarker::createDefaultLodLevel(const std::vector<QVector3D>& inPoints)
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& indices = data->indices;
    
    vertices = inPoints;
    indices.reserve(vertices.size());
    for (int i = 0; i < vertices.size(); ++i)
        indices << i;

    assignLodLevel(ELOD::Last, data);
}
