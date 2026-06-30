#pragma once

enum class ELandform
{
    Plains,
    RuggedPlains,
    Tablelands,
    Hills,
    Mountains
};

enum class ETableLand
{
    Pinnacle,
    Butte,
    Mesa,
    Plateau,
};

// Variations must start with the name of the landform from ETableland
enum class ELandformVariations
{
    MountainsRagged,
    MountainsRaggedScatteredGrouped,
    MountainsRaggedJaggedGrouped,
    MountainsRaggedGlacial,
    MountainsRaggedModeratelyExpanded,
    MountainsRaggedFold,
    MountainsRaggedJaggedFold,
    MountainsRaggedFoldWithSharpValleys,
    MountainsRaggedLong,

    HillsIrregularSmall,
    HillsIrregularModeratelyExpanded,
    HillsRoundedModeratelyExpanded,
    HillsRegularSmall,
    HillsScatteringSmall,

    TablelandsBasic,
    TablelandsElongated,
    TablelandsCanyon1,
    TablelandsCanyon2,
    TablelandsPreCanyon1,
    TablelandsPreCanyon2,
    TablelandsLarge,

    PlainsBasic,
    PlainsDense,
    PlainsDenseGrouped,

    RuggedPlainsBasic,
    RuggedPlainsSharp,
    RuggedPlainsGrouped,
    RuggedPlainsScattered
};

struct ParameterData
{
    std::pair<float, float> range;
    float offsetFinalValue;
    float flatness;

    float getRandomValue();
};

struct TablelandParams
{
    ParameterData flatRadius;
    ParameterData dropRatio;
    float ridgeDensityRatio;
    float desiredFormSize;
    int desiredPrecipiceSteps;
    ParameterData randomizationStart;
};

inline quint32 qHash(ELandform key, uint seed)
{
    return qHash(static_cast<uint>(key), seed);
}

struct LandformParams
{
    int minSquares;                                    // 1 = 1 grid square. Sets the minimum square count needed for landform to be valid
    int ridgeMargin;                                   // 1 = 1 grid square. Sets the domain border at which no ridge generation should occur
    ParameterData ridgeDensityPerSquare;     // 1.0 = 100%. Determines the area that should have ridgelines
    ParameterData ridgeAverageSize;          // 1.0 = 100%. Determines what portion of available space should a single ridgeline get
    ParameterData mainRidgeSize;                 // The size range of the main ridgeline
    ParameterData subRidgeSize;
    ParameterData mainPeakCount;                 // Number of peaks that should be present on a main ridge
    ParameterData peakDistance;              // The minimum distance between different peaks of same ridgeline
    ParameterData ridgeMaxTreeLevel;                             // 1 = one additional level beyond root
    ParameterData IsohypseDropAngle;         // angle of isohypses drop from peak
    ParameterData IsohypseCurveRatio;        // curvation of isohypses drop, 0.5 = linear drop
    ParameterData ridgelineSlopeAngle;        // angle of ridgeline drop from peak or source point (must be equal or lower than isohypseDropAngle)
    ParameterData slopeAngleSameRidgesLevel0;
    ParameterData slopeAngleSameRidges;
    ParameterData slopeAngleDifferentRidges;
    ParameterData slopeFactorRange;
    float segmentLength;
    std::pair<float, float> scaling;                    // Scaling factor, scaling threshold (minimum area required to start scaling)
    int randomizedIncrement;
};

extern std::unordered_map<ELandformVariations, LandformParams> PLandformTypes;
extern std::unordered_map<ELandformVariations, std::unordered_map<ETableLand, TablelandParams>> PTablelandTypes;

struct IHProtoData;

namespace Landform
{
    float computeMaxRidgeHeight(ELandform tt, int domainSize);
    std::tuple<float /*height*/, float /*merge distance mult*/> computeNextIHParams(const IHProtoData& ihData);
    float computeHeightStepsProjection(const IHProtoData& ihData, float targetHeight);
    float flattenHeight(ELandform tt, float currentHeight);
    std::tuple<float /*distance to base*/, float /*height at half of distance*/> calculateCurveDefiningIsohypseParameters(float height, ELandformVariations landform);
    void mergeIHParams(const IHProtoData& ih1, const IHProtoData& ih2, IHProtoData* result);
    float generateRidgeHeight(float minHeight, float maxHeight);
}