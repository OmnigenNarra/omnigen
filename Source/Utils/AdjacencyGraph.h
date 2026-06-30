#pragma once
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <memory>

#include "QuadTreeLite.h"

class AdjacencyGraph
{
public:
    struct Node
    {
        Node(const GVector2D& inData)
            : data(inData)
        {}

        GVector2D data;
        std::unordered_set<std::shared_ptr<Node>> neighbors;
        std::mutex neighborsGuard;
    };

private:
    std::vector<std::shared_ptr<Node>> nodes;
    std::unique_ptr<std::mutex> globalNodesGuard;

    mutable std::unique_ptr<tml::qtree<float, int>> nodesQTree;
    mutable bool isTreeDirty = true;

    float maxEdgeSize = 0.f;

    bool allowIdenticalEntries = true;
public:
    AdjacencyGraph(const bool inAllowIdenticalEntries = true)
        : globalNodesGuard(std::make_unique<std::mutex>()), allowIdenticalEntries(inAllowIdenticalEntries)
    {}

    [[nodiscard]] const std::vector<std::shared_ptr<Node>>& getNodes() const { return nodes; }

    [[nodiscard]] float getMaxEdgeSize() const { return maxEdgeSize; }

    const auto& getNodesQTree() const
    {
        if (!isTreeDirty)
            return nodesQTree;

        nodesQTree.reset();
        nodesQTree = std::make_unique<tml::qtree<float, int>>(0, 0,
            GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH, GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH);

        for (auto i = 0; i < nodes.size(); i++)
        {
            auto&& n = nodes[i];
            nodesQTree->add_node(n->data.x, n->data.z, i);
        }

        isTreeDirty = false;
        return nodesQTree;
    }

    [[nodiscard]] std::shared_ptr<Node> getNode(size_t idx) const
    {
        return nodes[idx];
    }

    [[nodiscard]] size_t getNodeIdx(GVector2D inData) const
    {
        if (const auto r = getNodesQTree()->find_nearest(inData.x, inData.z, 10.f); r)
            return r->data;

        return -1;
    }

    size_t addNode(GVector2D inData)
    {
        std::scoped_lock lock(*globalNodesGuard);
        if (!allowIdenticalEntries)
        {
            if (const auto d = getNodesQTree()->find_nearest(inData.x, inData.z, 1.f))
                return d->data;
        }

        nodes.emplace_back(std::make_shared<Node>(inData));

        isTreeDirty = true;

        return nodes.size() - 1;
    }

    void addEdge(const size_t idxA, const size_t idxB)
    {
        const auto nodeA = getNode(idxA);
        const auto nodeB = getNode(idxB);
        {
            std::scoped_lock lockA(nodeA->neighborsGuard);
            if (nodeA != nodeB && !contains(nodeA->neighbors, nodeB))
                nodeA->neighbors.insert(nodeB);

            if (const auto d = distance(nodeA->data, nodeB->data); d > maxEdgeSize)
                maxEdgeSize = d;
        }
        {
            std::scoped_lock lockB(nodeB->neighborsGuard);
            if (nodeA != nodeB && !contains(nodeB->neighbors, nodeA))
                nodeB->neighbors.insert(nodeA);
        }
    }

    void removeNode(const size_t idx)
    {
        const auto node = getNode(idx);
        for (auto&& n : node->neighbors)
        {
            std::scoped_lock lockA(n->neighborsGuard);
            n->neighbors.erase(node);
        }
        {
            std::scoped_lock lockA(*globalNodesGuard);
            removeOne(nodes, node);
            isTreeDirty = true;
        }
    }

    void removeEdge(const size_t idxA, const size_t idxB) const
    {
        const auto nodeA = getNode(idxA);
        const auto nodeB = getNode(idxB);
        {
            std::scoped_lock lockA(nodeA->neighborsGuard);
            nodeA->neighbors.erase(nodeB);
        }
        {
            std::scoped_lock lockB(nodeB->neighborsGuard);
            nodeB->neighbors.erase(nodeA);
        }
    }
};
