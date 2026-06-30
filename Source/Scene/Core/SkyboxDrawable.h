#pragma once
#include "Scene/OmnigenDrawable.h"

class DSkybox : public OmnigenDrawable
{
    static inline ShaderPipeline shaderPipeline;
    static inline QOpenGLTexture* texture = nullptr;

public:
    DSkybox() = default;

    // Drawable
    virtual void updateCullStatus(const OmnigenCamera& camera, int vIdx) override {};
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Skybox; };
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

protected:
    virtual void createShader() override;
    virtual void createShaderResources() override;
    virtual void createDefaultLodLevel() override;
};