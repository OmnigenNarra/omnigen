#pragma once
#include <QOpenGLShaderProgram>

class OmnigenDrawable;

// Used for uniform location mapping
enum class EShaderUniform
{
    WorldMtx,
    ViewMtx,
    ProjectionMtx,
    ViewProjectionMtx,
    SelectionData,
    DrawType,
    Color0,
    CameraPos,
    Height,
    Texture0,
    Texture1,
    Texture2,
    Texture3,
    Texture4,
    Texture5,
    Texture6,
    Texture7,
    TerrainTileSizes,
    TerrainMaxDisplacements,
    CoverTileSizes,
    CoverMaxDisplacements,
    TerrainTextureIds,
    BiomeTextureIds,
    Heat,
    Humidity,
    ObjectID,
    BatchID,
};

// Used for attribute location tracking
enum class EShaderAttribute
{
    Position,
    Normal,
    TexID0,
    TexID1,
    TexID2,
    TexWeight0,
    TexWeight1,
    Test,
    UV,
    DisplacementFactor,
    InstanceWorldMtx,
    SurfaceNormal,
};

// Wrapper for QOpenGLShaderProgram, simpler api
struct ShaderPipeline : public QOpenGLShaderProgram
{
    static inline ShaderPipeline* current = nullptr;

    virtual void bind(); // shadowing
    virtual void release(); // shadowing
    void set(EShaderUniform location, const char* var);
    void set(EShaderAttribute location, const char* var, bool instanced = false, int locationCount = 1);

    template<typename T>
    void setUniformValue(EShaderUniform location, const T& value)
    {
        QOpenGLShaderProgram::setUniformValue(uniforms.at(location), value);
    }

    void setUniformValueArray(EShaderUniform location, const std::vector<float>& values);
    void setUniformValueArray(EShaderUniform location, const std::vector<qint32>& values);
    void setUniformValueArray(EShaderUniform location, const std::vector<quint32>& values);

    template<typename T>
    void setAttributeValue(EShaderAttribute location, const T& value, bool instanced = false)
    {
        QOpenGLShaderProgram::setAttributeValue(attribs[instanced].at(location)[0], value);
    }

    template<typename T>
    void setAttributeArray(EShaderAttribute location, const std::vector<T>& values, bool instanced = false)
    {
        QOpenGLShaderProgram::setAttributeArray(attribs[instanced].at(location)[0], values.data(), values.size());
    }

    void setAttributeBuffer(EShaderAttribute location, GLenum type, int offset, int tupleSize, int stride, bool instanced = false);
    void setAttributeBufferI(EShaderAttribute location, GLenum type, int offset, int tupleSize, int stride, bool instanced = false);

    std::map<EShaderUniform, int> uniforms;
    std::array<std::map<EShaderAttribute, std::array<int, 2> /*location, count*/>, 2 /*per-vertex, per-instance*/> attribs;
};