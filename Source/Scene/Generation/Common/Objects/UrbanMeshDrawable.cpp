#include "stdafx.h"
#include "UrbanMeshDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"

DUrbanMesh::DUrbanMesh(const QSharedPointer<OmnigenAsset<EAsset::Structure>>& inMeshAsset, const QVector3D& inLocation, const float inScale)
    : meshAsset(inMeshAsset), location(inLocation), scale(inScale)
{
    assignLodLevel(ELOD::Last, meshAsset->getMesh().getGeometry().at(ELOD::Zero));

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    gl->glGenBuffers(1, &instanceDataBufferId);
}

void DUrbanMesh::updateCullStatus(const OmnigenCamera& camera, int vIdx)
{
    bIsCulled = !shouldDraw(vIdx);
}

void DUrbanMesh::cacheBoundingBox()
{
    cachedBoundingBox.sizes = { getMaxGridCoord(), getMaxGridCoord(), getMaxGridCoord() };
}

void DUrbanMesh::bindShader(const OmnigenCamera& camera)
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

void DUrbanMesh::draw()
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

void DUrbanMesh::unbindShader()
{
    shaderPipeline.release();
}

void DUrbanMesh::createResources()
{
    if (Generation::Data::get()->getUrbanMeshes().empty())
        return;

    auto&& allTextures = QOmnigenAssetMgrSection::getAssets<EAsset::Texture>();

    for (auto&& uMesh : Generation::Data::get()->getUrbanMeshes())
    {
        auto&& assetMaterials = uMesh->meshAsset->getTextureIds();
        uMesh->gpuTextureViews.resize(assetMaterials.size());

        int i = 0;
        for (auto&& id : assetMaterials)
        {
            for (auto comp : std::vector{ ETextureComponentOut::DiffuseHeight, ETextureComponentOut::Normal })
                uMesh->gpuTextureViews[i][comp] = allTextures[id]->outputs.at(comp).loadIntoGL();

            i++;
        }
    }
}

void DUrbanMesh::drawBounds()
{

}

void DUrbanMesh::drawForwardArrow() const
{
    auto&& placementData = meshAsset->getPlacementData().convertToWorldSpace(location, scale);

    const Segment2D forwardSegment = placementData.forwardSegment;
    const float markerHeight = location.y() + (placementData.height);

    spawn<DLineMarker, true>(forwardSegment.first, (forwardSegment.second + ((forwardSegment.second - forwardSegment.first).normalized() * 10000.f)),
        Colors::red, markerHeight, ELineDecorator::Arrow);
}

void DUrbanMesh::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    shaderPipeline.addShaderFromSourceFile(QOpenGLShader::Vertex, "Resources/Shaders/Urban/UrbanMesh.vert");
    shaderPipeline.addShaderFromSourceFile(QOpenGLShader::Fragment, "Resources/Shaders/Urban/UrbanMesh.frag");
    bool ok = shaderPipeline.link();
    Q_ASSERT(ok);

    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    //shaderPipeline.set(EShaderUniform::TerrainTextureIds, "idxMap");

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

void DUrbanMesh::createShaderResources()
{
    auto&& geom = getActiveInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();

    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceDataBufferId);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, geom->instanceData.size() * geom->instanceSize(), geom->instanceData.data(), GL_STATIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
