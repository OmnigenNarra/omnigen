#include "stdafx.h"
#include "DomainHandleDrawable.h"
#include "DomainDrawable.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Omnigen.h"
#include "Editor/StageTools/Layout/LayoutSelection.h"

void DDomainHandle::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    auto lockedDomain = ownedDomain.lock();
    float height = 0.0f;
    if (lockedDomain->getType() == EDomainType::Terrain)
        height = lockedDomain->getData<EDomainType::Terrain>()->maxHeight;

    QVector4D color = DDomain::Colors[rainbowMode ? EDomainType::Last : lockedDomain->getType()];
    if (Design::DomainSelection::isDomainHovered(sharedFromThis()))
        color *= 1.33;

    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, color);
    ShaderPipeline::current->setUniformValue(EShaderUniform::Height, height);

    if(lockedDomain->getType() == EDomainType::Terrain)
        if (getPosition().y() != height + (GRID_SEGMENT_WIDTH))
        {
            getActiveGeometry()->vertices.front().setY(height + (GRID_SEGMENT_WIDTH));
            cacheBoundingBox();
        }

    auto& indices = geometry[ELOD::Last]->indices;
    glDrawElements(GL_POINTS, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DDomainHandle::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewMtx, camera.getViewMatrix());
    shaderPipeline.setUniformValue(EShaderUniform::ProjectionMtx, camera.getProjectionMatrix());

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture->textureId());
}

void DDomainHandle::createShaderResources()
{
    // Setup texture
    if (!texture)
        texture = new QOpenGLTexture(QImage(getSpritePath()));
}

void DDomainHandle::unbindShader()
{
    shaderPipeline.release();
}

bool DDomainHandle::shouldDraw(int vIdx) const
{
    return Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout || 
        (Generation::Data::get()->getGenerationStage() == EGenerationStage::Ridges && ownedDomain.lock()->getType() == EDomainType::Terrain);
}

void DDomainHandle::setRainbowMode(bool inRaimbowMode)
{
    rainbowMode = inRaimbowMode;
}

void DDomainHandle::update()
{
    auto& vertices = getActiveGeometry()->vertices;
    auto& indices = getActiveGeometry()->indices;

    // Reset rainbow states if applicable
    prePositionChange();

    // Clear
    vertices.resize(0);
    indices.resize(0);

    // One vertex for billboard only
    QVector3D target;

    // Compute average x and z grid position of all selected squares
    auto lockedDomain = ownedDomain.lock();
    for (auto&& [x, z] : lockedDomain->squares)
    {
        target[0] += x;
        target[2] += z;
    }
    target /= lockedDomain->squares.size();

    // Adjust for the very center
    target[0] += 0.5f;
    target[2] += 0.5f;

    // Compute the world position
    target[0] *= GRID_SEGMENT_WIDTH;
    target[2] *= GRID_SEGMENT_WIDTH;
    // Handle height
    target[1] = GRID_SEGMENT_WIDTH;

    // Update position and rainbow states
    vertices << target;
    indices << 0;
    postPositionChange();

    updateVbo(activeLOD);

    hasAdjustedPosition = false;
}

void DDomainHandle::createShader()
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

        "const float particle_size = " DOMAIN_HANDLE_SIZE_SHADER ";\n\n"

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

void DDomainHandle::prePositionChange()
{
    setRainbowMode(false);
    auto overlappingHandles = Generation::Data::get()->getDomainHandlesAt(getPosition());
    removeOne(overlappingHandles, sharedFromThis());

    // If only one left, it's no longer overlapped.
    if (overlappingHandles.size() == 1)
        overlappingHandles[0]->setRainbowMode(false);
}

void DDomainHandle::postPositionChange()
{
    auto overlappingHandles = Generation::Data::get()->getDomainHandlesAt(getPosition());
    if (overlappingHandles.size() > 1)
        for (auto&& handle : overlappingHandles)
            handle->setRainbowMode(true);
}
