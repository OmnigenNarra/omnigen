#include "stdafx.h"
#include "BuildingGenerator.h"

#include "Building.h"
#include "Omnigen.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanUtils.h"

void BuildingGenerator::generate()
{
    for (auto i = 0; i < lots.size(); i++)
    {
        spawnGeometryForLot(i);
    }

    Generation::Data::get()->setUrbanMeshes(buildingMeshes);
    DUrbanMesh::createResources();
}


void BuildingGenerator::spawnGeometryForLot(const int lotId)
{
    if (Omnigen::get()->getAssetsSection()->getAssets<EAsset::Structure>().empty())
        return;

    float scaleModifier = 4.f;
    const auto& lot = lots[lotId];

    const auto& center = lot.lot.getCenter();

    const auto& buildingAssets = Omnigen::get()->getAssetsSection()->getAssets<EAsset::Structure>();
    std::vector<QSharedPointer<OmnigenAsset<EAsset::Structure>>> buildings;

    for (auto&& b : buildingAssets | std::views::values)
        buildings << b;

    std::ranges::sort(buildings, [&center, scaleModifier](const QSharedPointer<OmnigenAsset<EAsset::Structure>>& a, const QSharedPointer<OmnigenAsset<EAsset::Structure>>& b) {
        return a->getPlacementData().convertToWorldSpace(center, scaleModifier).getArea()
    > b->getPlacementData().convertToWorldSpace(center, scaleModifier).getArea();
        });


    const IndexType buildingIdx = selectStructure(buildings, scaleModifier, lot.lot, center);
    if (buildingIdx == -1)
        return;

    std::uniform_int_distribution<int> dist(buildingIdx, buildings.size() - 1);

    const auto& building = buildings[dist(Generation::gRandomEngine)];

    auto spawnPosition = UrbanUtils::heightQuery(center);
    spawnPosition.setY(spawnPosition.y() + 20.f);

    QMatrix4x4 transform;
    transform.translate(spawnPosition);
    transform.scale(QVector3D{ 1,1,1 } * scaleModifier);

    std::uniform_real_distribution<float> distribution(0.f, 180.f);
    transform.rotate(distribution(Generation::gRandomEngine), QVector3D(0, 1, 0));

    auto urbanmesh = QSharedPointer<DUrbanMesh>::create(
        building, spawnPosition, scaleModifier);

    auto&& geom = urbanmesh->getInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();
    geom->instanceData <<= MeshAssetInstanceData{ transform };
    urbanmesh->initialize();

    QOmnigenViewport::registerDrawable(urbanmesh);

    buildingMeshes << urbanmesh;
}

IndexType BuildingGenerator::selectStructure(const std::vector<QSharedPointer<OmnigenAsset<EAsset::Structure>>>& inStructures, const float inScale, const Polygon2D& lot, const GVector2D center)
{
    for (auto i = 0; i < inStructures.size(); i++)
    {
        auto&& str = inStructures[i];
        auto&& bb = str->getPlacementData().convertToWorldSpace(center, inScale).bb;
        if (lot.containsConcave(bb.getBottomLeft())
            && lot.containsConcave(bb.getBottomRight())
            && lot.containsConcave(bb.getTopRight())
            && lot.containsConcave(bb.getTopLeft()))
        {
            /*bool overlap = false;
            for (auto&& b : buildingMeshes)
            {
                if (bb.overlaps(b->getBoundingBox()))
                {
                    overlap = true;
                    break;
                }
            }

            if (!overlap)*/
                return i;
        }
    }

    return inStructures.size() - 1;
}
