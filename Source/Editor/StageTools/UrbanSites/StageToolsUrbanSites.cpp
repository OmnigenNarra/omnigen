#include "stdafx.h"
#include "StageToolsUrbanSites.h"

#include "Editor/StageTools/UrbanLayout/UrbanSelection.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/UrbanSite.h"
#include "Scene/Generation/Stages/UrbanSites/RuralGen/RuralRoadGenerator.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/Roads/RoadMarker.h"

namespace Design
{
    StageTools<EGenerationStage::UrbanSites>::StageTools()
        : StageToolsBase()
    {
    }

    void StageTools<EGenerationStage::UrbanSites>::bind()
    {
        StageToolsBase::bind();
    }

    void StageTools<EGenerationStage::UrbanSites>::unbind()
    {
        StageToolsBase::unbind();
    }

    SelectionMgrBase* StageTools<EGenerationStage::UrbanSites>::getSelectionMgr() const
    {
        static SelectionMgr<EUrbanSelection> manager;
        return &manager;
    }

    void StageTools<EGenerationStage::UrbanSites>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();

        writer << genData->getUrbanSites();
        writer << genData->getRuralRoadGenerator();
        genData->saveMarkers<DRoadMarker>(writer);
    }

    void StageTools<EGenerationStage::UrbanSites>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();

        std::vector<QSharedPointer<Generation::UrbanSite>> sites;
        size_t sitesCount;
        reader >> sitesCount;
        sites.resize(sitesCount);

        for (auto&& site : sites)
        {
            reader >> site;
        }

        genData->setUrbanSites(sites);

        QSharedPointer<RuralRoadGenerator> ruralRoads;
        reader >> ruralRoads;

        genData->setRuralRoadGenerator(ruralRoads);

        genData->loadMarkers<DRoadMarker>(reader);
    }
}
