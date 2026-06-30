#include "stdafx.h"
#include "EditorGridDrawable.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Utils/CoreUtils.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"
#include <Source/Scene/Generation/OmnigenGenerationData.h>

void DEditorGrid::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());

    // Bind selection data.
    shaderPipeline.setUniformValueArray(EShaderUniform::SelectionData, shaderSelectionData);
}

void DEditorGrid::cacheBoundingBox()
{
}

void DEditorGrid::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    // Draw the terrain mesh.
    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DEditorGrid::unbindShader()
{
    shaderPipeline.release();
}

bool DEditorGrid::shouldDraw(int vIdx) const
{
    return Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout;
}

void DEditorGrid::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mat4 viewProjection;\n\n"

        "in vec4 vertex;\n"
        "out vec4 vertexWorldPos;\n\n" // Store both screen space and world space coords

        "void main(void)\n"
        "{\n"
        "    gl_Position = viewProjection * vertex;\n"
        "    vertexWorldPos = vertex;\n"
        "}\n";

    // The final color depends on the height. Simple, but brings a lot.
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform uint selectionData[512];\n" // 512 * 31 usable flag bits = 15872 selection flags; 100x100 squares map needs 10000. Slightly more than needed
        "uniform int objectID;\n\n"

        "const vec4 baseColor = vec4(0.3, 0.3, 0.3, 1);\n"
        "const vec4 gridColor = vec4(0.4, 0.4, 0.4, 1);\n"
        "const vec4 gridSelectionColor = vec4(0.0, 0.8, 0.0, 1);\n"
        "const float gridWidth = " GRID_SEGMENT_WIDTH_SHADER ";\n"
        "const float gridThickness =" GRID_THICKNESS ";\n\n"

        "in vec4 vertexWorldPos;\n"
        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n" // selection buffer write

        "void main(void)\n"
        "{\n"
        // Grid position
        "   vec2 gridPos = vec2(mod(vertexWorldPos.x, gridWidth), mod(vertexWorldPos.z, gridWidth));\n"
        "   float mixFactor = 0.0f;\n"
        // Is section border?
        "   if (gridPos.x < gridThickness) { mixFactor = 1.0f; }\n"
        "   if (gridPos.y < gridThickness) { mixFactor = 1.0f; }\n"
        "   if (gridPos.x > gridWidth - gridThickness) { mixFactor = 1.0f; }\n"
        "   if (gridPos.y > gridWidth - gridThickness) { mixFactor = 1.0f; }\n"
        "	fragColor = mix(baseColor, gridColor, mixFactor);\n"
        // Selection
        "   int gridX = int(floor(vertexWorldPos.x / gridWidth));\n"
        "   int gridZ = int(floor(vertexWorldPos.z / gridWidth));\n"
        "   uint singleDimIdx = gridX *" GRID_SEGMENT_COUNT_SHADER "+ gridZ;\n"
        "   uint unitIdx = singleDimIdx / 31;\n"
        "   uint unitBit = singleDimIdx % 31;\n"
        "   if ((selectionData[unitIdx] & (1<<unitBit)) > 0) fragColor = mix(fragColor, gridSelectionColor, mixFactor == 1.0f ? 0.9 : 0.5);\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
        "};\n";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();

    // Set shader parameters' locations.
    shaderPipeline.set(EShaderAttribute::Position, "vertex");

    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::SelectionData, "selectionData");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DEditorGrid::createDefaultLodLevel()
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& indices = data->indices;

    // One great quad.
    vertices.resize(4);

    vertices[0] = QVector3D(0, GRID_HEIGHT, 0);
    vertices[1] = QVector3D(GRID_SEGMENT_WIDTH * GRID_SEGMENT_COUNT, GRID_HEIGHT, 0);
    vertices[2] = QVector3D(GRID_SEGMENT_WIDTH * GRID_SEGMENT_COUNT, GRID_HEIGHT, GRID_SEGMENT_WIDTH * GRID_SEGMENT_COUNT);
    vertices[3] = QVector3D(0, GRID_HEIGHT, GRID_SEGMENT_WIDTH * GRID_SEGMENT_COUNT);

    appendFace(indices, { 0, 1, 2, 3 });

    assignLodLevel(ELOD::Last, data);
}
