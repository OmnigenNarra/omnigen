#pragma once
#include <stack>
#include <Mathematics/MinimalCycleBasis.h>

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Utils/AdjacencyGraph.h"
#include "Utils/Polygon.h"

enum class EUrbanSize
{
    Outpost,
    Village,
    Town,
    LargeTown,
    HugeTown,

    Last
};

enum class ERoadWidth
{
    Alley,
    Street,
    MainRoad,
    Highway
};

struct RoadGraphData
{
    std::vector<std::array<float, 2>> nodes = {};
    std::vector<std::array<int, 2>> edges = {};

    RoadGraphData() = default;
};

struct RoadMergeInfo
{
    GVector2D nodeToAdd;
    std::vector<std::pair<size_t, size_t>> edgesToRemove;
    std::vector<size_t> nodesToConnectTo;
    std::vector<size_t> nodesToConnectFrom;

    RoadMergeInfo() = default;
};

class MCBComputer
{
public:
    explicit MCBComputer(const RoadGraphData& inGraph);
    const auto& getLots() const { return lots; }

private:
    void fixEdgeOrdering(const std::vector<int>& edgesToSwap);

    //Reads all of the resulting MCB cycles and fills the lots vector
    void consumeForest(const std::vector<std::shared_ptr<gte::MinimalCycleBasis<float>::Tree>>& inForest);

    RoadGraphData data;
    std::vector<Polygon2D> lots;
};

// A utility class that should only contain static methods and no state
class UrbanUtils final
{
public:
    // Expands an urban site to the desired area size.
    static std::pair<float, QSet<int>> calculateSiteArea(const int firstIdx, const float areaSize);

    // Expands a flatland cluster until no adjacent flatland exists or max city size is reached.
    static std::pair<float, QSet<int>> calculateFlatlandArea(const int firstIdx);

    // Get points from right and left side, transformed by road width size
    static std::tuple<std::vector<GVector2D>, std::vector<GVector2D>> getPointsAtOffset(const std::vector<GVector2D>& points, ERoadWidth roadWidth);

    // Get points from right and left side, offsetted by a magic number. You should prefer the enumerator overload when applicable.
    static std::tuple<std::vector<GVector2D>, std::vector<GVector2D>> getPointsAtOffset(const std::vector<GVector2D>& points, float offset);

    // Get smoothed line with with given segments lenght interval
    static std::vector<GVector2D> smoothLine(const std::vector<GVector2D>& line, int lenghtInterval);

    // Get EUrbanSize values as a float representing area size.
    static float getUrbanSizeAsFloat(const EUrbanSize& inSize);

    // Gets the closest EUrbanSize value that is less than the float given.
    static EUrbanSize getFloatAsUrbanSize(const float inSize);

    // Get the allowed block types urban objects (Roads, Buildings) can generate on.
    static constexpr std::array<Generation::ETerrainBlock, 3> getAllowedBlockTypes()
    {
        return {
        Generation::ETerrainBlock::Beach,
        Generation::ETerrainBlock::Flatland,
        Generation::ETerrainBlock::Fault
        };
    }
    
    //Gets the given road width as a float.
    static float getRoadWidthFromEnum(const ERoadWidth inWidth);

    //Get the average height for a collection of 3D points.
    //This contrary to the functionality in DRoadMarker will not cache the result and will hence recompute every time
    static float getPointHeightAverage(const std::vector<QVector3D>& pts);

    /*
    * Cast a road streamline point to 3d. If no height median is provided, the lowest point will be returned.
    */
    static QVector3D heightQuery(const GVector2D& p, const float heightMedian = -1.f);

    static void removeGraphIntersections(AdjacencyGraph* inGraph);
    static RoadGraphData getDataFromAdjacencyGraph(const AdjacencyGraph& inGraph);

    static float fastAngle3(const GVector2D& a, const GVector2D& b, const GVector2D& c);
private:
    static std::vector<QSet<int>> getUrbanSiteNeighbourCellsClusters(const QSet<int>& siteArea);
};
