#include "stdafx.h"
#include "UrbanHandleDrawable.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGenerationStage.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanSuggestion.h"

#define URBAN_HANDLE_SIZE_SHADER "3000.0"

DUrbanHandle::DUrbanHandle(const QVector3D& vertex)
{
    cachedVertex = { vertex.x(), 0, vertex.z()};
    cachedHeight = vertex.y() + 3000.f;
}

void DUrbanHandle::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    auto lockedSite = ownedSuggestion.lock();
   
    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, QVector4D(0.75, 0.75, 0.75, 1.0));
    ShaderPipeline::current->setUniformValue(EShaderUniform::Height, cachedHeight);

    getActiveGeometry()->vertices.front().setY(cachedHeight);
    cacheBoundingBox();

    auto& indices = geometry[ELOD::Last]->indices;
    glDrawElements(GL_POINTS, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DUrbanHandle::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewMtx, camera.getViewMatrix());
    shaderPipeline.setUniformValue(EShaderUniform::ProjectionMtx, camera.getProjectionMatrix());

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture->textureId());
}

void DUrbanHandle::createShaderResources()
{
    // Setup texture
    if (!texture)
        texture = new QOpenGLTexture(QImage(getSpritePath()));
}

void DUrbanHandle::unbindShader()
{
    shaderPipeline.release();
}

bool DUrbanHandle::shouldDraw(int vIdx) const
{
    return Generation::Data::get()->getGenerationStage() == EGenerationStage::UrbanLayout;
}

void DUrbanHandle::update()
{
    auto& vertices = getActiveGeometry()->vertices;
    auto& indices = getActiveGeometry()->indices;

    // Clear
    vertices.resize(0);
    indices.resize(0);

    // Update position
    vertices << cachedVertex;
    indices << 0;

    updateVbo(activeLOD);

    hasAdjustedPosition = false;
}

void DUrbanHandle::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 view;\n"
        "uniform float height;\n"
        "in vec4 vertex;\n"
        "void main(void)\n"
        "{\n"
        "    vec4 vtx = vertex;\n"
        "    vtx.y += height;\n"
        "    gl_Position = view * vtx;\n"
        "}\n";

    const char* geometryShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 projection;\n\n"

        "const float particle_size = " URBAN_HANDLE_SIZE_SHADER ";\n\n"

        "layout(points) in;\n"
        "layout(triangle_strip, max_vertices = 4) out;\n\n"

        "out vec2 vertexUV;\n\n"

        "void main()\n"
        "{\n"
        "vec4 P = gl_in[0].gl_Position;\n"
        // a: left-bottom
        "vec2 va = P.xy + vec2(-0.5, -0.5) * particle_size;\n"
        "gl_Position = projection * vec4(va, P.zw);\n"
        "vertexUV = vec2(0.0, 0.0);\n"
        "EmitVertex();\n"
        // b: left-top
        "vec2 vb = P.xy + vec2(-0.5, 0.5) * particle_size;\n"
        "gl_Position = projection * vec4(vb, P.zw);\n"
        "vertexUV = vec2(0.0, 1.0);\n"
        "EmitVertex();"
        // d: right-bottom
        "vec2 vd = P.xy + vec2(0.5, -0.5) * particle_size;\n"
        "gl_Position = projection * vec4(vd, P.zw);\n"
        "vertexUV = vec2(1.0, 0.0);\n"
        "EmitVertex();"
        // c: right-top
        "vec2 vc = P.xy + vec2(0.5, 0.5) * particle_size;\n"
        "gl_Position = projection * vec4(vc, P.zw);\n"
        "vertexUV = vec2(1.0, 1.0);\n"
        "EmitVertex();\n"
        "EndPrimitive();\n"
        "}\n";

    // The final color depends on the height. Simple, but brings a lot.
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform sampler2D tex0;\n"
        "uniform vec4 color;\n"
        "uniform int objectID;\n\n"

        "const vec3 transparent_sub = vec3(0,1,0);\n"
        "in vec2 vertexUV;\n"
        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"
        "void main(void)\n"
        "{\n"
        "   vec3 texColor = texture(tex0, vertexUV).rgb;\n"
        "   if (texColor == transparent_sub) \n"
        "       discard;"
        "	fragColor = vec4(texColor, 1.0) * color;\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
        "};";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Geometry, geometryShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();

    // Set shader parameters' locations.
    shaderPipeline.set(EShaderAttribute::Position, "vertex");

    shaderPipeline.set(EShaderUniform::ViewMtx, "view");
    shaderPipeline.set(EShaderUniform::ProjectionMtx, "projection");
    shaderPipeline.set(EShaderUniform::Color0, "color");
    shaderPipeline.set(EShaderUniform::Height, "height");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}
