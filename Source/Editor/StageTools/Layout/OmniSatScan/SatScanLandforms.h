#pragma once
#include "Scene/Generation/Stages/Layout/Data/Terrain/Landform.h"

class GDALDataset;

enum class EHammondLandforms
{
    LowMountains,
    HighMountains,
    OpenLowMountains,
    OpenHighMountains,

    LowHills,
    OpenLowHills,
    HighHills,
    OpenHighHills,

    TablelandsWithHighRelief,
    TablelandsWithLowRelief,
    TablelandsWithOpenHighRelief,
    TablelandsWithOpenLowRelief,

    IrregularPlainsWithHills,
    IrregularPlains,

    Plains,

    Undefined
};

// Gentle Slope (Percent of Neighborhood not Gentle Slope)
enum class EGentleSlopeClass
{
    GS400 = 400, // 0 - 30%
    GS300 = 300, // 31 - 60%
    GS200 = 200, // 61 - 80%
    GS100 = 100,  // 81 - 100%
    Undefined = 900
};

// Elevation change within a neighborhood analysis window (NAW)
enum class EReliefClass
{
    RC10 = 10, // 0 - 30m
    RC20 = 20, // 31 - 90m
    RC30 = 30, // 91 - 150m
    RC40 = 40, // 151 - 300m
    RC50 = 50, // 301 - 900m
    RC60 = 60,  // over 9000
    Undefined = 90
};

// Neighborhood terrain character
enum class EProfileClass
{
    PC0 = 0, // Rough terrain - Under 50% of upland or lowland is gentle slope
    PC1 = 1, // Gentle Lowland - Over 50% of lowland is gentle slope
    PC2 = 2, // Gentle Upland - Many precipices between lowland and upland
    Undefined = 9
};

struct RidgelineDomain
{
    std::vector<int> mountainIndices;
    EHammondLandforms lf;
    float areaPercentage;

    // Domain data
    mutable qint64 domainId = -1;
    mutable int assignedSquareCount;
    // Used for domain creation, cleared after
    mutable QSet<GPoint> squares;
};
class SatScanLandforms
{
public:
    SatScanLandforms(float demHeightFactor);

    EHammondLandforms computeLandform(std::vector<int> rasterIndices);

private:
    // GDAL
    EReliefClass getReliefClass(const std::vector<int>& AreaIndices);
    EProfileClass getProfileClass(float upperPercent, float lowerPercent, float extremeSlopesPercent);
    EGentleSlopeClass getGentleSlopeClass(float slopePercent);

    // Landform classification
    // Get landform enum based on the 3 digit number received during landform analysis
    EHammondLandforms getLandformType(int result);
    void fillClassification();

    QMap<EHammondLandforms, QSet<int>> mClassification;

    float fNoData = -9999; // Default no data value for GDAL datasets
    float fHeightMultiplier = 0.0f; // Elevation Change - based on the range set in makePostRequest()
    float fMinHeight = -1.0f;
    float fMaxHeight = -1.0f;

    std::vector<float> vMainRaster;
    std::vector<float> vSlopeRaster;
};

class SatScanDomainGeneration
{
public:
    SatScanDomainGeneration() = default;

    bool createLandformDomains(std::vector<RidgelineDomain>* domainsRequested, const QSet<GPoint>& availableSquares, bool copyFromWorld);

private:
    // If downgrade is allowed this func will change landformSquares by downgrading them (currently the downgrade order is: mountains -> hills -> tablelands -> rugged plains)
    bool assureSatisfiedLandformRestrictions(std::vector<RidgelineDomain>* domainsRequested, bool downgrade = true);

    // Domain generation
    static void generateDomainsFromResults(const std::vector<RidgelineDomain>& domainsRequested);

    // If true this assigns squares from availableSqrs to each requested domain according it its desired percentage
    bool assignSquaresToDomains(const std::vector<RidgelineDomain>& domainsRequested, const QSet<GPoint>& availableSqrs);
    GPoint getRandomPointFromArea(const QSet<GPoint>& area);
    // Returns an uninterrupted area of squares equal to domainSquares, starting from rootSquare. If this returns empty it restarts the whole generation
    std::optional<QSet<GPoint>> assignSquaresForDomain(const GPoint& rootSquare, int domainSquares, QSet<GPoint>* availableSquares);
    // For given borderSquares returns all newly generated points, and all still eligible to expand old points
    static std::optional<QSet<GPoint>> expandAreaBorder(const QSet<GPoint>& borderSquares, QSet<GPoint>* availableSquares, int* domainSquaresLeft );

    std::vector<QSet<GPoint>> getRemainingContinuousAreas(const QSet<GPoint>& allRemainingSquares);
    std::optional<std::unordered_map<int, int>> checkIfDomainsFitInArea(const std::vector<std::pair<int, int>>& remainingDomains, const std::vector<QSet<GPoint>>& unclaimedAreas);
    void getAllPossibleCombinations(std::vector<int>* combinationIndices, std::vector<std::vector<int>>* allCombinations, const std::vector<int>& claimedIndices, int startIdx, int endIdx, int currentIdx, int combinationSize);
    bool checkNextArea(std::unordered_map<int, int>* newDomainAssignements, const std::vector<int>& indicesClaimed, const std::vector<std::pair<int, int>>& remainingDomains, int areasFilled, const std::vector<int>& areasToFill);

    void mergeDomains(const std::vector<RidgelineDomain>& domainsRequested, const std::vector<int> domainsToMerge);

    friend class SatScanAnalysis;
};