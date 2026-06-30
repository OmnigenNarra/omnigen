#pragma once
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/Layout/Data/Terrain/DomainData_Terrain.h"
#include "Utils/ITree.h"
#include "Utils/SquigCurve.h"

namespace Generation
{
    struct SquigNode;
}

struct SubRidge
{
    SubRidge() = default;
    SubRidge(int maxLvl, SubRidge* prnt) : maxTreeLevel(maxLvl), parent(prnt) {};

    QSet<GPoint> visitedSquares;
    std::vector<GVector2D> squarePath;
    std::vector<SubRidge> subRidges;
    SubRidge* parent;
    int maxTreeLevel;
    std::optional<ETableLand> tablelandType;
};

struct RidgeRoot : SubRidge
{
    RidgeRoot() = default;
    RidgeRoot(int maxLvl, SubRidge* parent) : SubRidge(maxLvl, parent){};

    std::vector<QSharedPointer<Generation::SquigSquare>> area;
    std::vector<int> perimeter;
};

struct RidgeSegmentParams
{
    int subridgeId;
    int subridgeParentId;
    int parentBranchTier;
    int branchTier;
    int createSubridgePerIterations;
    int createSubridgePerIterationsSub;
    int createSubridgeOnIteration;
    int iteration; 
    int mainRidgeSteps; // The number of steps a main ridge must have to be valid
    int maxIterations; // Limits the maximum distance a continuous line can have, independently from max tier count
    int startingStep; // Ridge root, is the same as the parent step number when the branch appeared
    int steps; // Current step
    int maxSteps; // Limits a single ridge length
    int maxSquares; // Limits the maximum overall size a single ridgeline tree can have
    GVector2D pos; // Current step position
    GVector2D parentDir; // Parent ridge direction at the moment of branch creation
    GVector2D baseDir; // Ridge direction at starting step
    GVector2D dir; // Current step direction
    float spreadFactor;
    float moveLength;
    float otherSegmentDistFactor = 1.0f;
    std::optional<ETableLand> tablelandType;

    const std::vector<std::pair<int, std::vector<GVector2D>>>& otherSegments;

    static const inline float minDistanceMultiplierFromHigherRidge = 900.0f;
    static const inline float minDistanceFromGrid = 1000.0f;
    static const inline float minDistanceFromRidges = 1500.0f;
    static const inline float minDistanceFromOtherSegments = 10000.0f;
};

struct RidgeGridData
{
    QSet<GPoint> points;
    std::vector<Segment2D> perimeter;
};

struct RidgePeakData
{
    int tier = -1;
    qint64 ridgeId = -1;
    qint64 rootRidgeId = -1;
    QVector3D peakPoint = {};
    float nodeHeight = -1;
};

// Lines representing main mountain ridges.
class DRidgeMarker : public DLineMarker, public ITree<DRidgeMarker>
{
public:
    DRidgeMarker() = default;
    DRidgeMarker(const std::vector<QVector3D>& inControlPoints, const QSharedPointer<DRidgeMarker>& inParent = nullptr, const QVector4D& inColor = defaultColor);

    virtual void draw() override;

    void joinRidgeAsSubridge(const QSharedPointer<DRidgeMarker>& ridge);
    const auto& getParent() const { return parent; }
    const auto& getChildren() const { return children; }

    const auto& getName() const { return name; }
    const auto& getSourcePointIdx() const { return sourcePointIdx; }
    const auto& getSegmentWidth() const { return segmentWidth; }
    const auto& getHeights() const { return ridgelineHeight; };
    const auto& getTablelandType() const { return tablelandType; };
    const auto& getLeftSlopeFactor() const { return slopeVariation.first; };
    const auto& getRightSlopeFactor() const { return slopeVariation.second; };
    const auto& getSquares() const { return squares; };

    void setParent(const QSharedPointer<DRidgeMarker>& newParent);
    void addChild(const QSharedPointer<DRidgeMarker>& childToAdd);
    void setName(const QString& newName);
    void setSourcePointIdx(int newSourcePointidx);
    void setSegmentWidth(float newSegmentWidth);
    void setHeights(const std::vector<float>& newHeights);
    void setTablelandType(ETableLand newTablelandType);
    void setLeftSlopeFactor(float newLeftSlopeFactor);
    void setRightSlopeFactor(float newRightSlopeFactor);
    void setSquares(const QSet<GPoint>& newSquares);
    void setMarkerColor(const QVector4D& newColor);

    void moveRidgePoints(const std::vector<QVector3D>& newVerts, int vertsAdded = 0);

    std::vector<RidgePeakData> getPeakData();
    int findTier();
    QSharedPointer<DRidgeMarker> findRootParent();
    static ETableLand getRandomTablelandType(ETableLand domainType);

    void showAs3D();
    void showAs2D();

    // Rendering
    IMPLEMENT_SHOULD_DRAW();

    static bool generateAll();

    bool operator==(const DRidgeMarker& other) const
    {
        return OmnigenDrawable::operator==(other);
    }

private:
    using subRidgeParam_t = std::vector<std::tuple<RidgeSegmentParams, const RidgeGridData&, SubRidge*, int>>;

    static void branchPass(RidgeSegmentParams* ridgeParams, const RidgeGridData& ridgeGrid, SubRidge* ridgeRoot, RidgeSegmentParams* rootRidgeParams, std::map<int, subRidgeParam_t>* subRidgeParams, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares);
    static void ridgeMainPass(RidgeSegmentParams* ridgeParams, const RidgeGridData& ridgeGrid, SubRidge* ridgeRoot, std::map<int, subRidgeParam_t>* subRidgeParams, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares);
    static bool insertSegment(RidgeSegmentParams* ridgeParams, const RidgeGridData& ridgeGrid, SubRidge* ridgeRoot, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares, bool straight = false);
    static std::optional<Segment2D> checkNewSegment(const Segment2D& newSegment, const RidgeSegmentParams& ridgeParams, const SubRidge& ridgeRoot, const std::unordered_map<int, std::vector<Segment2D>>& segments);
    static RidgeSegmentParams createBranchSegment(const RidgeSegmentParams& ridgeParams, const GVector2D& fixedAngle = {});
    static void processSubRidges(RidgeSegmentParams* rootRidgeParams, std::map<int, subRidgeParam_t>* subRidges, int level, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares);

    static void applyMarginToSet(QSet<GPoint>* set, const QSharedPointer<DDomain>& domain, int marginSize);
    static GVector2D calcCenter(std::vector<QSharedPointer<Generation::SquigSquare>>* area);

    // <iterations, subridge per iteration for main ridge, subridge per iteration for sub ridge, spread factor>
    static std::tuple<int, int, int, float> ridgeParamsToValues(const RidgeGenParams& params);

    static void createRidgeMarkerTree(SubRidge* ridge, const QSharedPointer<DRidgeMarker>& inParent = nullptr);
    static void subtractSquareMargin(QSet<GPoint>* squares, const QSet<GPoint>& marginSource);
    static float randomSign(bool allowZero = false);

    void makeName();

    float segmentWidth;
    int sourcePointIdx = -1;
    QString name;

    std::pair<float /*left*/, float /*right*/> slopeVariation;
    std::vector<float> ridgelineHeight;
    std::optional<ETableLand> tablelandType;
    QSet<GPoint> squares;

    static const inline QVector4D defaultColor = QVector4D(1, 0, 0, 1);

    FRIEND_OMNIBIN(DRidgeMarker);
};

DEFINE_TREE_SAVELOAD(DRidgeMarker)

inline void omniSave(const DRidgeMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DLineMarker&>(object);
    omniBin << static_cast<const ITree<DRidgeMarker>&>(object);
    omniBin << object.segmentWidth;
    omniBin << object.sourcePointIdx;
    omniBin << object.name;
    omniBin << object.ridgelineHeight;
    omniBin << object.slopeVariation;
    omniBin << object.tablelandType;
    omniBin << object.squares;
}

inline void omniLoad(DRidgeMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DLineMarker&>(object);
    omniBin >> static_cast<ITree<DRidgeMarker>&>(object);
    omniBin >> object.segmentWidth;
    omniBin >> object.sourcePointIdx;
    omniBin >> object.name;
    omniBin >> object.ridgelineHeight;
    omniBin >> object.slopeVariation;
    omniBin >> object.tablelandType;
    omniBin >> const_cast<QSet<GPoint>&>(object.squares);
}
