#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Data/Assets/Structure/AssetStructure.h"
#include "Data/Assets/Texture/AssetTexture.h"

class DUrbanMesh : public OmnigenDrawable
{
    static inline ShaderPipeline shaderPipeline;

    static inline std::map<ETextureComponentOut, EShaderUniform> shaderTextureMap =
    {
        {ETextureComponentOut::DiffuseHeight, EShaderUniform::Texture0},
        {ETextureComponentOut::Normal, EShaderUniform::Texture1},
    };

public:
    DUrbanMesh(const QSharedPointer<OmnigenAsset<EAsset::Structure>>& inMeshAsset, const QVector3D& inLocation, const float inScale);

    // Drawable
    virtual void updateCullStatus(const OmnigenCamera& camera, int vIdx) override;
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };

    virtual void cacheBoundingBox() override;

    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Terrain; }
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    static void createResources();

    void drawBounds();
    void drawForwardArrow() const;

protected:
    virtual void createShader() override;
    virtual void createShaderResources() override;

    QSharedPointer<OmnigenAsset<EAsset::Structure>> meshAsset;
    mutable std::vector<quint32> materialIndexMap = std::vector<quint32>(4, 0);

private:
    std::vector<std::map<ETextureComponentOut, GLuint>> gpuTextureViews;
    QVector3D location;
    float scale;
    GLuint instanceDataBufferId;
};