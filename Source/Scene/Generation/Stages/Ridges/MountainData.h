#pragma once

#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"

enum class EValleyProfile
{
    Sharp = 1,
    Normal = 2,
    Gentle = 3,
    Undefined
};

enum class ESlopeCurve
{
    Convex = 1,
    Normal = 2,
    Concave = 3,
    Undefined
};

struct RidgePoint
{
    int parent = -1;
    std::vector<int> children;
    float height;
    GVector2D pos;
};

struct RidgeData
{
    // ridge start point (also subridge source point), many ridges can have the same root point
    int rootPtIdx = -1;
    // ridges second point, first of its unique branch, acts as an identifier
    int branchPtIdx = -1;
    int endPtIdx = -1;
    float averageHeight = -1.0f;
    float length = 0.0f;
    // 0 - main ridge, 1 - subridge 1st class, 2, subridge 2nd class etc
    mutable int ridgeClass = -1;

    std::vector<int> ridgePointsIndices;
    int localPeakIdx = -1;
};

class MountainData
{
public:
    MountainData(const RidgePoint& peakPoint);

    const auto& getMountainPts() const { return vMountainPts; };
    const auto& getPeakIndices() const { return vPeakPts; };
    const auto& getRidgeline() const { return vRidgeline; };
    const auto& getSquaresCountOfRidgeline() const { return ridgelineGridSquaresCount; };
    const auto& getRidgelineValleyProfile() const { return ridgelineProfile; };
    const auto& getInnerValleyProfile() const { return innerProfile; };
    const auto& getOuterValleyProfile() const { return outerProfile; };
    const auto& getSlopeProfile() const { return slopeCurve; };

    void setSquareCountOfRidgeline(int squaresCount) { ridgelineGridSquaresCount = squaresCount; };
    void setRidgelineValleyProfile(EValleyProfile profile) { ridgelineProfile = profile; };
    void setInnerValleyProfile(EValleyProfile profile) { innerProfile = profile; };
    void setOuterValleyProfile(EValleyProfile profile) { outerProfile = profile; };
    void setSlopeProfile(ESlopeCurve profile) { slopeCurve = profile; };

    void computeRidgeline();

    void addRidgePoint(const RidgePoint& pt) { vMountainPts.emplace_back(pt); };
    void addChildToPoint(int childIdx, int parentIdx) { vMountainPts[parentIdx].children.emplace_back(childIdx); };
    void addPeakIdx(int peakIdx) { vPeakPts.emplace(peakIdx); };

    // Mostly for debug purposes (most likely will significantly change when final Copy From World will be set)
    void drawRidgesInDomain(qint64 domainGuid, int nCols, int nRows, const QSet<GPoint>& availableSquares);

private:
    RidgeData computeShortRidge(int branchRootIdx);
    // Returns short ridges - lines from root peak till the first ridge branch
    std::vector<RidgeData> computeShortRidgeline();

    // For the given point if there are ridges and if the average height is similar to that of its parent, 
    // merge (with its parent ridge) the one branched at the end, or longest if none are / more than one is branched
    // The remaining ridges are added to vRidgeline. If this returns empty it means there are no more ridges for the given root
    std::optional<std::vector<int>> findEndPoints(int rootPointIdx, const std::vector<RidgeData>& ridgeline);

    void mergeRidgeWithParent(const RidgeData& ridge);
    std::optional<std::vector<std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>>> makeRidgeMarker(const std::vector<int>& branchPts, QSharedPointer<DRidgeMarker> parentMarker, const std::tuple<float, float, float, float>& scaleData);

    // Main ridgelines (might be two, both with same root, keep in mind while drawing/computing) and all subridges
    std::vector<RidgeData> vRidgeline;
    std::vector<RidgePoint> vMountainPts;
    // Element 0 of mountainPts is always the root peak
    std::unordered_set<int> vPeakPts = { 0 };
    int ridgelineGridSquaresCount;

    float ridgelineHeightFactor = 8.0f;

    EValleyProfile ridgelineProfile = EValleyProfile::Undefined; // The profile of passes in main ridgeline
    EValleyProfile innerProfile = EValleyProfile::Undefined; // The profile of valleys between ridgelines of same mountain
    EValleyProfile outerProfile = EValleyProfile::Undefined; // The profile of valleys between ridgelines of neighboring mountains

    ESlopeCurve slopeCurve = ESlopeCurve::Undefined;
};