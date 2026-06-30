#include "stdafx.h"
#include "StageGeneration_UrbanSites.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "UrbanGen/UrbanSite.h"
#include "UrbanGen/Roads/RoadMarker.h"
#include "RuralGen/RuralRoadGenerator.h"

namespace Generation
{
    bool StageGen<EGenerationStage::UrbanSites>::autoGen()
    {
        UrbanSite::generateSites();

        RuralRoadGenerator ruralRoads;
        ruralRoads.generate();

        Data::get()->setRuralRoadGenerator(QSharedPointer<RuralRoadGenerator>::create(std::move(ruralRoads)));

        return true;
    }

    void StageGen<EGenerationStage::UrbanSites>::clear()
    {
        Data::get()->clearExactMarkers<DRoadMarker>();

        if (auto&& roadGen = Data::get()->getRuralRoadGenerator(); roadGen)
            roadGen->revertGen();

        Data::get()->setRuralRoadGenerator({});

        for (auto&& site : Data::get()->getUrbanSites())
            site->revertGeneration();

        Data::get()->setUrbanSites({});
    }
}
