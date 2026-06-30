#include "stdafx.h"
#include "UrbanSite.h"

#include "Omnigen.h"
#include <Editor/Sections/Profiler/OmnigenProfiler.h>

#include "../../UrbanLayout/UrbanSuggestion.h"
#include "../Buildings/Building.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"

#include "tbb/parallel_for.h"
#include "Utils/Clipper/clipper.hpp"

namespace Generation
{
    void UrbanSite::generateSites()
    {
        if (Omnigen::get()->getAssetsSection()->getAssets<EAsset::Structure>().empty())
            return;

        OmniProfile("Urban Sites: Generation");

        std::vector<QSharedPointer<UrbanSite>> sites;

        for (auto&& suggestion : Data::get()->getUrbanSuggestions())
        {
            if (!suggestion->getShouldGenerate())
                continue;

            const auto areaSize = suggestion->getAreaSize();
            const auto site = QSharedPointer<UrbanSite>::create(
                UrbanUtils::calculateSiteArea(suggestion->getInitialCellId(), UrbanUtils::getUrbanSizeAsFloat(areaSize)).second,
                areaSize
            );

            site->setName(suggestion->getName());
            site->setWantsPerimeterRoads(suggestion->getGenPerimeterRoads());

            sites.push_back(site);
        }

        for (auto&& site : sites)
            site->generate();

        /*tbb::parallel_for(0, int(sites.size()), [&](int i)
            {
                sites[i]->generate();
            });*/

        Data::get()->setUrbanSites(sites);
    }

    UrbanSite::UrbanSite(const QSet<int>& inArea, const EUrbanSize urbanSize)
        : urbanSize(urbanSize)
        , area(inArea)
        , guid(makeGuid())
    {
        mergeArea();

        for (auto&& id : area)
        {
            auto&& cell = Data::get()->getTerrainCells()->getCellAt(id);

            for (auto&& neighbour_id : cell.getNeighbors().keys())
            {
                if (area.contains(neighbour_id))
                    continue;

                neighbourArea.insert(neighbour_id);
            }
        }

        computeEnvBounds();
    }

    void UrbanSite::generate()
    {
        displaySite();

        if (!areaPolygon.getPts().empty())
            areaPolygon.debugPlot({ 1, 0, 1, 1 }, -85.f);

        network = DistrictNetwork({ areaPolygon, urbanSize }, wantsPerimeterRoads);

        computeTopology();

        {
            OmniProfile("Building Generation");
            generateBuildings();
        }
    }

    void UrbanSite::revertGeneration()
    {
        topologyGenerator->clear();

        topologyGenerator.reset();
        buildingGenerator.reset();
    }

    void UrbanSite::displaySite(const QVector4D& inColor)
    {
        for (auto&& id : area)
        {
            auto&& poly = Data::get()->getTerrainCells()->getCellAt(id).getPolygon();
            Data::get()->createMarker<DPolygonMarker>(std::vector<QVector3D>(poly.getPts().begin(), poly.getPts().end()), 0.0f, QVector4D(0.5, 0.5, 0.5, 1));
        }
    }

    void UrbanSite::mergeArea()
    {
        std::vector<Polygon2D> sitePolygons;
        for (int id : area)
            sitePolygons <<= Data::get()->getTerrainCells()->getCellAt(id).getPolygon();

        areaPolygon = mergePolygons(sitePolygons).front();
    }

    void UrbanSite::computeTopology()
    {
        OmniProfile("Topology Generation", true);

        auto pEdges = network.getPrimaryRoadEdges();
        topologyGenerator = std::make_shared<UrbanTopologyGenerator>(this, pEdges);

        topologyGenerator->generate();
    }

    void UrbanSite::generateDistrictVoronoi(const std::vector<DistrictCreationInfo>& info) const
    {
        OmniProfile("District Generation", true);

        network.calculateDistricts(info);
    }

    void UrbanSite::generateBuildings()
    {
        buildingGenerator = std::make_unique<BuildingGenerator>(topologyGenerator->getBuildingLots());

        buildingGenerator->generate();
    }

    void UrbanSite::computeEnvBounds()
    {
        auto&& bounds = Data::get()->getEnviroBounds();

        std::unordered_set<qint64> includedBounds;

        for (int cellIdx : area)
        {
            for (auto&& bound : bounds[cellIdx])
            {
                if (!includedBounds.contains(bound->guid))
                {
                    includedBounds.insert(bound->guid);
                    envBounds << bound;
                }
            }
        }
    }
}
