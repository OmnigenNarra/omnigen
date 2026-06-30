#include "stdafx.h"
#include "SkyboxDrawable.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"

void DSkybox::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();
    shaderPipeline.setUniformValue(EShaderUniform::ViewMtx, camera.getViewMatrix());
    shaderPipeline.setUniformValue(EShaderUniform::ProjectionMtx, camera.getProjectionMatrix());
    shaderPipeline.setUniformValue(EShaderUniform::CameraPos, camera.getPosition());

    texture->bind(GL_TEXTURE0);

    // The most important part
    // Skybox rendering uses a trick, in fact it's a tiny sphere around the camera.
    glDepthFunc(GL_LEQUAL);
    glFrontFace(GL_CCW);
}

void DSkybox::cacheBoundingBox()
{
}

void DSkybox::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    // Draw the terrain mesh.
    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_POINTS, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DSkybox::unbindShader()
{
    // The most important part
    glFrontFace(GL_CW);
    glDepthFunc(GL_LESS);
    shaderPipeline.release();
}

void DSkybox::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform vec3 cameraPos;\n"

        "void main(void)\n"
        "{\n"
        "    gl_Position = vec4(cameraPos, 1.0);\n"
        "}\n";

    const char* geometryShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 view;\n"
        "uniform mediump mat4 projection;\n\n"

        "const float radius = 10000;\n\n"

        "layout(points) in;\n"
        "layout(triangle_strip, max_vertices = 14) out;\n\n"

        "out vec3 vertexUV;\n\n"

        "const vec3 v1  = vec3(-1, 1, 1);\n"    // 4: front_top_left
        "const vec3 v2  = vec3(1, 1, 1);\n"     // 3: front_top_right
        "const vec3 v3  = vec3(-1, -1, 1);\n"   // 7: front_bottom_left
        "const vec3 v4  = vec3(1, -1, 1);\n"    // 8: front_bottom_right
        "const vec3 v5  = vec3(1, -1, -1);\n"   // 5: back_bottom_right
        "const vec3 v6  = vec3(1, 1, 1);\n"     // 3: front_top_right
        "const vec3 v7  = vec3(1, 1, -1);\n"    // 1: back_top_right
        "const vec3 v8  = vec3(-1, 1, 1);\n"    // 4: front_top_left 
        "const vec3 v9  = vec3(-1, 1, -1);\n"   // 2: back_top_left
        "const vec3 v10 = vec3(-1, -1, 1);\n"   // 7: front_bottom_left
        "const vec3 v11 = vec3(-1, -1, -1);\n"  // 6: back_bottom_left
        "const vec3 v12 = vec3(1, -1, -1);\n"   // 5: back_bottom_right
        "const vec3 v13 = vec3(-1, 1, -1);\n"   // 2: back_top_left
        "const vec3 v14 = vec3(1, 1, -1);\n"    // 1: back_top_right

        "void main()\n"
        "{\n"
        "vec4 P = gl_in[0].gl_Position;\n"
        // cube strip
        "vertexUV = v1;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v2;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v3;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v4;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v5;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v6;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v7;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v8;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v9;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v10;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v11;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v12;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v13;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "vertexUV = v14;\n"
        "gl_Position = projection * view * (P + vec4(vertexUV * radius, 0));\n"
        "gl_Position = gl_Position.xyww;\n"
        "EmitVertex();\n"
        "EndPrimitive();\n"
        "}\n";

    // The final color depends on the height. Simple, but brings a lot. 
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "#extension ARB_texture_cube_map : enable\n"
        "uniform samplerCube skyboxTexture;\n"
        "in vec3 vertexUV;\n"
        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"
        
        "void main(void)\n"
        "{\n"
        "	fragColor = texture(skyboxTexture, vertexUV);\n"
        "	fragColor.w = 0.5;\n"
        "	outData = vec4(0,0,0,0);\n"
        "};";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Geometry, geometryShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();

    shaderPipeline.set(EShaderUniform::ViewMtx, "view");
    shaderPipeline.set(EShaderUniform::ProjectionMtx, "projection");
    shaderPipeline.set(EShaderUniform::CameraPos, "cameraPos");

    // Can't render without any vbo
    shaderPipeline.attribs[0][EShaderAttribute::Position];

    // Bind stuff here, but it's never gonna be used
    shaderPipeline.uniforms[EShaderUniform::ObjectID];
}

void DSkybox::createShaderResources()
{
    const QImage posx = QImage("Resources/Skybox/xpos.png");
    const QImage posy = QImage("Resources/Skybox/ypos.png");
    const QImage posz = QImage("Resources/Skybox/zpos.png");
    const QImage negx = QImage("Resources/Skybox/xneg.png");
    const QImage negy = QImage("Resources/Skybox/yneg.png");
    const QImage negz = QImage("Resources/Skybox/zneg.png");

    texture = new QOpenGLTexture(QOpenGLTexture::TargetCubeMap);
    texture->create();
    texture->setSize(posx.width(), posx.height(), posx.depth());
    texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    texture->allocateStorage();

    texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveX,
        QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
        (const void*)posx.constBits(), 0);

    texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveY,
        QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
        (const void*)posy.constBits(), 0);

    texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveZ,
        QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
        (const void*)posz.constBits(), 0);

    texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeX,
        QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
        (const void*)negx.constBits(), 0);

    texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeY,
        QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
        (const void*)negy.constBits(), 0);

    texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeZ,
        QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
        (const void*)negz.constBits(), 0);

    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::LinearMipMapLinear);

    shaderPipeline.set(EShaderUniform::Color0, "skyboxTexture");
}

void DSkybox::createDefaultLodLevel()
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& indices = data->indices;

    // One vertex expanded to cube in geometry shader.
    vertices << QVector3D();
    indices << 0;

    assignLodLevel(ELOD::Last, data);
}
