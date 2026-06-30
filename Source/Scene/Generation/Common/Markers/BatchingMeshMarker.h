#pragma once
#include "Utils/CoreUtils.h"
#include "Scene/Generation/Common/Markers/BatchMarker.h"

template<typename VertexType>
struct MeshPainter;

template<typename T>
concept HasNormal = requires (T v) { v.normal.x; } || requires (T v) { v.normal.x(); };

template<typename InVertexType>
struct MeshBatchParams
{
    using VertexType = InVertexType;
    using PainterType = MeshPainter<InVertexType>;

    QVector4D color;
    QVector3D renderOffset;
    ERenderType renderMode;
    decltype(GL_TRIANGLES) primitiveType = -1;

    bool operator<(const MeshBatchParams& o) const
    {
        return std::make_tuple(color.x(), color.y(), color.z(), color.w(), renderOffset.x(), renderOffset.y(), renderOffset.z(), renderMode, primitiveType)
            < std::make_tuple(o.color.x(), o.color.y(), o.color.z(), o.color.w(), o.renderOffset.x(), o.renderOffset.y(), o.renderOffset.z(), o.renderMode, o.primitiveType);
    };
};

template<typename VertexType = QVector3D>
struct MeshPainter
{
    ShaderPipeline shaderPipeline;
    float lineWidthCached;

    quint32 getShaderLabel() const { return typeid(decltype(*this)).hash_code(); };

    void createShader()
    {
        if (shaderPipeline.isLinked())
            return;

        // Compute final vertex position.
        std::ostringstream vSS;
        vSS << "#version " OPENGL_SHADER_VER "\n"

            << "uniform mat4 viewProjection;\n"
            << "uniform vec3 renderOffset;\n"
            << "in vec3 pos;\n";

        if constexpr (HasNormal<VertexType>)
        {
            vSS << "in vec3 normal;\n"
                << "out vec3 pNormal;\n";
        }

        vSS << "void main(void)\n"
            << "{\n"
            << "    gl_Position = viewProjection * vec4(pos + renderOffset, 1.0f);\n";

        if constexpr (HasNormal<VertexType>)
        {
            vSS << "pNormal = normal;\n";
        }

        vSS << "}";

        std::ostringstream fSS;
        fSS << "#version " OPENGL_SHADER_VER "\n"

            << "const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);\n"
            << "const float ambient = 0.2f;\n"

            << "uniform vec4 color;\n"
            << "uniform int objectID;\n\n";

        if constexpr (HasNormal<VertexType>)
        {
            fSS << "in vec3 pNormal;\n";
        }

        fSS << "layout (location = 0) out vec4 fragColor;\n"
            << "layout (location = 1) out vec4 outData;\n\n"

            << "void main(void)\n"
            << "{\n";

        if constexpr (HasNormal<VertexType>)
        {
            fSS << "vec3 normal = normalize(pNormal);\n"
                << "float factor = clamp(dot(lightDirection, -normal), 0.0f, 1.0f - ambient) + ambient;\n"
                << "fragColor = vec4(color.rgb * factor, 1);\n";
        }
        else
        {
            fSS << "fragColor = color;\n";
        }

        fSS << "   outData = vec4(objectID, 0, gl_PrimitiveID, 1); \n";
        fSS << "}";

        shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vSS.str().data());
        shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fSS.str().data());
        bool ok = shaderPipeline.link();

        shaderPipeline.set(EShaderAttribute::Position, "pos");
        shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
        shaderPipeline.set(EShaderUniform::WorldMtx, "renderOffset");
        shaderPipeline.set(EShaderUniform::Color0, "color");

        if constexpr (HasNormal<VertexType>)
        {
            shaderPipeline.set(EShaderAttribute::Normal, "normal");
        }

        shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
    }

    void bindShader(const OmnigenCamera& camera)
    {
        shaderPipeline.bind();
        shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
    }

    void draw(RenderGeometryData<VertexType>& geom, const MeshBatchParams<VertexType>& params)
    {
        auto& vbo = geom.vbo;
        vbo.bind();

        // simple vertex type
        if constexpr (sizeof(VertexType) == sizeof(QVector3D))
        {
            ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, sizeof(VertexType));
        }
        // custom vertex type
        else
        {
            ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(VertexType, position), 3, sizeof(VertexType));

            if constexpr (HasNormal<VertexType>)
            {
                ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(VertexType, normal), 3, sizeof(VertexType));
            }
        }
        vbo.release();

        ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, params.color);
        ShaderPipeline::current->setUniformValue(EShaderUniform::WorldMtx, params.renderOffset);

        if (params.renderMode == ERenderType::Wireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glGetFloatv(GL_LINE_WIDTH, &lineWidthCached);
            glLineWidth(3);
        }

        glDrawElements(params.primitiveType, geom.indices.size(), GL_UNSIGNED_INT, geom.indices.data());

        if (params.renderMode == ERenderType::Wireframe)
        {
            glPolygonMode(GL_FRONT, GL_FILL);
            glLineWidth(lineWidthCached);
        }
    }

    void unbindShader()
    {
        shaderPipeline.release();
    }

    virtual bool shouldDraw() const { return true; }
};

template<typename VertexType>
using DMeshMarker = DBatchingMarker<MeshBatchParams<VertexType>>;