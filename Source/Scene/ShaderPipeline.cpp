#include "stdafx.h"
#include "ShaderPipeline.h"

void ShaderPipeline::set(EShaderUniform location, const char* var)
{
    int loc = uniformLocation(var);
    if (loc == -1)
        return;

    uniforms[location] = loc;
}

void ShaderPipeline::set(EShaderAttribute location, const char* var, bool instanced, int locationCount)
{
    int loc = attributeLocation(var);
    if (loc == -1)
        return;

    attribs[instanced][location] = { loc, locationCount };
}

void ShaderPipeline::setUniformValueArray(EShaderUniform location, const std::vector<float>& values)
{
    QOpenGLShaderProgram::setUniformValueArray(uniforms.at(location), values.data(), values.size(), 1);
}

void ShaderPipeline::setUniformValueArray(EShaderUniform location, const std::vector<qint32>& values)
{
    QOpenGLShaderProgram::setUniformValueArray(uniforms.at(location), values.data(), values.size());
}

void ShaderPipeline::setUniformValueArray(EShaderUniform location, const std::vector<quint32>& values)
{
    QOpenGLContext::currentContext()->extraFunctions()->glUniform1uiv(uniforms.at(location), values.size(), values.data());
}

void ShaderPipeline::setAttributeBuffer(EShaderAttribute location, GLenum type, int offset, int tupleSize, int stride, bool instanced)
{
    auto [loc, count] = attribs[instanced].at(location);
    for (int i = 0; i < count; ++i)
        QOpenGLShaderProgram::setAttributeBuffer(loc + i, type, offset + 4 * i * sizeof(float), tupleSize, stride);
}

void ShaderPipeline::setAttributeBufferI(EShaderAttribute location, GLenum type, int offset, int tupleSize, int stride, bool instanced)
{
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    auto [loc, count] = attribs[instanced].at(location);
    for (int i = 0; i < count; ++i)
        gl->glVertexAttribIPointer(loc + i, tupleSize, type, stride, reinterpret_cast<const void*>(qintptr(offset + 4 * i * sizeof(float))));
}

void ShaderPipeline::bind() // shadowing
{
    bool bindSuccess = QOpenGLShaderProgram::bind();
    Q_ASSERT(bindSuccess);

    // Enable attribute arrays registered in attribs
    for (auto&& [key, attrib] : attribs[0])
        for (int i = 0; i < attrib[1]; ++i)
            enableAttributeArray(attrib[0] + i);

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    for (auto&& [key, attrib] : attribs[1])
        for (int i = 0; i < attrib[1]; ++i)
        {
            enableAttributeArray(attrib[0] + i);
            gl->glVertexAttribDivisor(attrib[0] + i, 1);
        }

    // current is used by draw logic
    current = this;
}

void ShaderPipeline::release() // shadowing
{
    // Disable attribute arrays registered in attribs
    for (auto&& [key, attrib] : attribs[0])
        for (int i = 0; i < attrib[1]; ++i)
            disableAttributeArray(attrib[0] + i);

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    for (auto&& [key, attrib] : attribs[1])
        for (int i = 0; i < attrib[1]; ++i)
        {
            disableAttributeArray(attrib[0] + i);
            gl->glVertexAttribDivisor(attrib[0] + i, 0);
        }

    current = nullptr;
    QOpenGLShaderProgram::release();
}