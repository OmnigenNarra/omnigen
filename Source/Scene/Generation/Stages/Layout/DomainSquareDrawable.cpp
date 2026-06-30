#include "stdafx.h"
#include "DomainSquareDrawable.h"
#include "Scene/Core/EditorGridDrawable.h"
#include <QMessageBox>
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Utils/CoreUtils.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "Omnigen.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"

DDomainSquare::DDomainSquare(int gx, int gz)
    : gridID({ gx, gz })
{
}

void DDomainSquare::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());

    // Bind selection data
    auto& shaderSelectionData = DEditorGrid::shaderSelectionData;
    shaderPipeline.setUniformValueArray(EShaderUniform::SelectionData, shaderSelectionData);
}

void DDomainSquare::cacheBoundingBox()
{
    auto&& [x, z] = gridID;
    QVector3D nbl = { x * GRID_SEGMENT_WIDTH, 0, z * GRID_SEGMENT_WIDTH };
    QVector3D ftr = { (x + 1) * GRID_SEGMENT_WIDTH, 10, (z + 1) * GRID_SEGMENT_WIDTH };

    cachedBoundingBox = { nbl, ftr - nbl };
}

void DDomainSquare::draw()
{
    auto&& geom = getActiveGeometry();
    auto& vbo = geom->vbo;

    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, geom->size());
    vbo.release();

    QVector4D colors[4];
    auto myDomains = Generation::Data::get()->getDomainsAtSquare(gridID);
    auto domainIt = myDomains.begin();
    for (int i = 0; i < 4; ++i)
    {
        if ((i < myDomains.size()) && domainIt->second)
        {
            colors[i] = DDomain::Colors[domainIt->first];
            ++domainIt;
        }
        else switch (myDomains.size())
        {
        case 1: colors[i] = colors[0]; break;
        case 2: colors[i] = colors[i - 2]; break;
        case 3: colors[i] = colors[1]; break;
        }
    }

    QMatrix4x4 M(colors[0][0], colors[1][0], colors[2][0], colors[3][0],
        colors[0][1], colors[1][1], colors[2][1], colors[3][1],
        colors[0][2], colors[1][2], colors[2][2], colors[3][2],
        colors[0][3], colors[1][3], colors[2][3], colors[3][3]);

    shaderPipeline.setUniformValue(EShaderUniform::Color0, M);

    // Draw the terrain mesh.
    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DDomainSquare::unbindShader()
{
    shaderPipeline.release();
}

bool DDomainSquare::shouldDraw(int vIdx) const
{
    return Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout;
}

void DDomainSquare::createShader()
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
        "uniform mat4 colors;\n"
        "uniform uint selectionData[512];\n\n"
        "uniform int objectID;\n\n"

        "const vec4 gridColor = vec4(0.4, 0.4, 0.4, 1);\n"
        "const float gridOpacity = 0.7f;"
        "const vec4 gridSelectionColor = vec4(0.0, 0.8, 0.0, 1);\n"
        "const float gridWidth = " GRID_SEGMENT_WIDTH_SHADER ";\n"
        "const float gridThickness =" GRID_THICKNESS ";\n\n"

        "in vec3 pNormal;\n"
        "in vec4 vertexWorldPos;\n\n"
        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"

        "void main(void)\n"
        "{\n"
        "   vec2 gridPos = vec2(mod(vertexWorldPos.x," GRID_SEGMENT_WIDTH_SHADER "), mod(vertexWorldPos.z," GRID_SEGMENT_WIDTH_SHADER "));\n"
        "   int color_idx = int(floor(mod(gridPos.x + gridPos.y, " GRID_SEGMENT_WIDTH_SHADER ") / (" GRID_SEGMENT_WIDTH_SHADER "* 0.25)));\n"
        "   fragColor = colors[color_idx] * 0.9;\n"
        // Grid overlay
        "   float mixFactor = 0.0f;\n"
        "   if (gridPos.x < gridThickness) { mixFactor = gridOpacity; }\n"
        "   if (gridPos.y < gridThickness) { mixFactor = gridOpacity; }\n"
        "   if (gridPos.x > " GRID_SEGMENT_WIDTH_SHADER " - gridThickness) { mixFactor = gridOpacity; }\n"
        "   if (gridPos.y > " GRID_SEGMENT_WIDTH_SHADER " - gridThickness) { mixFactor = gridOpacity; }\n"
        //"	if (mixFactor > 0) { fragColor.xyz = abs(fragColor.xyz - gridColor.xyz); fragColor = mix(fragColor, gridColor, mixFactor); }\n"
        // Selection
        "   int gridX = int(floor(vertexWorldPos.x / gridWidth));\n"
        "   int gridZ = int(floor(vertexWorldPos.z / gridWidth));\n"
        "   uint singleDimIdx = gridX *" GRID_SEGMENT_COUNT_SHADER "+ gridZ;\n"
        "   uint unitIdx = singleDimIdx / 31;\n"
        "   uint unitBit = singleDimIdx % 31;\n"
        "   if ((selectionData[unitIdx] & (1<<unitBit)) > 0) fragColor = mix(fragColor, gridSelectionColor, mixFactor == gridOpacity ? 0.7 : 0.3);\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
        "};\n";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();
    Q_ASSERT(ok);

    // Set shader parameters' locations.
    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::SelectionData, "selectionData");
    shaderPipeline.set(EShaderUniform::Color0, "colors");

    shaderPipeline.set(EShaderAttribute::Position, "vertex");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DDomainSquare::createDefaultLodLevel()
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& indices = data->indices;

    // One great quad.
    vertices.resize(4);

    vertices[0] = QVector3D(GRID_SEGMENT_WIDTH * gridID.x, 0, GRID_SEGMENT_WIDTH * gridID.z);
    vertices[1] = QVector3D(GRID_SEGMENT_WIDTH * (gridID.x + 1), 0, GRID_SEGMENT_WIDTH * gridID.z);
    vertices[2] = QVector3D(GRID_SEGMENT_WIDTH * (gridID.x + 1), 0, GRID_SEGMENT_WIDTH * (gridID.z + 1));
    vertices[3] = QVector3D(GRID_SEGMENT_WIDTH * gridID.x, 0, GRID_SEGMENT_WIDTH * (gridID.z + 1));

    appendFace(indices, { 0, 1, 2, 3 });

    assignLodLevel(ELOD::Last, data);
}