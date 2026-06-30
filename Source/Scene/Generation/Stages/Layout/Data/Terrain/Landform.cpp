#include "stdafx.h"
#include "Landform.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"
#include <Source/Scene/Generation/OmnigenGenerationData.h>

std::unordered_map<ELandformVariations, LandformParams> PLandformTypes = {};
std::unordered_map<ELandformVariations, std::unordered_map<ETableLand, TablelandParams>> PTablelandTypes = {};

namespace Landform
{
    float computeMaxRidgeHeight(ELandform tt, int domainSize)
    {
        switch (tt)
        {
            // pow with base of 1 = 1, log2 of 1 = 0, thus the 1 + for domainsSize = 1
        case ELandform::Plains:
        case ELandform::RuggedPlains:
            return 20 + std::pow(std::log2(domainSize), 2);
        case ELandform::Tablelands:
        case ELandform::Hills:
            return 100 + 2 * std::log2(std::pow(domainSize, 2));
        case ELandform::Mountains:
            return 100 + std::pow(std::log2(std::pow(domainSize, 2)), 2);
        }

        Q_ASSERT(false);
        return -1.0f;
    }

    std::tuple<float /*distance*/, int /*last local lvl*/> estimateNextIsohypseDistance(const IHProtoData& ihData)
    {
        int estimatedDistance = 200;
        int lastLocalLevel = 0;

        int ihLevels = ihData.heightDeltas.size() + 1;

        for (int lvl = 0; lvl < ihLevels; lvl++, lastLocalLevel++)
        {
            if (std::any_of(ihData.mergeIhlevels.begin(), ihData.mergeIhlevels.end(), [&](auto& mLvl) { return mLvl == lvl; }))
                lastLocalLevel = 1;

            estimatedDistance += ContourLines::baseIncrement * lastLocalLevel;
        }

        return { estimatedDistance, lastLocalLevel };
    }

    BezierCurve2D calculateCurveDefiningIsohypseDrop(const IHProtoData& ihData)
    {
        auto ridgeHeight = std::accumulate(ihData.heightDeltas.begin(), ihData.heightDeltas.end(), ihData.height);

        return BezierCurve2D({ {0 , 0 }, {ihData.heightAtHalfOfDistanceToBase, 0.5f }, {ridgeHeight, 1.0f} });
    }

    std::tuple<float /*height diff*/, float /*merge distance mult*/> computeNextIHParams(const IHProtoData& ihData)
    {
        auto estimatedDistance = std::get<float>(estimateNextIsohypseDistance(ihData));
        auto curve = calculateCurveDefiningIsohypseDrop(ihData);

        auto t = std::max(0.0f , 1 - estimatedDistance / ihData.distanceToBase);

        // For now use t^3 to have better defined slides
        return { std::max(ihData.height - curve.evaluate(t * t * t).x, 1.0f), 1.0f };
    }

    float computeHeightStepsProjection(const IHProtoData& ihData, float targetHeight)
    {
        auto [estimatedDistance, lastLevel] = estimateNextIsohypseDistance(ihData);
        auto curve = calculateCurveDefiningIsohypseDrop(ihData);

        int steps = 1;

        while (true)
        {
            auto t = std::max(0.0f, 1 - estimatedDistance / ihData.distanceToBase);
            if (curve.evaluate(t * t * t).x <= targetHeight)
                break;

            steps++;
            estimatedDistance += ContourLines::baseIncrement * (lastLevel + steps);
        }

        return steps;
    }

    float flattenHeight(ELandform tt, float currentHeight)
    {
        return currentHeight * 0.5f;
    }

    std::tuple<float /*distance to base*/, float /*height at half of distance*/> calculateCurveDefiningIsohypseParameters(float height, ELandformVariations landform)
    {
        auto ihDropAngle = PLandformTypes[landform].IsohypseDropAngle.getRandomValue();
        auto ihCurveRatio = PLandformTypes[landform].IsohypseCurveRatio.getRandomValue();

        return { height * tanf(qDegreesToRadians(90 - ihDropAngle)), height * ihCurveRatio };
    }

    void mergeIHParams(const IHProtoData& ih1, const IHProtoData& ih2, IHProtoData* result)
    {

        auto getPrefferedIH = [](const IHProtoData& ih1, const IHProtoData& ih2) -> const IHProtoData&
        {
            if (ih1.usedDomainId == ih2.usedDomainId)
                return (ih1.height < ih2.height) ? ih1 : ih2;
            else
            {
                auto maxH1 = (*Generation::Data::get()->findDomainByGuid(ih1.usedDomainId))->getData<EDomainType::Terrain>()->maxHeight;
                auto maxH2 = (*Generation::Data::get()->findDomainByGuid(ih2.usedDomainId))->getData<EDomainType::Terrain>()->maxHeight;

                return (maxH1 < maxH2) ? ih1 : ih2;
            }
        };
        // Height picking
        //const IHProtoData& preferred = (ih1.height < ih2.height) ? ih1 : ih2;
        auto&& preferred = getPrefferedIH(ih1, ih2);

        result->height = preferred.height;
        result->heightDeltas = preferred.heightDeltas;
        result->mergeDistanceMult = 1.0f;

        result->distanceToBase = preferred.distanceToBase;
        result->heightAtHalfOfDistanceToBase = preferred.heightAtHalfOfDistanceToBase;

        result->mergeIhlevels = preferred.mergeIhlevels;
        result->mergeIhlevels << result->heightDeltas.size();

        result->usedDomainId = preferred.usedDomainId;
    }

    float generateRidgeHeight(float minHeight, float maxHeight)
    {
        static std::uniform_real_distribution<float> heightDistribution(0.0f, 1.0f);
        return minHeight + (maxHeight - minHeight) * heightDistribution(Generation::gRandomEngine);
    }
}

float ParameterData::getRandomValue()
{
    float offset = (offsetFinalValue - range.first) / (range.second - range.first);
    if (range.first >= range.second)
        return range.first;

    hybrid_int_distribution<int> rnd(range.first * 100, range.second * 100, offset, flatness);
    return (float(rnd(Generation::gRandomEngine)) / 100.0f);
}
