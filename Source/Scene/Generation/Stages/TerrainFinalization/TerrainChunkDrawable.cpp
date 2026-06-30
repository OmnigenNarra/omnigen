#include "stdafx.h"
#include "TerrainChunkDrawable.h"
#include "Scene/Core/EditorGridDrawable.h"
#include <QImage>
#include <Qvector2D>
#include <QMessageBox>
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Utils/CoreUtils.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "Omnigen.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include <noise/noise.h>
#include <Scene/Generation/OmnigenGeneration.h>
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"
#include "Data/Assets/SoilMaterial/AssetSoilMaterial.h"

Texture DTerrainChunk::tileNoise;

QImage generateTileableNoiseTex(int w, int h)
{
    noise::module::Perlin noiseSource;
    noiseSource.SetPersistence(1.0);
    noiseSource.SetSeed(std::rand());

    noise::model::Plane noiseModel;
    noiseModel.SetModule(noiseSource);

    QImage noise(w, h, QImage::Format::Format_RGBA8888);

	for (size_t x = 0; x < w / 2 + 1; ++x)
		for (size_t y = 0; y < h / 2 + 1; ++y)
		{
			float perlinValue = noiseModel.GetValue(double(x) / double(w), double(y) / double(h));
			float clampedValue = (std::clamp(perlinValue, -0.98f, 1.0f) + 1.0f) * 0.5f;
			auto color = QColor(int(std::round(clampedValue * 255)), 0, 0, 255);
            noise.setPixelColor(x, y, color);
			noise.setPixelColor((w - 1) - x, (h - 1) - y, color);
			noise.setPixelColor(x, (h - 1) - y, color);
			noise.setPixelColor((w - 1) - x, y, color);
		}

    return noise;
}

void DTerrainChunk::generateTerrainResources()
{
    auto&& assetMgr = Omnigen::get()->getAssetsSection();

    // Terrain textures
    if (auto&& terrainData = Generation::Data::get()->getTerrainTextureArray(); !terrainData.empty())
    {
        auto&& lithoAssets = assetMgr->getAssets<EAsset::RockMaterial>();
        constexpr size_t cRockMaterialLayersCount = std::tuple_size_v<std::decay_t<decltype(lithoAssets[0]->getTextures())>>;

        terrainTileSizes.resize(terrainData.size() * cRockMaterialLayersCount);
        terrainMaxDisplacements.resize(terrainData.size() * cRockMaterialLayersCount);
        int globalTextureIdx = 0;
        for (qint64 assetId : terrainData)
        {
            auto&& asset = lithoAssets[assetId];

            for (int i = 0; i < cRockMaterialLayersCount; ++i)
            {
                auto&& tex = asset->getTextures()[i];
                terrainTileSizes[globalTextureIdx] = tex.tileSize;
                terrainMaxDisplacements[globalTextureIdx] = tex.maxDisplacement;
                ++globalTextureIdx;
            }
        }

        for (auto&& [key, glTexArrayId] : terrainTextureArrays)
        {
            if (glTexArrayId)
            {
                glDeleteTextures(1, &*glTexArrayId);
                glTexArrayId = {};
            }

            auto&& firstTex = lithoAssets.begin()->second->getTextures()[0].outputs.at(key);
            glTexArrayId = firstTex.initGLArray(terrainData.size() * cRockMaterialLayersCount);

            globalTextureIdx = 0;
            for (qint64 assetId : terrainData)
            {
                auto&& asset = lithoAssets[assetId];
                for (int i = 0; i < cRockMaterialLayersCount; ++i)
                {
                    auto&& tex = asset->getTextures()[i];
                    tex(key)->loadIntoGLArray(*glTexArrayId, globalTextureIdx++);
                }
            }
        }
    }

    // Cover textures
    if (auto&& coverData = Generation::Data::get()->getCoverTextureArray(); !coverData.empty())
    {
        auto&& coverAssets = assetMgr->getAssets<EAsset::SoilMaterial>();
        constexpr size_t cCoverMaterialLayersCount = std::tuple_size_v<std::decay_t<decltype(coverAssets[0]->getMaterials())>>;

        coverTileSizes.resize(coverData.size());
        coverMaxDisplacements.resize(coverData.size());
        int globalTextureIdx = 0;
        for (qint64 assetId : coverData)
        {
            auto&& asset = coverAssets[assetId];

            for (int i = 0; i < cCoverMaterialLayersCount; ++i)
            {
                auto&& tex = asset->getMaterials()[i];
                coverTileSizes[globalTextureIdx] = tex.tileSize;
                coverMaxDisplacements[globalTextureIdx] = tex.maxDisplacement;
                ++globalTextureIdx;
            }
        }

        for (auto&& [key, glTexArrayId] : coverTextureArrays)
        {
            if (glTexArrayId)
            {
                glDeleteTextures(1, &*glTexArrayId);
                glTexArrayId = {};
            }

            auto&& firstTex = coverAssets.begin()->second->getMaterials()[0].outputs.at(key);
            glTexArrayId = firstTex.initGLArray(coverData.size() * cCoverMaterialLayersCount);

            globalTextureIdx = 0;
            for (qint64 assetId : coverData)
            {
                auto&& asset = coverAssets[assetId];
                for (int i = 0; i < cCoverMaterialLayersCount; ++i)
                {
                    auto&& tex = asset->getMaterials()[i];
                    tex(key)->loadIntoGLArray(*glTexArrayId, globalTextureIdx++);
                }
            }
        }
    }

    tileNoise.set(ETextureComponentOut::DiffuseHeight, generateTileableNoiseTex(tileNoiseDim, tileNoiseDim));
    tileNoiseId = tileNoise.loadIntoGL();
}

DTerrainChunk::DTerrainChunk(const QSet<quint32>& terrainTexPackIds, const QSet<quint32>& biomeTexPackIds)
{
    terrainTextureIds.reserve(terrainTexPackIds.size());
    for (quint32 id : terrainTexPackIds)
        terrainTextureIds << id;

    int terSize = terrainTexPackIds.size();

    biomeTextureIds.reserve(biomeTexPackIds.size());
    for (quint32 id : biomeTexPackIds)
        biomeTextureIds << id;

    static const std::uniform_real_distribution<float> channelDist(0.0f, 1.0f);
    //debugColor = { channelDist(Generation::gRandomEngine), channelDist(Generation::gRandomEngine), channelDist(Generation::gRandomEngine), 1 };
}

void DTerrainChunk::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
    shaderPipeline.setUniformValueArray(EShaderUniform::TerrainTileSizes, terrainTileSizes);
    //shaderPipeline.setUniformValueArray(EShaderUniform::CoverTileSizes, coverTileSizes);
    //shaderPipeline.setUniformValueArray(EShaderUniform::CoverMaxDisplacements, coverMaxDisplacements);

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    if (tileNoiseId)
    {
        shaderPipeline.setUniformValue(EShaderUniform::Texture7, 7);
        gl->glActiveTexture(GL_TEXTURE7);
        gl->glBindTexture(GL_TEXTURE_2D, *tileNoiseId);
    }

    int i = 0;
    if (auto [key, glTex] = *terrainTextureArrays.begin(); glTex)
    {
        for (auto&& [key, glTex] : terrainTextureArrays)
        {
            // 0 = terrainDiffuseHeight
            // 1 = terrainNormal
            shaderPipeline.setUniformValue(EShaderUniform(int(EShaderUniform::Texture0) + i), i);
            gl->glActiveTexture(GL_TEXTURE0 + i);
            gl->glBindTexture(GL_TEXTURE_2D_ARRAY, *glTex);
            ++i;
        }
    }

    if (auto [key, glTex] = *coverTextureArrays.begin(); glTex)
    {
        for (auto&& [key, glTex] : coverTextureArrays)
        {
            // 2 = coverDiffuseHeight
            // 3 = coverNormal
            shaderPipeline.setUniformValue(EShaderUniform(int(EShaderUniform::Texture0) + i), i);
            gl->glActiveTexture(GL_TEXTURE0 + i);
            gl->glBindTexture(GL_TEXTURE_2D_ARRAY, *glTex);
            ++i;
        }
    }
}

void DTerrainChunk::cacheBoundingBox()
{
    // filled during gen
}

void DTerrainChunk::draw()
{
    auto&& geom = getActiveGeometry<TerrainMeshVertex>();
    auto& vbo = geom->vbo;

    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, debugColor/* QVector4D()*/);
    ShaderPipeline::current->setUniformValueArray(EShaderUniform::TerrainTextureIds, terrainTextureIds);
    ShaderPipeline::current->setUniformValueArray(EShaderUniform::BiomeTextureIds, biomeTextureIds);

    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(TerrainMeshVertex, position), 3, geom->size());
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(TerrainMeshVertex, normal), 3, geom->size());
    ShaderPipeline::current->setAttributeBufferI(EShaderAttribute::TexID0, GL_UNSIGNED_INT, offsetof(TerrainMeshVertex, terrainTexWeights), 1, geom->size());
    ShaderPipeline::current->setAttributeBufferI(EShaderAttribute::TexID1, GL_UNSIGNED_INT, offsetof(TerrainMeshVertex, biomeTexWeights), 1, geom->size());
    ShaderPipeline::current->setAttributeBufferI(EShaderAttribute::UV, GL_UNSIGNED_INT, offsetof(TerrainMeshVertex, packParams), 1, geom->size());
    vbo.release();

    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, indices.data());
}

void DTerrainChunk::unbindShader()
{
    shaderPipeline.release();
}

void DTerrainChunk::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    shaderPipeline.addShaderFromSourceFile(QOpenGLShader::Vertex, "Resources/Shaders/Terrain/TerrainMaterial.vert");
    shaderPipeline.addShaderFromSourceFile(QOpenGLShader::Fragment, "Resources/Shaders/Terrain/TerrainMaterial.frag");
    bool ok = shaderPipeline.link();
    Q_ASSERT(ok);

    // Set shader parameters' locations.
    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::TerrainTileSizes, "terrainTileSizes");
    shaderPipeline.set(EShaderUniform::TerrainMaxDisplacements, "terrainMaxDisplacements");
    //shaderPipeline.set(EShaderUniform::CoverTileSizes, "coverTileSizes");
    //shaderPipeline.set(EShaderUniform::CoverMaxDisplacements, "coverMaxDisplacements");
    shaderPipeline.set(EShaderUniform::TerrainTextureIds, "terrainTexIds");
    shaderPipeline.set(EShaderUniform::BiomeTextureIds, "biomeTexIds");
    shaderPipeline.set(EShaderUniform::Texture7, "tilingNoise");

    shaderPipeline.set(EShaderUniform::Texture0, "terrainDiffuseHeight");
    shaderPipeline.set(EShaderUniform::Texture1, "terrainNormal");
    shaderPipeline.set(EShaderUniform::Texture2, "coverDiffuseHeight");
    shaderPipeline.set(EShaderUniform::Texture3, "coverNormal");

    shaderPipeline.set(EShaderAttribute::Position, "pos");
    shaderPipeline.set(EShaderAttribute::Normal, "normal");
    shaderPipeline.set(EShaderAttribute::TexID0, "terrainWeights");
    shaderPipeline.set(EShaderAttribute::TexID1, "biomeWeights");
    shaderPipeline.set(EShaderAttribute::UV, "packParams");

    shaderPipeline.set(EShaderUniform::Color0, "debugColor");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}
