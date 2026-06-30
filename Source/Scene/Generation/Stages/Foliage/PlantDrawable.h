#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Data/Assets/AssetBase.h"
#include "Data/Assets/Texture/AssetTexture.h"
#include "Scene/Generation/Stages/Layout/Data/Biome/DomainData_Biome.h"

struct PlantPlacementData;

namespace Generation
{
    class DPlant : public OmnigenDrawable
    {
        static inline ShaderPipeline shaderPipeline;
        static inline std::map<ETextureComponentOut, EShaderUniform> shaderTextureMap =
        {
            {ETextureComponentOut::DiffuseHeight, EShaderUniform::Texture0},
            {ETextureComponentOut::Normal, EShaderUniform::Texture1},
        };

    public:
        DPlant(const QSharedPointer<OmnigenAsset<EAsset::Plant>>& inAtlas, int inIdx);
        ~DPlant();

        // Drawable
        virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Foliage; }
        virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
        virtual void cacheBoundingBox() override;
        virtual float getCullDistance() const override { return 5000; }

        virtual void bindShader(const OmnigenCamera& camera) override;
        virtual void unbindShader() override;
        virtual void draw() override;
        virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

        const PlantPlacementData& getPlacementData(EBiomeLayer layer);
        auto getAssetInfo() const
        {
            return std::tuple{ meshAtlasAsset, atlasIdx };
        }

        static void createResources(const std::vector<QSharedPointer<DPlant>>& spawnedPlants);

        IMPLEMENT_SHOULD_DRAW();

    private:
        virtual void createShader() override;
        virtual void createShaderResources() override;

        QSharedPointer<OmnigenAsset<EAsset::Plant>> meshAtlasAsset;
        int atlasIdx;
        std::vector<std::map<ETextureComponentOut, GLuint>> gpuTextureViews;
        GLuint instanceDataBufferId;
    };
}