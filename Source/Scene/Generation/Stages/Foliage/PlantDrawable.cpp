#include "stdafx.h"
#include "PlantDrawable.h"
#include "Data/Assets/Plant/AssetPlant.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Omnigen.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"

#include <tbb/parallel_for.h>

namespace Generation
{
    DPlant::DPlant(const QSharedPointer<OmnigenAsset<EAsset::Plant>>& inAtlas, int inIdx)
        : meshAtlasAsset(inAtlas)
        , atlasIdx(inIdx)
    {
        auto&& meshComponent = meshAtlasAsset->getMeshes()[atlasIdx];

        auto geometry = meshComponent.getGeometry().at(ELOD::Zero);
        assignLodLevel(ELOD::Last, geometry);
        //assignLodLevel(ELOD::Mid, geometry);
        //assignLodLevel(ELOD::Last, QSharedPointer<GrassGeometry>::create()); // empty geom beyond Mid

        auto* gl = QOpenGLContext::currentContext()->extraFunctions();
        gl->glGenBuffers(1, &instanceDataBufferId);
    }

    DPlant::~DPlant()
    {
        auto&& geom = getInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();
        geom->instanceData.clear();
    }

    void DPlant::cacheBoundingBox()
    {
        cachedBoundingBox = BoundingBox::fromPoints(getInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>(ELOD::Last)->vertices, [](const MeshAssetVertex& gv) { return gv.position; });
        //cachedBoundingBox = BoundingBox::fromPoints(getInstancedGeometry<GrassVertex, GrassInstanceData>(ELOD::Mid)->vertices, [](const GrassVertex& gv) { return gv.pos; });
    }

    void DPlant::bindShader(const OmnigenCamera& camera)
    {
        shaderPipeline.bind();
        shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());

        int texUnitIdx = 0;
        for (auto comp : std::vector{ ETextureComponentOut::DiffuseHeight, ETextureComponentOut::Normal })
        {
            for (int i = 0; i < 4; ++i, ++texUnitIdx)
            {
                if (i < gpuTextureViews.size())
                {
                    int texLocation = shaderPipeline.uniforms[shaderTextureMap[comp]] + i;
                    shaderPipeline.QOpenGLShaderProgram::setUniformValue(texLocation, texUnitIdx);
                }
            }
        }
    }

    void DPlant::unbindShader()
    {
        shaderPipeline.release();
    }

    void DPlant::draw()
    {
        // Per model
        auto* gl = QOpenGLContext::currentContext()->extraFunctions();
        int texUnitIdx = 0;
        for (auto comp : std::vector{ ETextureComponentOut::DiffuseHeight, ETextureComponentOut::Normal })
        {
            for (int i = 0; i < 4; ++i, ++texUnitIdx)
            {
                if (i < gpuTextureViews.size())
                {
                    gl->glActiveTexture(GL_TEXTURE0 + texUnitIdx);
                    gl->glBindTexture(GL_TEXTURE_2D, gpuTextureViews[i][comp]);
                }
            }
        }

        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceDataBufferId);

        auto&& geom = getActiveInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();
        auto& vbo = geom->vbo;
        auto& ibo = geom->instanceBuffer;

        // Per vertex
        vbo.bind();
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(MeshAssetVertex, position), 3, geom->size());
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(MeshAssetVertex, normal), 3, geom->size());
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::UV, GL_FLOAT, offsetof(MeshAssetVertex, uv), 2, geom->size());
        ShaderPipeline::current->setAttributeBufferI(EShaderAttribute::TexID0, GL_INT, offsetof(MeshAssetVertex, materialId), 1, geom->size());
        vbo.release();

        // Per instance
        ibo.bind();
        ShaderPipeline::current->setAttributeBufferI(EShaderAttribute::InstanceWorldMtx, GL_UNSIGNED_INT, 0, 1, sizeof(IndexType), true);
        ibo.release();

        auto& indices = getActiveGeometry()->indices;
        gl->glDrawElementsInstanced(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, indices.data(), geom->visibleInstanceCount());
    }

    const PlantPlacementData& DPlant::getPlacementData(EBiomeLayer layer)
    {
        return meshAtlasAsset->placementData[atlasIdx].at(layer);
    }

    void DPlant::createShader()
    {
        if (shaderPipeline.isLinked())
            return;

        shaderPipeline.addShaderFromSourceFile(QOpenGLShader::Vertex, "Resources/Shaders/Urban/UrbanMesh.vert");
        shaderPipeline.addShaderFromSourceFile(QOpenGLShader::Fragment, "Resources/Shaders/Urban/UrbanMesh.frag");
        bool ok = shaderPipeline.link();

        shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
        shaderPipeline.set(EShaderUniform::Texture0, "diffuseTextures");
        shaderPipeline.set(EShaderUniform::Texture1, "normalTextures");

        shaderPipeline.set(EShaderUniform::WorldMtx, "allWorldMatrices");

        shaderPipeline.set(EShaderAttribute::Position, "pos");
        shaderPipeline.set(EShaderAttribute::Normal, "normal");
        shaderPipeline.set(EShaderAttribute::UV, "UV");
        shaderPipeline.set(EShaderAttribute::TexID0, "matId");
        shaderPipeline.set(EShaderAttribute::InstanceWorldMtx, "gInstanceIdx", true);

        shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
    }


    void DPlant::createShaderResources()
    {
        auto&& geom = getActiveInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();

        auto* gl = QOpenGLContext::currentContext()->extraFunctions();
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceDataBufferId);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, geom->instanceData.size() * geom->instanceSize(), geom->instanceData.data(), GL_STATIC_DRAW);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void DPlant::createResources(const std::vector<QSharedPointer<DPlant>>& spawnedPlants)
    {
        if (spawnedPlants.empty())
            return;

        // Create GPU texture views
        for (auto&& plant : spawnedPlants)
        {
            auto&& assetMaterials = plant->meshAtlasAsset->getMaterials();
            plant->gpuTextureViews.resize(assetMaterials.size());

            for (int i = 0; i < assetMaterials.size(); ++i)
                for (auto comp : std::vector{ ETextureComponentOut::DiffuseHeight, ETextureComponentOut::Normal })
                    plant->gpuTextureViews[i][comp] = assetMaterials[i].outputs.at(comp).loadIntoGL();
        }
    }
}