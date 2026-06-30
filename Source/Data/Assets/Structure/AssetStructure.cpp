#include "stdafx.h"
#include "AssetStructure.h"

#include <Mathematics/ConvexHull2.h>

#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include <Source/Omnigen.h>
#include <Source/Editor/Sections/Viewport/OmnigenCameraManager.h>
#include "Scene/Generation/Common/Objects/UrbanMeshDrawable.h"
#include "Editor/Dialogs/AssetCompiler/AssetCompilerDialog.h"
#include "Utils/ConcaveHull.h"
#include "Utils/FbxHelpers.h"

BuildingPlacementData BuildingPlacementData::convertToWorldSpace(const QVector3D& centerPt, const float scale) const
{
    // Create instance hull
    std::vector<GVector2D> instanceHull(hull.size());

    float r = 0.0f;
    for (int i = 0; i < hull.size(); ++i)
    {
        instanceHull[i] = GVector2D(hull[i] * scale);
        r = std::max(instanceHull[i].length(), r);
    }

    for (auto&& ihp : instanceHull)
        ihp = centerPt + ihp;

    BuildingPlacementData dataToReturn;
    dataToReturn.hull = instanceHull;
    dataToReturn.bb = Polygon2D(instanceHull).getEnclosingBB();
    dataToReturn.height = dataToReturn.bb.sizes.y() - dataToReturn.bb.nbl.y();

    const GVector2D bbCenter = dataToReturn.bb.getCenter();

    dataToReturn.forwardSegment = Segment2D(bbCenter, GVector2D(bbCenter.x + forwardVector.x(), bbCenter.z));

    return dataToReturn;
}

float BuildingPlacementData::getMaxGroundExtent() const
{
    return bb.sizes.x() >= bb.sizes.z() ? bb.sizes.x() : bb.sizes.z();
}

float BuildingPlacementData::getArea() const
{
    return Polygon2D(hull).getArea();
}

OmnigenAsset_Structure::OmnigenAsset()
{
    type = EAsset::Structure;
}

void OmnigenAsset_Structure::makeUniqueName()
{
    name = getUniqueName("Structure");
}

QSharedPointer<OmnigenPropertyListBase> OmnigenAsset_Structure::makePropertyList()
{
    auto props = QSharedPointer<OmnigenPropertyListBase>::create(id);

    auto textureIds = Omnigen::get()->getAssetsSection()->getAssetsIds<EAsset::Texture>();

    /*for (auto&& [matId, mat] : mesh.getMaterials())
    {
        props->addField(QSharedPointer<TOmnigenField<qint64, ComboFieldEditTexSlot>>::create(
            QString("Material #" + QString::number(matId)),
            [this, matId]()
            {
                return mesh.getMaterials().at(matId);
            },
            [this, matId](const qint64& textureId)
            {
                mesh.setMaterial(textureId, matId);
                return true;
            },
                [textureIds] { return new ComboFieldEditTexSlot<qint64>(textureIds, true); }
            ));
    }*/

    auto* generateButton = new QPushButton("Generate");
    QObject::connect(generateButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            auto spawnPosition = OmnigenCameraMgr::get()->getCameraForActiveViewport()->getPosition() + OmnigenCameraMgr::get()->getCameraForActiveViewport()->getLookAt() * 1000.0f;
            QMatrix4x4 transform;
            transform.translate(spawnPosition);
            transform.scale(QVector3D{ 1,1,1 } * 100);

            auto urbanmesh = QSharedPointer<DUrbanMesh>::create(QOmnigenAssetMgrSection::getAssets<EAsset::Structure>().at(id), spawnPosition, 1.f);

            auto&& geom = urbanmesh->getInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();
            geom->instanceData <<= MeshAssetInstanceData{ transform };
            urbanmesh->initialize();

            urbanmesh->drawForwardArrow();

            Generation::Data::get()->setUrbanMeshes({ urbanmesh });
            DUrbanMesh::createResources();

            emit Editable::created(urbanmesh);
        });

    props->setControlSection(generateButton);

    return props;
}

std::vector<QSharedPointer<OmnigenAssetBase>> OmnigenAsset_Structure::newAsset()
{
    std::vector<QSharedPointer<OmnigenAssetBase>> newAssets;

    for (auto&& fbxFile : QFileDialog::getOpenFileNames(nullptr, QObject::tr("Choose mesh files"), "", QObject::tr("Mesh files (*.fbx)")))
    {
        FBXLoader loader;
        FBXEssentialData fbxData = loader.loadFBXFile(fbxFile.toStdString());

        if (!fbxData.isLoaded)
            return {};

        std::map<int, qint64> fbxMaterialToAsset;

        for (auto&& material : fbxData.materials)
        {
            auto textureAsset = QSharedPointer<OmnigenAsset<EAsset::Texture>>::create();
            textureAsset->name = QString::fromStdString(material.name);

            // Hardcoded desc
            // TODO: Modularize
            const TextureComplilationDesc desc =
            {
                {ETextureComponentOut::DiffuseHeight, filled_array<4>(ETextureComponentIn::DiffuseAlpha)},
                {ETextureComponentOut::Normal, filled_array<4>(ETextureComponentIn::Normal)},
            };

            QAssetCompilerDialog textureCompiler(textureAsset, desc);

            if (material.textures.empty())
                continue;

            for (auto&& [fbxTextureType, fbxTextures] : material.textures)
            {
                for (auto&& fbxTexture : fbxTextures)
                {
                    if (auto assetType = toAssetTextureType(fbxTextureType); assetType)
                    {
                        textureCompiler.setComponent(*assetType, QImage(QString::fromStdString(fbxTexture.filepath)).mirrored());
                    }

                    //For now just take first texture
                    break;
                }
            }

            textureCompiler.compile();
            fbxMaterialToAsset[material.sceneId] = textureAsset->id;
            newAssets << textureAsset;
        }

        for (auto&& node : fbxData.nodes)
        {
            auto meshAsset = QSharedPointer<OmnigenAsset_Structure>::create();
            meshAsset->name = QString::fromStdString(node.name);
            

            MeshComponent newMeshComponent;
            newMeshComponent.setGeometry(ELOD::Zero, node.meshGeometry);

            meshAsset->setMesh(newMeshComponent);

            for (auto&& assetId : fbxMaterialToAsset | std::views::values)
            {
                meshAsset->addTexture(assetId);
            }

            meshAsset->computePlacementData();

            meshAsset->placementData.forwardVector = fbxData.forwardVector;

            newAssets << meshAsset;
        }
    }

    return newAssets;
}

void OmnigenAsset<EAsset::Structure>::addTexture(qint64 texId)
{
    textureAssetIds.insert(texId);
}

void OmnigenAsset<EAsset::Structure>::computePlacementData()
{
    //TODO: Add scaling to this
    auto&& vertices = mesh.getGeometry().at(ELOD::Zero)->vertices;
    auto&& bb = BoundingBox::fromPoints(vertices, [](const MeshAssetVertex& gv) { return gv.position; });

    std::vector<GVector2D> newHull;

    BuildingPlacementData newData;
    newData.bb = bb;
    newData.hull.reserve(vertices.size());
    newData.height = bb.sizes.y() - bb.nbl.y();

    // Process points for bounding box and hull
    for (auto&& p : vertices)
    {
        auto&& nbl = bb.nbl;

        auto&& point = p.position;

        //Is this needed?
        if (point.x() < nbl.x()) nbl.setX(point.x());
        if (point.z() < nbl.z()) nbl.setZ(point.z());

        newHull.push_back(point);
    }

    // Compute hull
    gte::ConvexHull2<float> convexHullBuilder;
    const bool ok = convexHullBuilder(newHull.size(), reinterpret_cast<const gte::Vector2<float>*>(newHull.data()), 0.001f);
    Q_ASSERT(ok);

    newData.hull = reinterpret_cast<std::vector<GVector2D>&&>(std::move(concaveman<float, 32>(reinterpret_cast<std::vector<std::array<float, 2>>&>(newHull), convexHullBuilder.GetHull())));

    placementData = newData;
}

void omniSave(const OmnigenAsset_Structure& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const MeshAssetBase&>(object);
}

void omniLoad(OmnigenAsset_Structure& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<MeshAssetBase&>(object);
}