#pragma once
#include "UrbanSuggestion.h"

namespace Generation
{
    constexpr int maxPotentialSuggestions = 5000;

    class UrbanPlanner
    {
        enum class ESuggestionType
        {
            Elevation,
            WaterProximity,
            MountainProximity,
            ClusterSize,
            SphereOfInflunce
        };

        struct SuggestionData
        {
            int cellId = -1;
            QVector3D seed;
            QSet<int> maxArea;
            QSet<int> maxFlatlandArea;
            float maxFlatlandSize = 0.f;
            EUrbanSize maxAreaSize = EUrbanSize::Town;
            bool shouldDiscard = false;

            QMap<ESuggestionType, float> weightPerType;
            QMap<ESuggestionType, float> scorePerType;

            float finalScore = 0.f;

            SuggestionData(const int id, const QVector3D& inSeed)
                : cellId(id), seed(inSeed) {}

            void calculateArea();
        };

    public:
        UrbanPlanner() = default;
        void calculateSuggestions();
        std::vector<SuggestionData> getSuggestions(const int num, const bool printDebugData = false);

    private:
        void gatherFlatlandSeeds();

        //TODO: Could be run on TBB with a mutex on the map
        void rateSphereOfInflunce();
        void rateElevation();
        void rateWaterProximity();
        void rateMountainProximity();
        void rateClusterSize();

        void validateDistanceFromWorldBounds();

        void applyWeightsToRates();

        // Utils
        float getScoreFromId(const int id) const;
        void setScoreFromId(const int id, ESuggestionType type, const float newScore);

        void printScoresToLog(const std::vector<SuggestionData>& data);

        std::vector<SuggestionData> suggestionSeeds;

        bool hasCalculated = false;
    };

}

