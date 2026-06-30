#pragma once
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "Utils/Polygon.h"
#include "Utils/ITree.h"
#include "Editor/StageTools/ModAssignment/ModToolsBase.h"

namespace tbb
{
    class spin_mutex;
}

class DDDMarkerBase;
class DShorelineMarker;
struct BayNode;
class DDomain;
class DRiverMarker;
class DTrueRiverBoundMarker;

struct InfluentInfo
{
    QWeakPointer<DRiverMarker> river;
    float angle;
    int parentSegmentIdx;
    float parentSegmentPart;
    qint64 riverGuid;

    bool isRight() const { return angle < 180.0f; };

    explicit operator bool() const
    {
        return river;
    }

    bool operator<(const InfluentInfo& other) const;
};

inline void omniSave(const InfluentInfo& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.angle;
    omniBin << object.parentSegmentIdx;
    omniBin << object.parentSegmentPart;
    omniBin << object.riverGuid;
}

inline void omniLoad(InfluentInfo& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.angle;
    omniBin >> object.parentSegmentIdx;
    omniBin >> object.parentSegmentPart;
    omniBin >> object.riverGuid;
}

struct RiverAxisPoint
{
    QVector3D center;
    QVector3D leftFloodBound;
    QVector3D rightFloodBound;
};

struct RiverData
{
    std::vector<QVector3D> axis;
    std::vector<QVector3D> leftFloodBound;
    std::vector<QVector3D> rightFloodBound;
    bool fallsIntoSea = false;

    QSet<int> affectedClustersKeyCells;
};

class DRiverMarker : public DLineMarker, public ITree<DRiverMarker>
{
public:
    DRiverMarker() = default;
    DRiverMarker(const RiverData& riverData, QWeakPointer<DRiverMarker> inParent);

    const auto& getInfluents() const { return influents; }
    const auto& getParentRiver() const { return parent; }
    const auto& getRiverBounds() const { return riverBounds; }
    const auto& getBoundPolygon() const { return boundPolygon; }
    const auto& getRiverLength() const { return riverLength; }
    const auto& getArea() const { return area; }
    bool fallsIntoSea() const { return bFallsIntoSea; }
    void addInfluent(const InfluentInfo& info);

    // Rendering
    IMPLEMENT_SHOULD_DRAW();

    static QSharedPointer<DRiverMarker> generateOne(const IHSrcInfo& source, const std::vector<QVector3D>& riverPoints = {});
    static bool generateAll();

    static inline const float step = 100;

private:
    using PenPairsMap = QMap<QSharedPointer<DDomain> /* terrain domain */,
        QMap<QSharedPointer<DShorelineMarker> /* bay's owner */,
        std::vector<QPair<QSharedPointer<BayNode>, QSharedPointer<BayNode>>> /*neighboring peninsulas*/>>;

    struct IHSegmentLookupData
    {
        float w;
        GVector2D closestPoint;
    };

    float riverLength = 0;
    std::vector<InfluentInfo> influents;
    std::array<std::vector<QVector3D>, 2> riverBounds;
    Polygon2D boundPolygon;
    bool bFallsIntoSea = false;
    QSet<int> area;

    Polygon2D createBoundPolygon();

    static void createRiverNetworks(std::vector<RiverData>&& rivers);
    static std::array<QVector3D, 2> findFloodBounds(RiverData* riverData, int pointIdx, const GVector2D& dir);
    static std::tuple<std::optional<QVector3D>, bool> extendRiver(const GVector2D& dir, const QVector3D& p, QVector3D* prev_p, float dirForce);
    static std::tuple<std::vector<QVector3D>, bool> generateTerrainCompatibleRiver(const IHSrcInfo & source, const std::vector<QSharedPointer<DShorelineMarker>>&shores);
    static std::vector<QPair<QSharedPointer<DShorelineMarker>, int>> getShoreCollisionInfo(const std::vector<QSharedPointer<DShorelineMarker>>& shores, const QPair<QVector3D, QVector3D>& riverSegment);
    static InfluentInfo tryMergeIntoOtherRiver(const std::vector<QSharedPointer<DRiverMarker>>& existingRivers, RiverData* river);
    static void mergeInfluentBounds(RiverData* river, const InfluentInfo& mergeInfo);
    
    static void smoothRiver(std::vector<QVector3D>* path, std::vector<QPair<GVector2D, GVector2D>>* calibrationBounds);
    static GVector2D findRiverDir(Isohypse* nextIh, const QVector3D& p3, bool* escapeLock);
    static GVector2D computeEscapeDir(const std::set<Isohypse*>& ihs, const std::map<IHSrcInfoMulti, IHSegmentLookupData>& influencers, const QVector3D& p3);
    static GVector2D findDirToDescendant(const IHSrcInfo& ihs, auto&& p3, bool debug = false);
    static GVector2D findShortcutToDescendant(const IHSrcInfo& ihs, int dir, int target);
    static void addFloodBounds(RiverData* river);
    static void alignRiverEnd(std::vector<QVector3D>* path);
    static bool endMarkerAtShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& shores, std::vector<QVector3D>* riverPts);
    static std::tuple<std::vector<QVector3D>, std::vector<GVector2D>, std::vector<GVector2D>> splitAxisPoints(const std::vector<RiverAxisPoint>& pts);

    static std::optional<QVector3D> findRiverPoint(const GVector2D& p, QVector3D* prev_p);

    static std::vector<QVector3D> getAxisCenters(const std::vector<RiverAxisPoint>& axisPoints);

    friend struct Design::ModTools<Generation::ETerrainMod::River>;

    FRIEND_OMNIBIN(DRiverMarker);
};

DEFINE_TREE_SAVELOAD(DRiverMarker);

inline void omniSave(const DRiverMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DLineMarker&>(object);
    omniBin << static_cast<const ITree<DRiverMarker>&>(object);
    omniBin << object.riverLength;
    omniBin << object.influents;
    omniBin << object.riverBounds;
    omniBin << object.boundPolygon;
    omniBin << object.bFallsIntoSea;
    omniBin << object.area;
}

inline void omniLoad(DRiverMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DLineMarker&>(object);
    omniBin >> static_cast<ITree<DRiverMarker>&>(object);
    omniBin >> object.riverLength;
    omniBin >> object.influents;
    omniBin >> object.riverBounds;
    omniBin >> object.boundPolygon;
    omniBin >> object.bFallsIntoSea;
    omniBin >> object.area;
}

class DTrueRiverBoundMarker : public DLineMarker
{
public:
    using DLineMarker::DLineMarker;

    // Rendering
    IMPLEMENT_SHOULD_DRAW();
};
