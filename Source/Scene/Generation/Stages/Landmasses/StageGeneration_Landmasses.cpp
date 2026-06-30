#include "stdafx.h"
#include "StageGeneration_Landmasses.h"
#include "ShorelineMarker.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "LandmassBoundMarker.h"
#include "Scene/Generation/Stages/Layout/StageGeneration_Layout.h"
#include "LandmassMarker.h"
#include "SeamassMarker.h"
#include "Editor/StageTools/StageTools.h"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

namespace Generation
{
    void StageGen<EGenerationStage::Landmasses>::initialize()
    {
        DShorelineMarker::generateInit();
    }

    // Generate continents and islands, their shorelines and boundaries for later steps.
    bool StageGen<EGenerationStage::Landmasses>::autoGen()
    {
        if (!DShorelineMarker::generateAll())
            return false;

        return true;
    }

    void StageGen<EGenerationStage::Landmasses>::clear()
    {
        Data::get()->clearExactMarkers<DLandmassBound>();
        Data::get()->clearExactMarkers<DShorelineMarker>();
        Data::get()->clearExactMarkers<DLandmassMarker>();
        Data::get()->clearExactMarkers<DSeamassMarker>();
        Data::get()->setDomainHeightBounds({});
    }

    bool StageGen<EGenerationStage::Landmasses>::validate()
    {
        if (Generation::Data::get()->getMarkers<DLandmassMarker>().empty())
        {
            OmniLog(ELoggingLevel::Error) <<= "No landmass found!";
            return false;
        }

        return true;
    }

    void StageGen<EGenerationStage::Landmasses>::finalize()
    {
        getStageTools<EGenerationStage::Landmasses>()->changeTo3DShorelines();

        auto&& shorelines = Generation::Data::get()->getMarkers<DShorelineMarker>();
        for (auto&& shoreline : shorelines)
            shoreline->detectBays();

        // Compute heights on domain boundaries.
        std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>> heightBounds;
        {
            computeDomainBoundHeights(&heightBounds);
            computeSimplifiedShorelinesPerDomain(&heightBounds);
        }

        Data::get()->setDomainHeightBounds(heightBounds);
        DLandmassBound::generateAll();
    }

    void StageGen<EGenerationStage::Landmasses>::computeDomainBoundHeights(std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>* result)
    {
        auto terrainDomains = Generation::Data::get()->getAllDomains<EDomainType::Terrain>();

        for (int i = 0; i < terrainDomains.size(); ++i)
        {
            auto&& td1 = terrainDomains[i].second;
            for (int j = 0; j < terrainDomains.size(); ++j)
            {
                if (i == j)
                    continue;

                auto&& td2 = terrainDomains[j].second;
                int maxHeight = std::round(std::min(td1->getData<EDomainType::Terrain>()->maxHeight, td2->getData<EDomainType::Terrain>()->maxHeight));
                auto segments = Generation::Utils::computeSharedPerimeter(td1, td2);

                for (auto&& segment : segments)
                    (*result)[td1->getGuid()][Generation::HeightBoundOrigin::Domain][td2->getGuid()][maxHeight] << segment;
            }
        }
    }

    void StageGen<EGenerationStage::Landmasses>::computeSimplifiedShorelinesPerDomain(std::map<qint64, std::map<Generation::HeightBoundOrigin, std::map<qint64, std::map<int, std::vector<Segment2D>>>>>* result)
    {
        auto shorelineMarkers = Generation::Data::get()->getMarkers<DShorelineMarker>();
        static const int skipFactor = 5; // take every skipFactor'th point from shores

        for (auto&& shoreline : shorelineMarkers)
        {
            auto&& pts = shoreline->getControlPoints();
            auto&& heights = shoreline->getHeights();
            auto&& peninsula = shoreline->getPeninsulas();

            tbb::spin_mutex push_guard;

            std::vector<int> indicesConsidered(1, 0);
            indicesConsidered.reserve(pts.size() / 10 + 2);

            for (int i = 1; i < pts.size(); i += skipFactor)
                indicesConsidered << i;

            if (indicesConsidered.back() != int(pts.size()) - 1)
                indicesConsidered << int(pts.size()) - 1;

            tbb::parallel_for(tbb::blocked_range<int>(1, indicesConsidered.size()), [&](tbb::blocked_range<int> r)
                {
                    for (int ic = r.begin(); ic < r.end(); ++ic)
                    {
                        int i = indicesConsidered[ic];
                        int i2 = indicesConsidered[ic - 1];
                        float boundHeight = (heights[i] + heights[i2]) * 0.5f;

                        Segment2D s = { pts[i], pts[i2] };

                        GPoint g1 = GVector2D(pts[i]).toGPoint();
                        GPoint g2 = GVector2D(pts[i2]).toGPoint();

                        if (g1 == g2)
                        {
                            auto domain = Generation::Data::get()->getDomainAtSquare(g1, EDomainType::Terrain);

                            // Critical section
                            std::scoped_lock lock(push_guard);
                            (*result)[domain->getGuid()][Generation::HeightBoundOrigin::Shoreline][shoreline->getGuid()][boundHeight] << s;
                        }
                        else
                        {
                            auto domain1 = Generation::Data::get()->getDomainAtSquare(g1, EDomainType::Terrain);
                            auto domain2 = Generation::Data::get()->getDomainAtSquare(g2, EDomainType::Terrain);
                            Q_ASSERT(domain1 || domain2);

                            if (!domain1)
                                domain1 = domain2;

                            if (!domain2)
                                domain2 = domain1;

                            // Critical sections
                            {
                                std::scoped_lock lock(push_guard);
                                (*result)[domain1->getGuid()][Generation::HeightBoundOrigin::Shoreline][shoreline->getGuid()][boundHeight] << s;
                            }

                            if (domain1->getGuid() != domain2->getGuid())
                            {
                                std::scoped_lock lock(push_guard);
                                (*result)[domain2->getGuid()][Generation::HeightBoundOrigin::Shoreline][shoreline->getGuid()][boundHeight] << s;
                            }
                        }
                    }
                });
        }
    }
}