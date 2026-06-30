#pragma once
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Utils/CoreUtils.h"
#include "Scene/Generation/Stages/Layout/Data/Terrain/Landform.h"
#include "IHSrcInfo.h"

class DIsohypseBound : public DLineMarker
{
public:
    DIsohypseBound(const std::vector<QVector3D>& inControlPoints, const QVector4D color = { 1,1,1,1 });
    bool isCrossing(QSharedPointer<Isohypse> ih) const;

protected:
    DIsohypseBound() = default;
    FRIEND_OMNIBIN(DIsohypseBound);
};

enum class EIHGenerationStage
{
    Init,
    BoundsCrossed,
    Done
};

enum class EIHAffectType
{
    Domain,
    Shoreline,
    Merge
};

struct IHProtoData
{
    bool boundsReached = false;
    bool slopeFactorForPeakApplied = false;
    int originIdx;
    int lowestRidgeTier;
    float height;
    float mergeDistanceMult;
    float heightAtHalfOfDistanceToBase;
    float distanceToBase;
    float groupingFactor = 1.0f;
    qint64 usedDomainId;

    QSharedPointer<Isohypse> ptr;

    std::vector<QVector3D> pts;
    std::vector<IHSrcInfo> sources;
    std::vector<float> increments;
    std::vector<std::pair<float /*value*/, bool /*forward trend*/>> modifiedBy;
    std::unordered_map<qint64, std::pair<QSharedPointer<DIsohypseBound>, EIHGenerationStage>> bounds;
    mutable std::unordered_map<qint64, EIHGenerationStage> boundsGuid; // for serialization
    std::vector<float> heightDeltas;
    std::vector<int> mergeIhlevels;
    std::unordered_map<EIHAffectType, std::unordered_set<qint64>> affectedBy;

    std::set<qint64> mergedDomains;
    std::set<Isohypse*> parentIhs;
    std::unordered_set<qint64> ridgeIds;
    std::unordered_map<int /*point idx*/, QVector3D /*ridgeline sourcepoint*/> ridgelineSources;
    std::optional<float> mergeThreshold;

    // Tablelands data
    std::optional<int> currentDropLvl;
    std::optional<int> desiredDropLvl;
    std::optional<ETableLand> tablelandType;

    bool regenerated = false;

    // Does not carry over
    mutable IHProtoData* swallowedBy = nullptr;

    // Merging data
    mutable GVector2D center;
    mutable float radius;
    mutable float maxIncrement;

    bool operator<=>(const IHProtoData&) const = default;
    void computeMergingData() const;
};

inline void omniSave(const IHProtoData& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.boundsReached;
    omniBin << object.slopeFactorForPeakApplied;
    omniBin << object.originIdx;
    omniBin << object.lowestRidgeTier;
    omniBin << object.height;
    omniBin << object.mergeDistanceMult;
    omniBin << object.heightAtHalfOfDistanceToBase;
    omniBin << object.distanceToBase;
    omniBin << object.groupingFactor;
    omniBin << object.usedDomainId;

    omniBin << object.sources;
    omniBin << object.modifiedBy;
    for (auto&& [guid, bound] : object.bounds)
        object.boundsGuid[guid] = bound.second;
    omniBin << object.boundsGuid;
    omniBin << object.mergeIhlevels;
    omniBin << object.affectedBy;

    omniBin << object.mergedDomains;
    omniBin << object.ridgeIds;
    omniBin << object.ridgelineSources;
    omniBin << object.mergeThreshold;

    omniBin << object.currentDropLvl;
    omniBin << object.desiredDropLvl;
    omniBin << object.tablelandType;
}

inline void omniLoad(IHProtoData& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.boundsReached;
    omniBin >> object.slopeFactorForPeakApplied;
    omniBin >> object.originIdx;
    omniBin >> object.lowestRidgeTier;
    omniBin >> object.height;
    omniBin >> object.mergeDistanceMult;
    omniBin >> object.heightAtHalfOfDistanceToBase;
    omniBin >> object.distanceToBase;
    omniBin >> object.groupingFactor;
    omniBin >> object.usedDomainId;

    omniBin >> object.sources;
    omniBin >> object.modifiedBy;
    omniBin >> object.boundsGuid;
    omniBin >> object.mergeIhlevels;
    omniBin >> object.affectedBy;

    omniBin >> object.mergedDomains;
    omniBin >> object.ridgeIds;
    omniBin >> object.ridgelineSources;
    omniBin >> object.mergeThreshold;

    omniBin >> object.currentDropLvl;
    omniBin >> object.desiredDropLvl;
    omniBin >> object.tablelandType;
}