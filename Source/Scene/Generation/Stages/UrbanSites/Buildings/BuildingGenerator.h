#pragma once
#include "Data/Assets/AssetBase.h"
#include "Utils/Polygon.h"

class DUrbanMesh;
class Building;

struct BuildingLotInfo
{
    Polygon2D lot;
    Polygon2D districtBounds;
    bool isEnclosed;
    bool isDivided = true;
};

class BuildingGenerator
{
public:
    BuildingGenerator() = default;
    explicit BuildingGenerator(const std::vector<BuildingLotInfo>& inLots)
        : lots(inLots) {}

    void generate();
private:
    void spawnGeometryForLot(const int lotId);

    std::vector<GVector2D> getBuildingSeeds(const float minOffset, const float maxOffset, const Polygon2D& lotPolygon, const float areaThreshold);

    std::vector<BuildingLotInfo> lots;
    std::vector<QSharedPointer<DUrbanMesh>> buildingMeshes;

    IndexType selectStructure(const std::vector<QSharedPointer<OmnigenAsset<EAsset::Structure>>>& inStructures, const float inScale, const Polygon2D& lot, const GVector2D center);
};