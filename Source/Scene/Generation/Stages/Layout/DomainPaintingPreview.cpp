#include "stdafx.h"
#include "DomainPaintingPreview.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Scene/Core/EditorGridDrawable.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Source/Scene/Generation/OmnigenGenerationData.h"

DDomainPaintingPreview::DDomainPaintingPreview()
{
}

void DDomainPaintingPreview::update(const GPoint& newSquare)
{
    static const float previewHeight = 100.0f;

    squares += newSquare;

    auto& vertices = getActiveGeometry()->vertices;
    auto& indices = getActiveGeometry()->indices;

    vertices << QVector3D{ newSquare.x * GRID_SEGMENT_WIDTH, previewHeight, newSquare.z * GRID_SEGMENT_WIDTH };
    vertices << QVector3D{ (newSquare.x + 1) * GRID_SEGMENT_WIDTH, previewHeight, newSquare.z * GRID_SEGMENT_WIDTH };
    vertices << QVector3D{ (newSquare.x + 1) * GRID_SEGMENT_WIDTH, previewHeight, (newSquare.z + 1) * GRID_SEGMENT_WIDTH };
    vertices << QVector3D{ newSquare.x * GRID_SEGMENT_WIDTH, previewHeight, (newSquare.z + 1) * GRID_SEGMENT_WIDTH };
    quint32 i = indices.size();
    appendFace(indices, std::vector{ i, i + 1, i + 2, i + 3 }, true);

    updateVbo(ELOD::Last);
    cacheBoundingBox();
}

ERenderPriority DDomainPaintingPreview::getRenderPriority() const
{
    return ERenderPriority::Marker;
}

void DDomainPaintingPreview::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
}

void DDomainPaintingPreview::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    // Draw the terrain mesh.
    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DDomainPaintingPreview::unbindShader()
{
    shaderPipeline.release();
}

bool DDomainPaintingPreview::shouldDraw(int vIdx) const
{
    return Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout;
}

void DDomainPaintingPreview::createDefaultLodLevel()
{
    assignLodLevel(ELOD::Last, QSharedPointer<RenderGeometryData<>>::create());
}

void DDomainPaintingPreview::cacheBoundingBox()
{
    if (getActiveGeometry()->vertices.empty())
    {
        cachedBoundingBox = { {}, {} };
        return;
    }

    GVector2D min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    GVector2D max = { std::numeric_limits<float>::min(), std::numeric_limits<float>::min() };

    for (auto&& pt : getActiveGeometry()->vertices) 
    {
        min.x = std::min(min.x, pt.x());
        min.z = std::min(min.z, pt.z());

        max.x = std::max(max.x, pt.x());
        max.z = std::max(max.z, pt.z());
    }

    QVector3D nbl = { min.x, 0, min.z};
    QVector3D ftr = { max.x + GRID_SEGMENT_WIDTH, 10, max.z + GRID_SEGMENT_WIDTH };
    cachedBoundingBox = { nbl, ftr - nbl };
}

void DDomainPaintingPreview::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    // Compute final vertex position.
    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 viewProjection;\n\n"

        "in vec4 vertex;\n"

        "out vec4 vertexWorldPos;\n\n"

        "void main(void)\n"
        "{\n"
        "    gl_Position = viewProjection * vertex;\n"
        "    vertexWorldPos = vertex;\n"
        "}\n";

    // The final color depends on the height. Simple, but brings a lot.
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"

        "const vec4 gridColor = vec4(0.4, 0.4, 0.4, 1);\n"
        "const float gridOpacity = 0.7f;"
        "const vec4 selectionColor = vec4(0.8, 0.8, 0.0, 1.0);\n"
        "const float gridWidth = " GRID_SEGMENT_WIDTH_SHADER ";\n"
        "const float gridThickness =" GRID_THICKNESS ";\n\n"

        "in vec4 vertexWorldPos;\n\n"
        "layout (location = 0) out vec4 fragColor;\n"

        "void main(void)\n"
        "{\n"
        "   fragColor = selectionColor;\n"
        // Grid overlay
        "   vec2 gridPos = vec2(mod(vertexWorldPos.x," GRID_SEGMENT_WIDTH_SHADER "), mod(vertexWorldPos.z," GRID_SEGMENT_WIDTH_SHADER "));\n"
        "   float mixFactor = 0.0f;\n"
        "   if (gridPos.x < gridThickness) { mixFactor = gridOpacity; }\n"
        "   if (gridPos.y < gridThickness) { mixFactor = gridOpacity; }\n"
        "   if (gridPos.x > " GRID_SEGMENT_WIDTH_SHADER " - gridThickness) { mixFactor = gridOpacity; }\n"
        "   if (gridPos.y > " GRID_SEGMENT_WIDTH_SHADER " - gridThickness) { mixFactor = gridOpacity; }\n"
        "	if (mixFactor > 0) { fragColor.xyz = abs(fragColor.xyz - gridColor.xyz); fragColor = mix(fragColor, gridColor, mixFactor); }\n"
        "};\n";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();

    // Set shader parameters' locations.
    shaderPipeline.set(EShaderAttribute::Position, "vertex");

    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");

    // Bind stuff here, but it's never gonna be used
    shaderPipeline.uniforms[EShaderUniform::ObjectID];
}