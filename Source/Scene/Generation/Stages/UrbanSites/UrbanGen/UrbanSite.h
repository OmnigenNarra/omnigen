#pragma once

#include "Editor/StageTools/StageToolsBase.h"
#include "Scene/Generation/OmnigenGenerationStage.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Utils/Polygon.h"
#include "../../UrbanLayout/UrbanUtils.h"
#include "../Buildings/BuildingGenerator.h"
#include "Core/DistrictNetwork.h"
#include "Roads/UrbanTopologyGenerator.h"

class DLineMarker;

void omniSave(const Generation::UrbanSite& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::UrbanSite& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    class TerrainBlockClusterBase;
    class TerrainBlockBase;

    class UrbanSite
    {
    public:
        virtual ~UrbanSite() = default;
        static void generateSites();

        UrbanSite() = default;
        UrbanSite(const QSet<int>& inArea, EUrbanSize urbanSize);

        void generate();
        void revertGeneration();

        void displaySite(const QVector4D& inColor = QVector4D(1, 1, 0, 1));
        const EUrbanSize& getUrbanSize() const { return urbanSize; }
        const QSet<int>& getNeighbourAreaIds() const { return neighbourArea; }
        const QSet<int>& getAreaIds() const { return area; }
        Polygon2D getAreaPolygon() const { return areaPolygon; }

        bool getIsSelected() const { return isSelected; }
        qint64 getGuid() const { return guid; }

        const DistrictNetwork& getDistrictNetwork() const { return network; }
    
        void setIsSelected(const bool val) { isSelected = val; }

        QString getName() const { return townName; }
        void setName(const QString& newName) { townName = newName; }

        bool getWantsPerimeterRoads() const { return wantsPerimeterRoads; }
        void setWantsPerimeterRoads(const bool val) { wantsPerimeterRoads = val; }

        [[nodiscard]] const auto& getEnvironmentBounds() const { return envBounds; }
        [[nodiscard]] const std::vector<std::vector<QVector3D>>& getEnvironmentalLines() const { return envBoundLines; }

        const auto& getTopologyGenerator() const { return topologyGenerator; }

        //TODO: Needs new structure
        void generateDistrictVoronoi(const std::vector<DistrictCreationInfo>& info) const;
    protected:
        void mergeArea();
        void computeEnvBounds();

        void computeTopology();
        void generateBuildings();

        EUrbanSize urbanSize;

        QSet<int> neighbourArea;
        QSet<int> area;
        Polygon2D areaPolygon;
        Polygon2D concaveHull;

        mutable DistrictNetwork network;

        std::shared_ptr<UrbanTopologyGenerator> topologyGenerator;
        std::unique_ptr<BuildingGenerator> buildingGenerator;

        std::vector<QSharedPointer<EnvBound>> envBounds;
        std::vector<std::vector<QVector3D>> envBoundLines;

        qint64 guid;
        QString townName = "Default";

        bool isSelected = false;
        bool wantsPerimeterRoads = true;

        friend Design::StageTools<EGenerationStage::UrbanSites>;
        FRIEND_OMNIBIN_NS(UrbanSite);
    };
}

inline void omniSave(const Generation::UrbanSite& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.urbanSize;
    omniBin << object.area;
    omniBin << object.areaPolygon;
    omniBin << object.network;
    omniBin << object.guid;
    omniBin << object.townName;
}

inline void omniLoad(Generation::UrbanSite& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.urbanSize;
    omniBin >> object.area;
    omniBin >> object.areaPolygon;
    omniBin >> object.network;
    omniBin >> object.guid;
    omniBin >> object.townName;
}

