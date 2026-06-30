#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "Source/Scene/Generation/Stages/FeatureGeneration/ClusterMeshMarker.h"
#include "Data/Assets//Texture/AssetTexture.h"
#include "Scene/Generation/OmnigenGenerationStage.h"

namespace Generation
{
    template<EGenerationStage>
    class StageGen;
}

using TerrainChunkGeometryData = RenderGeometryData<TerrainMeshVertex>;

inline void omniSave(const TerrainChunkGeometryData& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const GeometryData<TerrainMeshVertex>&>(object);
}

inline void omniLoad(TerrainChunkGeometryData& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<GeometryData<TerrainMeshVertex>&>(object);
}

class DTerrainChunk : public OmnigenDrawable
{
    inline static ShaderPipeline shaderPipeline;

    inline static std::vector<float> terrainTileSizes;
    inline static std::vector<float> coverTileSizes;

    inline static std::vector<float> terrainMaxDisplacements;
    inline static std::vector<float> coverMaxDisplacements;

    inline static std::map<ETextureComponentOut, std::optional<GLuint>> terrainTextureArrays =
    {
        {ETextureComponentOut::DiffuseHeight, {} },
        {ETextureComponentOut::Normal, {} },
    };

    inline static std::map<ETextureComponentOut, std::optional<GLuint>> coverTextureArrays =
    {
        {ETextureComponentOut::DiffuseHeight, {} },
        {ETextureComponentOut::Normal, {} },
    };

    inline static std::optional<GLint> tileNoiseId;

public:
    static void generateTerrainResources();

    DTerrainChunk() = default;
    DTerrainChunk(const QSet<quint32>& terrainTexPackIds, const QSet<quint32>& biomeTexPackIds);

    // Drawable
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Terrain; };
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    const auto& getTerrainTextureIds() const { return terrainTextureIds; }
    const auto& getBiomeTextureIds() const { return biomeTextureIds; }
    const auto& getTerrainTextureArrays() const { return terrainTextureArrays; }
    const auto& getTextureTileSizes() const { return terrainTileSizes; }
    const auto& getTileNoiseDim() const { return tileNoiseDim; }
    const auto& getTileNoiseTexture() const { return tileNoise; }
    
    QVector4D debugColor;

    static const int tileNoiseDim = 256;
    static Texture tileNoise;
private:
    virtual void createShader() override;
    std::vector<quint32> terrainTextureIds;
    std::vector<quint32> biomeTextureIds;

    template<EGenerationStage>
    friend class Generation::StageGen;
    friend class Omnigen;

    FRIEND_OMNIBIN(DTerrainChunk);
};

inline void omniSave(const DTerrainChunk& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << reinterpret_cast<const QMap<ELOD, QSharedPointer<TerrainChunkGeometryData>>&>(object.geometry);
    omniBin << object.cachedBoundingBox;
    omniBin << object.terrainTextureIds;
    omniBin << object.biomeTextureIds;
}

inline void omniLoad(DTerrainChunk& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> reinterpret_cast<QMap<ELOD, QSharedPointer<TerrainChunkGeometryData>>&>(object.geometry);
    omniBin >> object.cachedBoundingBox;
    omniBin >> object.terrainTextureIds;
    omniBin >> object.biomeTextureIds;
}