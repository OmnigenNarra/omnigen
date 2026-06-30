#include "stdafx.h"
#include "MultiLineColoredMarker.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"


DMultiLineColoredMarker::DMultiLineColoredMarker(const std::vector<LineData>& inLines)
{
    linesGeometry = QSharedPointer<RenderGeometryData<PointData>>::create();

    auto& vertices = linesGeometry->vertices;
    auto& indices = linesGeometry->indices;

    for (const auto& lineData : inLines)
    {
        const int offset = vertices.size();
        for (const auto& pt: lineData.line)
            vertices <<= PointData{pt, QVector3D(lineData.color.x(), lineData.color.y(), lineData.color.z())};

        std::vector<IndexType> lineIndices(lineData.line.size());
        for (int i = 0; i < lineData.line.size(); ++i)
            lineIndices[i] = offset + i;

        appendLines(indices, lineIndices);
    }

    assignLodLevel(ELOD::Last, linesGeometry);
}

DMultiLineColoredMarker::DMultiLineColoredMarker(const std::vector<std::vector<PointData>>& inLines)
{
    linesGeometry = QSharedPointer<RenderGeometryData<PointData>>::create();
    auto& vertices = linesGeometry->vertices;
    auto& indices  = linesGeometry->indices;

    for (const auto& pointDataVec : inLines)
    {
        const int offset = vertices.size();
        vertices << pointDataVec;

        std::vector<IndexType> lineIndices(pointDataVec.size());
        for (int i = 0; i < pointDataVec.size(); ++i)
            lineIndices[i] = offset + i;

        appendLines(indices, lineIndices);
    }

    assignLodLevel(ELOD::Last, linesGeometry);
}


void DMultiLineColoredMarker::initialize()
{
    DMarker::initialize();
}

void DMultiLineColoredMarker::bindShader(const OmnigenCamera& camera)
{
    multiLineShaderPipeline.bind();
    multiLineShaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
    glGetFloatv(GL_LINE_WIDTH, &lastMultiLineWidth);
    glLineWidth(3);
}

void DMultiLineColoredMarker::unbindShader()
{
    multiLineShaderPipeline.release();
    glLineWidth(lastMultiLineWidth);
}

void DMultiLineColoredMarker::draw()
{
    auto&& geom = getActiveGeometry<PointData>();
    auto& vbo = geom->vbo;
    vbo.bind();

    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(PointData, position), 3, geom->size());
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(PointData, color),    3, geom->size());
    vbo.release();

    auto& indices = geom->indices;
    glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, indices.data());

    DMarker::draw();
}

void DMultiLineColoredMarker::createShader()
{
    if (multiLineShaderPipeline.isLinked())
        return;

    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mat4 viewProjection;\n"

        "in vec3 pos;\n"
        "in vec3 lineColor;\n\n"

        "out vec3 pColor;\n"

        "void main(void)\n"
        "{\n"
        "    gl_Position = viewProjection * vec4(pos, 1);\n"
        "    pColor = lineColor;\n"
        "}\n";

    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "in vec3 pColor;\n"
        "uniform int objectID;\n\n"

        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"

        "void main(void)\n"
        "{\n"
        "   fragColor = vec4(pColor, 1);\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
        "};\n";

    multiLineShaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    multiLineShaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = multiLineShaderPipeline.link();
    Q_ASSERT(ok);

    multiLineShaderPipeline.set(EShaderAttribute::Position, "pos");
    multiLineShaderPipeline.set(EShaderAttribute::Normal, "lineColor");
    multiLineShaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");

    multiLineShaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}
