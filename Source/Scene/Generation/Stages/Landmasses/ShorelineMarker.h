#pragma once
#include "Scene/Generation/Stages/Landmasses/LandmassMarker.h"
#include "Scene/Generation/Stages/Landmasses/SeamassMarker.h"
#include "Scene/Generation/Stages/Layout/Data/DomainData.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Utils/ITree.h"
#include "Utils/Polygon.h"

class DShorelineMarker;
class DLandmassMarker;
class DSeamassMarker;

struct BayNode : ITree<BayNode>
{
    QPair<int, int> range;

    std::vector<QPair<QSharedPointer<BayNode>, QSharedPointer<BayNode>>> getNeighboringSubnodes() const;
    bool contains(QSharedPointer<BayNode> node) const;
    bool contains(int point) const;
    bool insert(QSharedPointer<BayNode> node);

    auto operator<=>(const BayNode& other) const = default;
    bool operator==(const BayNode& other) const
    {
        return range == other.range;
    }
};

DEFINE_TREE_SAVELOAD(BayNode)

inline void omniSave(const BayNode& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.range;
    omniBin << static_cast<const ITree<BayNode>&>(object);
}

inline void omniLoad(BayNode& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.range;
    omniBin >> static_cast<ITree<BayNode>&>(object);
}

struct Shoreline
{
    QSet<GPoint> area;
    std::vector<QVector3D> path;
};

struct Landmass
{
    QSet<GPoint> insideArea;

    std::vector<Shoreline> shorelines;

    std::vector<Shoreline> innerSeaShorelines;

    // Island is of origin type when it came from pre existing land which neighbor seaside
    bool isOrigin = false;

    // Island is coast when it's land neighbour's end of map
    bool isCoast = false;

    // temporary, do not save:
    int index = -1;

    // returns shoreline id if it's area is within given seaside
    std::optional<int> shorelineWithinSeaside(const QSet<GPoint>& seaside);

    // returns inner sea shoreline id if it's area is within given seaside
    std::optional<int> innerSeaShorelineWithinSeaside(const QSet<GPoint>& seaside);
};

class DShorelineMarker : public DLineMarker, public QEnableSharedFromThis<DShorelineMarker>
{
    struct GPUPoint
    {
        float x;
        float z;
        // type: -1 = left; 0 = straight; 1 = right
        int forwardType;
        int backwardType;
        QVector2D forwardDir;
        QVector2D backwardDir;
        int forwardPair;
        int backwardPair;
    };

public:
    enum class EIslandGrowType
    {
        SmallIsland,
        MediumIsland,
        LargeIsland,
        OriginIslandExtension
    };

    // How many islands should be connected per grow type
    inline static const std::map<EIslandGrowType, int> PIslandGrowSize
    {
        {EIslandGrowType::SmallIsland, 0},
        {EIslandGrowType::MediumIsland, 1},
        {EIslandGrowType::LargeIsland, 3},
        {EIslandGrowType::OriginIslandExtension, 1}
    };

    struct IslandParameters
    {
        // Every value here has a default range [0;1]

        // How much space should be used for islands
        float coverage;

        // chance of generating small islands
        float smallIslandsQuantity;

        // chance of generating medium islands
        float mediumIslandsQuantity;

        // chance of generating large islands
        float largeIslandsQuantity;

        float shorelineComplexity;
    };

    DShorelineMarker(const std::vector<QVector3D>& inControlPoints, bool isCircular);

    virtual void draw() override;
    virtual void showAs3D();
    virtual void showAs2D();
    virtual void computeBoxVerts() override {};
    void makeName();

    const auto& getName() const { return name; }
    const auto& getLandmass() const { return landmass; }
    const auto& getBays() const { return baysRoot; }
    const auto& getPeninsulas() const { return peninsulasRoot; }
    float getSegmentWidth() const { return segmentWidth; }
    const auto& getSquares() const { return squares; }
    const auto& getHeights() const { return shorelineHeights; }
    const auto& getLandmassGuid() const { return landmassGuid; }

    void setName(const QString& newName);
    void setSegmentWidth(float newSegmentWidth);
    void setLandmass(const QSharedPointer<DLandmassMarker>& newLandmass);
    void setBays(const QSharedPointer<BayNode>& newBays);
    void setPeninsulas(const QSharedPointer<BayNode>& newPeninsulas);
    void setSquares(const QSet<GPoint>& newSquares);
    virtual void setPoints(const std::vector<QVector3D>& newVerts, bool isLoop = false) override;
    virtual void setHeights(std::vector<float> newHeights);

    std::vector<QSharedPointer<BayNode>> getPeninsulaPath(int pointIdx) const;

    bool detectBays();

    // Rendering
    IMPLEMENT_SHOULD_DRAW();

    // Returns shorelines per land (squares connected with each other) generated around them on provided seasides
    static std::vector<std::vector<QSharedPointer<DShorelineMarker>>> generateBasicShorelines(const QSet<GPoint>& landSquares, const QSet<GPoint>& seasideSquares);

    // returns basic shorelines from not assigned terrain only squares
    static std::vector<std::vector<QSharedPointer<DShorelineMarker>>> generateInitBasicShorelines(const std::optional<std::vector<QSharedPointer<DLandmassMarker>>>& existingLandmasses = std::nullopt);

    static void generateInit();

    static bool generateAll();

    static QSharedPointer<DLandmassMarker> generateLandmassAtSquare(const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalInsideSquares, const GPoint& startSquare, EShorelineComplexity comlexity, int size);
    static std::vector<QVector3D> generateshorelinePath(const QSet<GPoint>& land, const QSet<GPoint>& seaside, const IslandParameters& params, bool isCoast);

protected:
    DShorelineMarker() = default;
	enum class SquareType
	{
		Water,
		Terrain,
		Seaside,
		Other
	};

    void addNewBay(int a, int b);
    void addNewPeninsula(int a, int b);
    void addNewBayNode(int a, int b, QSharedPointer<BayNode> targetRoot);

    static std::optional<std::pair<GVector2D, GVector2D>> findCoastEndpoints(const QSet<GPoint>& seaside, const QSet<GPoint>& island);

    static SquareType getSquareType(const GPoint& p, int x = 0, int z = 0);

    // Check if a pure terrain or pure water point is lying on the outside of the domain.
    static bool checkAccess(const SquareType& excludedType, const GPoint& p, int x, int z);

    static bool checkOutsideAccess(const SquareType setType, const QSet<GPoint>& set);

    static std::optional<int> findLandmassSurroundingSet(const QSet<GPoint>& set, const QHash<GPoint, std::optional<int>>& landmassIndexAffiliation);

    static std::vector<Landmass> createOriginLandmasses(const std::vector<QSet<GPoint>>& lands, const std::vector<QSharedPointer<DLandmassMarker>>& existingLandmasses);

    static std::vector<Landmass> findSeamassesInLandmass(const Landmass& landmass);

    // Determine the inside of the island - returns (perimeter, illegalPoints, boundingShores).
    static std::tuple<QSet<GPoint>, QSet<GPoint>> findOutsideTypePoints(const QSet<GPoint>& seasideToSplit, SquareType insideType);

    // The connecting function that returns true if the connection was succesfull.

    static std::optional<QSet<GPoint>> findPathback(const QHash<GPoint, int>& pathsWithDepth, const GPoint& finalPoint);

    // Make sure every clump of coeherent seaside1 and seaside2 points has access to the outside of the shore.
    static void detachedPoints(const QSet<GPoint>& island, QSet<GPoint>* seaside, const SquareType insideType, QSet<GPoint>* sourceSeaside, QSet<GPoint>* otherSeaside);

    // Classify the point as null - (1, 0 ,0), seaside1 - (0, 1, 0), seaside2 - (0, 0, 1) or invalid (0, 0, 0).
    static QVector3D classifySeasidePoint(const QSet<GPoint>& island, const QSet<GPoint>& seaside, const QSet<GPoint>& seaside1, const QSet<GPoint>& seaside2, const GPoint point);

    // Generate a set that contains all of the points that can be considered as potential endpoints for the shoreline generation.
    static QSet<GPoint> generatePotentialEndpoints(const QSet<GPoint>& island, const QSet<GPoint>& seaside, const SquareType insideType, const QSet<GPoint>& seaside1, const QSet<GPoint>& seaside2);

    // Prune the potential endpoints and divide it into two coeherent groups.
    static std::tuple<std::vector<GPoint>, std::vector<GPoint>> dividePotentialEndpoints(QSet<GPoint>* potentialEndpoints);

    static std::tuple<bool, bool, QSet<GPoint>> checkSeasideForEndpointAccess(const std::vector<GPoint>& potentialLeftEndpoints, const std::vector<GPoint>& potentialRightEndpoints, QSet<GPoint>* sourceSeaside, const GPoint& root,  const GPoint excludedP);

    // Make sure the seasides are coeherent and bounded by the island + endpoints border.
    static void convertSeasidesBasedOnEndpoints(const QSet<GPoint> seaside, const std::vector<GPoint>& potentialLeftEndpoints, const std::vector<GPoint>& potentialRightEndpoints, QSet<GPoint>* seaside1, QSet<GPoint>* seaside2); 

    // Remove points from both seasides that can make generated shoreline not end in the endpoints.
    static void removeDangerousPoints(const QSet<GPoint>& island, const QSet<GPoint>& seaside, QSet<GPoint>* seaside1, QSet<GPoint>* seaside2, const GPoint& endpoint, const std::vector<GPoint>& potentialLeftEndpoints, const std::vector<GPoint>& potentialRightEndpoints);

    // This is to ensure proper DD generation.
    static void swapEndpointsIfNeeded(const SquareType insideType, GVector2D* lp, GVector2D* rp);

    // Find set of points which creates perimeter of given set
    static QSet<GPoint> findPerimeterPoints(const QSet<GPoint>& set);


    static std::vector<GVector2D> findMidpointsPerLandmass(const std::vector<Landmass>& landmasses);

    // logic for connecting landmasses on seaside
    static std::optional<QSet<GPoint>> findConnectingPathBetweenLandmasses(const Landmass& landmassFrom, const Landmass& landmassTo, const QSet<GPoint>& avaliableSeaside, const QHash<GPoint, std::optional<int>>& landmassIndexAffiliation, const QSet<GPoint>& illegalPoints);

    static QSet<int> connectLandmasses(Landmass* landmassToConnectTo, Landmass* landmassToBeConnected, const QSet<GPoint>& pathBetweenLandmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const SquareType insideType);

    static bool tryConnectingLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType, const std::pair<int, int>& landmassesToConnect, const bool mustBeJoined, const bool avoidIfInApproximityToOtherLandmass);

    static void generateRandomLandmassAreas(std::vector<Landmass>* landmasses, QSet<GPoint> avaliableSeaside, const IslandParameters& params, SquareType insideType, bool treatPerimeterAsOutsideBounds /*= false*/);

    static void populateSeasideWithVirtualLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const IslandParameters& islandParams);

    static void generateVirtualLandmassFromPoint(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const GPoint& startPoint, int targetLandmassSize);

    static void joinTooCloseLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType);

    static void distributeVirtualLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType, const IslandParameters& islandParams);

    static void joinRandomOriginLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType, const IslandParameters& islandParams);

    static void consumeLandmassesInsideLandmasses(std::vector<Landmass>* landmasses, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation);

    static void resolveOutsideTypePointsInsideLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& outsideTypePoints, const SquareType insideType);

    static void assignMustHaveShorelineAreas(std::vector<Landmass>* landmasses, const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalPoints);

    static void assignMustHaveShorelineAreas(Landmass* landmass, const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalPoints);

    static void assignBonusSeasidePointsToLandmasses(std::vector<Landmass>* landmasses, const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalPoints, const float sizePara);

    static void generateShorelineMarkers(Landmass* landmass, const IslandParameters& params, const SquareType& insideType);

    static void updateLandmassAreas(Landmass* landmass);

    static std::vector<QVector3D> createCoastShorelinePath(const QSet<GPoint>& land, const QSet<GPoint>& seaside, const IslandParameters& params, SquareType insideType);

    static std::vector<QVector3D> createIslandShorelinePath(const QSet<GPoint>& land, const QSet<GPoint>& seaside, const IslandParameters& params, SquareType insideType);

    static QSharedPointer<DLandmassMarker> generateLandmassMarker(const Landmass& landmass);

    static std::vector<QSharedPointer<DLandmassMarker>> generateLandmassesMarkers(const std::vector<Landmass>& landmasses);

    static std::vector<QVector3D> createCoastlinePath(const SquareType& insideType, const QSet<GPoint>& seaside, const QSet<GPoint>& land, const GVector2D& p1, const GVector2D& p2, float complexity);

    static std::vector<QVector3D> createIslandPath(const SquareType& insideType, const QSet<GPoint>& seaside1, const QSet<GPoint>& seaside2, const GVector2D& leftEndpoint, const GVector2D& rightEndpoint, float complexity);

    QString name;
    QWeakPointer<DLandmassMarker> landmass;
    QSharedPointer<BayNode> baysRoot;
    QSharedPointer<BayNode> peninsulasRoot;
    float segmentWidth;
    QSet<GPoint> squares;
    std::vector<float> shorelineHeights;

    mutable qint64 landmassGuid; // for serialization

    FRIEND_OMNIBIN(DShorelineMarker);
};

inline void omniSave(const DShorelineMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DLineMarker&>(object);
    omniBin << object.name;
    omniBin << object.baysRoot;
    omniBin << object.peninsulasRoot;
    omniBin << object.segmentWidth;
    omniBin << object.squares;
    omniBin << object.shorelineHeights;
    omniBin << object.landmassGuid;
}

inline void omniLoad(DShorelineMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DLineMarker&>(object);
    omniBin >> object.name;
    omniBin >> object.baysRoot;
    omniBin >> object.peninsulasRoot;
    omniBin >> object.segmentWidth;
    omniBin >> const_cast<QSet<GPoint>&>(object.squares);
    omniBin >> object.shorelineHeights;
    omniBin >> object.landmassGuid;
}