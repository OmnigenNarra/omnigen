#include "stdafx.h"
#include "UrbanPlanner.h"

#include <ranges>

#include "UrbanUtils.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "../TerrainMods/TerrainModBase.h"
#include "../TerrainMods/TerrainModData.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"

namespace Generation
{
    void UrbanPlanner::SuggestionData::calculateArea()
    {
        auto [size, area] = UrbanUtils::calculateSiteArea(cellId, 30000.f);
        if (size < UrbanUtils::getUrbanSizeAsFloat(EUrbanSize::Outpost))
        {
            shouldDiscard = true;
            return;
        }

        maxAreaSize = UrbanUtils::getFloatAsUrbanSize(size);
        maxArea = area;

        std::tie(maxFlatlandSize, maxFlatlandArea) = UrbanUtils::calculateFlatlandArea(cellId);
    }

    void UrbanPlanner::calculateSuggestions()
    {
        OmniProfile("Urban Planner: Score Calculation");

        //Get flatland locations
        gatherFlatlandSeeds();

        //Rate them
        rateElevation();
        rateWaterProximity();
        rateMountainProximity();
        rateClusterSize();
        rateSphereOfInflunce();

        validateDistanceFromWorldBounds();

        applyWeightsToRates();

        //Success, you can now get the results with the getter
        hasCalculated = true;
    }

    std::vector<UrbanPlanner::SuggestionData> UrbanPlanner::getSuggestions(const int num, const bool printDebugData /* = false*/)
    {
        Q_ASSERT(num <= maxPotentialSuggestions);
        Q_ASSERT(hasCalculated);

        std::vector<SuggestionData> vecToReturn;
        std::ranges::sort(suggestionSeeds,
            [](const SuggestionData& a, const SuggestionData& b)
            {
                return a.finalScore > b.finalScore;
            });

        for (const auto& val : suggestionSeeds)
        {
            if (vecToReturn.size() >= num)
                break;

            if (!val.shouldDiscard)
                vecToReturn << val;
        }

        if (printDebugData)
            printScoresToLog(vecToReturn);

        return vecToReturn;
    }

    void UrbanPlanner::gatherFlatlandSeeds()
    {
        OmniProfile("Seed Gathering");

        for (auto&& metaClusterVec : Generation::Data::get()->getTerrainMetaClusters())
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                {
                    if (cluster->type != ETerrainBlock::Flatland)
                        continue;

                    const int cellId = *(cluster->cells.begin());
                    auto&& cell = Data::get()->getTerrainCells()->getCellAt(cellId);

                    QVector3D centerPoint = cell->getCenter();

                    if (auto potentialPts = Utils::castPointTo3D(cell->getCenter()); !potentialPts.empty())
                    {
                        centerPoint = potentialPts[0];
                    }

                    auto newSuggestion = SuggestionData(cellId, centerPoint);
                    newSuggestion.calculateArea();

                    bool discardSeed = false;

                    for (auto&& data : suggestionSeeds)
                        if (data.shouldDiscard || distance(data.seed, newSuggestion.seed) <
                            (data.maxFlatlandSize < UrbanUtils::getUrbanSizeAsFloat(EUrbanSize::LargeTown) ? data.maxFlatlandSize * 2 : data.maxFlatlandSize / 2))
                        {
                            discardSeed = true;
                            break;
                        }

                    if (!discardSeed)
                        suggestionSeeds << newSuggestion;

                    if (suggestionSeeds.size() >= maxPotentialSuggestions)
                        return;
                }
    }

    void UrbanPlanner::rateSphereOfInflunce()
    {
        OmniProfile("Urban Planner: Sphere of Influence");

        for (auto&& data : suggestionSeeds)
        {
            for (auto&& data2 : suggestionSeeds)
            {
                //setScoreFromId(data.cellId, (data.maxFlatlandSize * 2 / d) / 100.f);
            }
        }
    }

    void UrbanPlanner::rateElevation()
    {
        OmniProfile("Urban Planner: Elevation");

        //TODO: Sample more than 1 area point?
        float maxHeight = 0.f;
        for (auto& data : suggestionSeeds)
        {
            auto&& seed = data.seed;
            if (seed.y() > maxHeight)
                maxHeight = seed.y();
        }

        for (auto&& data : suggestionSeeds)
        {
            auto&& seed = data.seed;
            setScoreFromId(data.cellId, ESuggestionType::Elevation,(maxHeight + seed.y()));
        }
    }

    void UrbanPlanner::rateWaterProximity()
    {
        OmniProfile("Urban Planner: Water");

        auto&& riverMap = Data::get()->getTerrainMods()[ETerrainMod::River];
        for (auto&& data : suggestionSeeds)
        {
            for (auto&& area : data.maxArea)
            {
                auto river = std::find_if(riverMap.begin(), riverMap.end(), [&](const auto& r) {return r->getArea().contains(area); });

                if (river != riverMap.end())
                {
                    setScoreFromId(data.cellId, ESuggestionType::WaterProximity, 200.f);
                    goto river_cnt;
                }
            }

        river_cnt:;
        }

        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        for (auto&& data : suggestionSeeds)
        {
            for (auto&& area : data.maxArea)
            {
                auto&& cluster = clusterMap[area];
                if (cluster->type == ETerrainBlock::Beach)
                {
                    setScoreFromId(data.cellId, ESuggestionType::WaterProximity, 150.f);
                    goto beach_cnt;
                }
            }

        beach_cnt:;
        }
    }

    void UrbanPlanner::rateMountainProximity()
    {
        OmniProfile("Urban Planner: Mountain");

        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& terrainCells = Data::get()->getTerrainCells()->getCells();
        auto ridgeMap = clusterMap | std::views::filter([](const auto& c) {return c->type == ETerrainBlock::Ridge; });

        if (!ridgeMap.empty())
        {
            std::vector<GVector2D> ridgeCenters;
            for (auto& ridgeCluster : ridgeMap)
                for(int cellId : ridgeCluster->cells)
                    ridgeCenters.push_back(terrainCells[cellId]->getCenter());

            for (auto&& data : suggestionSeeds)
            {
                float minDistanceToRidge = std::numeric_limits<float>::max();

                for (auto& ridgeCenter : ridgeCenters)
                    minDistanceToRidge = std::min(minDistanceToRidge, ridgeCenter.dist(data.seed));

                setScoreFromId(data.cellId, ESuggestionType::MountainProximity, minDistanceToRidge);
            }
        }
    }

    void UrbanPlanner::rateClusterSize()
    {
        OmniProfile("Urban Planner: Cluster Size");

        for (auto&& data : suggestionSeeds)
        {
            if (data.maxFlatlandArea.size() < 4 || data.maxFlatlandSize < UrbanUtils::getUrbanSizeAsFloat(EUrbanSize::Outpost))
            {
                data.shouldDiscard = true;
                continue;
            }

            //TODO: This is a bit naive
            data.maxAreaSize = UrbanUtils::getFloatAsUrbanSize(data.maxFlatlandSize);

            //Then rate
            //...

        }

    }

    void UrbanPlanner::validateDistanceFromWorldBounds()
    {
        OmniProfile("Urban Planner: World Bounds");

        QSet<GPoint> worldSquares = Generation::Data::get()->getAllSquares();
        auto worldPerimeter = std::get<std::vector<Segment2D>>(computePerimeterForSquares(worldSquares));

        for (auto&& data : suggestionSeeds)
            if (std::any_of(worldPerimeter.begin(), worldPerimeter.end(), [&](const auto& s) { return s.dist(data.seed) < GRID_SEGMENT_WIDTH * 0.5f; }))
                data.shouldDiscard = true;
    }

    void UrbanPlanner::applyWeightsToRates()
    {
        for (auto& type : magic_enum::enum_values<ESuggestionType>())
        {
            std::vector<size_t> suggestionIndexes;
            std::vector<double> suggestionsScores;

            for (size_t i = 0; i < suggestionSeeds.size(); i++)
            {
                if (!suggestionSeeds[i].shouldDiscard && suggestionSeeds[i].scorePerType.contains(type))
                {
                    suggestionIndexes.push_back(i);
                    suggestionsScores.push_back(suggestionSeeds[i].scorePerType[type]);
                }
            }

            if (!suggestionsScores.empty())
            {
                std::vector<double> adjustedScores = softmax(suggestionsScores);

                for (size_t i = 0; i < suggestionsScores.size(); i++)
                    suggestionSeeds[suggestionIndexes[i]].scorePerType[type] = adjustedScores[i];
            }
        }

        for (auto& data : suggestionSeeds)
            for (auto& type : magic_enum::enum_values<ESuggestionType>())
                data.finalScore += data.scorePerType[type] * 1.0f;//data.weightPerType[type];
    }

    float UrbanPlanner::getScoreFromId(const int id) const
    {
        const auto result = std::ranges::find_if(suggestionSeeds, [id](const SuggestionData& e)
        {
            return e.cellId == id;
        });
        
        return result != suggestionSeeds.end()? result->finalScore : -1.f;
    }

    void UrbanPlanner::setScoreFromId(const int id, ESuggestionType type, const float newScore)
    {
        auto result = std::ranges::find_if(suggestionSeeds, [id](const SuggestionData& e)
        {
            return e.cellId == id;
        });

        if (result == suggestionSeeds.end())
            return;

        result->scorePerType[type] = newScore;
    }

    void UrbanPlanner::printScoresToLog(const std::vector<SuggestionData>& data)
    {
        for (auto&& e : data)
        {
            OmniLog(ELoggingLevel::Info) << "Suggestion: " << e.cellId << "\n" << "Score: " << e.finalScore <<= "\n";
        }
    }
}
