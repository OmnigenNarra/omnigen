#pragma once
#include <unordered_map>

#include "OmniBin/OmniBin.h"

namespace TrianglesGraph
{
    struct NodeAddress
    {
        size_t clusterIdx = 0;
        size_t triangleIdx = 0;

        bool operator==(const TrianglesGraph::NodeAddress& other) const
        {
            return clusterIdx == other.clusterIdx && triangleIdx == other.triangleIdx;
        }
    };

}

namespace std
{
    template<> struct hash<TrianglesGraph::NodeAddress>
    {
        size_t operator()(TrianglesGraph::NodeAddress const& na) const noexcept
        {
            size_t result = 0;
            result = (result << 1) ^ hash<size_t>{}(na.clusterIdx);
            result = (result << 1) ^ hash<size_t>{}(na.triangleIdx);
            return result;
        }
    };
}


namespace TrianglesGraph
{
    class TriangleAdjacencyGraph
    {
    public:

        TriangleAdjacencyGraph() = default;

        struct Neighbor
        {
            NodeAddress nodeAddress;
            size_t nodeIndex = 0;
            float length = 0.f;
        };

        struct Node
        {
            QVector3D pos;
            std::array<Neighbor, 3> neighbors;
            int neighborsCount = 0;
            NodeAddress nodeAddress;
        };

        [[nodiscard]] const std::vector<Node>& getNodes() const { return nodes; }
        [[nodiscard]] std::vector<Node>& getNodes() { return nodes; }
        [[nodiscard]] const std::unordered_map<NodeAddress, size_t>& getIndexMap() const { return indexMap; }

        static std::unordered_map<int, TriangleAdjacencyGraph> calcTrianglesGraphByClusters();
        static TriangleAdjacencyGraph mergeClustersGraphs(std::unordered_map<int, TriangleAdjacencyGraph>& graphsMap);

    private:

        std::vector<Node> nodes;
        std::unordered_map<NodeAddress, size_t> indexMap;
    };

    class TwiGraph
    {
    public:

        struct TwiNeighbor
        {
            float weight = 0.f;
            float absorptionFactor = 0.f;
            NodeAddress nodeAddress;
            size_t nodeIndex = 0;
            int bufferIndex = 0;
            bool isEmptyLink = true;
        };

        struct TwiNode
        {
            QVector3D pos;
            float flow = 0.f;
            float weightSum = 0.f;
            std::array<TwiNeighbor, 3> in;
            std::array<TwiNeighbor, 3> out;
        };

        TwiGraph(TriangleAdjacencyGraph& graph) : baseGraph(graph) {}
        void calculateGraph();
        void calculateTwiData();

        const std::vector<TwiNode>& getNodes() const { return twiGraph; }

        void drawTwiGraph() const;

        float getRelativeWetnessValue(size_t index) const;
        float getRelativeWetnessValue(const NodeAddress& nodeAddress) const;
        float getRelativeWetnessValue(size_t clusterIdx, size_t triangleIdx) const;

    private:

        void traverseNode(size_t index);

    private:

        std::vector<TwiNode> twiGraph;

        std::array<std::vector<float>, 3> flowBuffers;

        TriangleAdjacencyGraph& baseGraph;
    };

    inline std::vector<int> intersect(const std::vector<int>& v1, const std::vector<int>& v2)
    {
        std::vector<int> result;
        std::ranges::set_intersection(v1, v2,
                                      back_inserter(result));
        return result;
    };
}

