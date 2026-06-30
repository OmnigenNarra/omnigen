#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Constants.h"

// Grid represented by a single quad with lines procedurally generated in pixel shader
class DEditorGrid : public OmnigenDrawable
{
    static inline ShaderPipeline shaderPipeline;

public:
    static inline std::vector<GLuint> shaderSelectionData = std::vector<GLuint>(512);

    DEditorGrid() = default;

    // Drawable
    virtual void updateCullStatus(const OmnigenCamera& camera, int vIdx) override { bIsCulled = !shouldDraw(vIdx); };
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Grid; };
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual bool shouldDraw(int vIdx) const override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

protected:
    virtual void createShader() override;
    virtual void createDefaultLodLevel() override;
};