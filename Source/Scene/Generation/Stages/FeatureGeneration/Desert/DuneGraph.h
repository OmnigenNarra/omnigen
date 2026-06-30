#pragma once

#include "Utils/CoreUtils.h"
#include "Utils/Interpolation.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockUtils.h"


// Abstract graph ---------------------------------------------------------------------------------------------------------------------------------------------------------------

struct BaseNodeConnection
{
    int from;
    int to;
};

struct VertexAddress
{
    int nodeId;
    int vertexId;
};

struct BaseNode
{
    GVector2D center;
    std::vector<int> connections;
    std::vector<int> verticesIdx;
};

struct BaseVertex
{
    GVector2D point;

    VertexAddress address;
    VertexAddress prev;
    VertexAddress next;
};

template<typename T>
concept NodeConnectionConcept = std::is_base_of_v<BaseNodeConnection, T>;

template<typename T>
concept NodeConcept = std::is_base_of_v<BaseNode, T>;

template<typename T>
concept VertexConcept = std::is_base_of_v<BaseVertex, T>;


// Graph, representing structure of nodes and vertices(leaves)
// It has restriction of filling order:
// 1) create graph
// 2) add nodes
// 3) add vertices to nodes
// 4) add connections between nodes
template<NodeConcept Node, NodeConnectionConcept NodeConnection, VertexConcept Vertex>
class GeomGraph
{
public:

    Node& getNode(int i) { return nodes[i]; }
    const Node& getNode(int i) const { return nodes[i]; }

    NodeConnection& getConnection(int i) { return connections[i]; }
    const NodeConnection& getConnection(int i) const { return connections[i]; }
    int getConnectionIdx(int node1, int node2) const
    {
        for (int i = 0; i < connections.size(); ++i)
        {
            const NodeConnection& connection = getConnection(i);
            if ((connection.from == node1 && connection.to == node2) || (connection.from == node2 && connection.to == node1))
                return i;
        }

        Q_ASSERT_X(false, "GeomGraph", "Connection between nodes not found!");
        return -1;
    }
    const NodeConnection& getConnection(int node1, int node2) const
    {
        return getConnection(getConnectionIdx(node1, node2));
    }

    Vertex& getVertex(int i) { return vertices[i]; }
    const Vertex& getVertex(int i) const { return vertices[i]; }
    Vertex& getVertex(int node, int i) { return vertices[nodes[node].verticesIdx[i]]; }
    Vertex& getVertex(const Node& node, int i) { return vertices[node.verticesIdx[i]]; }
    Vertex& getLastNodeVertex(int node) { return vertices[nodes[node].verticesIdx.back()]; }
    Vertex& getFirstNodeVertex(int node) { return vertices[nodes[node].verticesIdx.front()]; }

    virtual void debugDraw(int flags)
    {
        for (const auto& connection : connections)
            spawn<DLineMarker>(std::vector<GVector2D>{getNode(connection.from).center, getNode(connection.to).center}, Colors::rose, false, 200.f);

        // ridges
        for (const auto& node: nodes)
        {
            for (const int vIdx: node.verticesIdx)
                spawn<DLineMarker>(std::vector<GVector2D>{node.center, getVertex(vIdx).point}, Colors::red, false, 200.f);
        }

        std::vector<GVector2D> bottomPts;
        bottomPts.reserve(vertices.size());

        traverseVertices([&bottomPts](const Vertex& v)
        {
            bottomPts << v.point;
        });

        spawn<DLineMarker>(bottomPts, Colors::springGreen, true, 200.f);
    }

    void traverseVertices(const std::function<void(const Vertex&)> func)
    {
        if (vertices.empty())
            return;

        const Vertex* current = &vertices.front(); // start from 0 vertex
        while (true)
        {
            func(*current);
            const int next = current->next.vertexId;
            if (next == 0) // cycle completed, find start vertex
                break;
            current = &vertices[next];
        }
    }

    int addNode(const Node& node)
    {
        nodes << node;
        return nodes.size() - 1;
    }

    int addVertex(int nodeIdx, const Vertex& vertex)
    {
        vertices << vertex;
        const int vId = (int)vertices.size() - 1;
        Node& node = getNode(nodeIdx);
        node.verticesIdx << vId;
        const int vIndexLocal = (int)node.verticesIdx.size() - 1;

        Vertex& currVertex = vertices[vId];
        currVertex.address = { nodeIdx, vId };

        if (vIndexLocal == 0)
        {
            currVertex.prev = currVertex.address;
            currVertex.next = currVertex.address;
            return vId;
        }

        const int nextLocal = 0;
        const int prevLocal = vIndexLocal - 1;
        const int prevIdx = getVertex(node, prevLocal).address.vertexId;
        const int nextIdx = getVertex(node, nextLocal).address.vertexId;

        currVertex.prev = { nodeIdx, prevIdx};
        currVertex.next = { nodeIdx, nextIdx };
        vertices[prevIdx].next = currVertex.address;
        vertices[nextIdx].prev = currVertex.address;

        return vId;
    }

    void addConnection(int fromNode, int toNode, const std::array<int, 2>& fromVertexIndex, const std::array<bool, 2>& direction,
        const std::array<int, 2>& toVertexIndex)
    {
        NodeConnection connection;
        connection.from = fromNode;
        connection.to = toNode;
        initConnection(connection);
        connections << std::move(connection);
        const auto connectionIdx = connections.size() - 1;

        // nodes
        getNode(fromNode).connections << connectionIdx;
        getNode(toNode).connections << connectionIdx;

        // vertices
        for (int i = 0; i < 2; ++i)
        {
            Vertex& fromCurr = getVertex(fromVertexIndex[i]);
            Vertex& fromPrev = vertices[fromCurr.prev.vertexId];
            Vertex& fromNext = vertices[fromCurr.next.vertexId];

            Vertex& toCurr = getVertex(toVertexIndex[i]);
            Vertex& toPrev = vertices[toCurr.prev.vertexId];
            Vertex& toNext = vertices[toCurr.next.vertexId];

            const VertexAddress& fromAddress = fromCurr.address;
            const VertexAddress& toAddress = toCurr.address;

            if (direction[i])
            {
                fromCurr.next = toAddress;
                toCurr.prev = fromAddress;
            }
            else
            {
                fromCurr.prev = toAddress;
                toCurr.next = fromAddress;
            }
        }

    }

public:

    std::vector<Node> nodes;
    std::vector<NodeConnection> connections;
    std::vector<Vertex> vertices;

protected:

    virtual void initConnection(NodeConnection& connection) {}

};


// Dune Structures ----------------------------------------------------------------------------------------------------------------------------------------------------------------

using DuneConnection = BaseNodeConnection;

struct DuneNode : public BaseNode
{
    float height = 0.f;

    DuneNode() = default;
    DuneNode(const GVector2D& pt, float h): height(h) { center = pt; }
};


struct DuneVertex : public BaseVertex
{
    float height = 0.f;

    DuneVertex() = default;
    DuneVertex(const GVector2D& pt, float h): height(h) { point = pt; }
};

// returns height, param is t [0, 1]
// Should behave like 0-1 interpolation, f(0) = 0, f(1) = 1
using HeightFunction = std::function<float(float)>;


struct DuneCurve
{
    BezierCurve2D curve;
    HeightFunction heightFunc;
    Polygon2D restrictionPolygon;
};


struct DuneFace
{
    int startVertex = 0;
    int endVertex = 0;
    DuneCurve bottomCurve;
    std::array<std::shared_ptr<DuneCurve>, 2> sideRigde = { nullptr, nullptr };
    std::shared_ptr<DuneCurve> topRidge = nullptr;
    bool isTopRidgeReversed = false;

    // first - is main polygon, second is additional bottom polygon
    std::array<Polygon2D, 2> getPolygons(int curvePointsCount) const;

    std::vector<GVector2D> points;
    std::vector<float> xPts;
    std::vector<float> zPts;
    std::vector<float> yPts;
    int ridgesPointsCount = 0;
};

// Dune graph ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

class DuneGraph : public GeomGraph<DuneNode, DuneConnection, DuneVertex>
{
public:

    float bottomCurveTopPointFactor = 0.5f;

public:

    void generateRidgesAndFaces(const std::vector<HeightFunction>& ridgeHeightFuncVec, const std::vector<HeightFunction>& topRidgeHeightFuncVec, const std::function<float(const GVector2D&)>& bottomHeightFunc);

    const std::vector<DuneFace>& getFaces() const;

    float getPointOnFaceHeight(const DuneFace& face, const GVector2D& point) const; //const std::function<float(const GVector2D&)>& bottomHeightFunc

    Polygon2D getBoundingPolygon() const;

    enum class DebugDrawFlags: int
    {
        None              = 0x00,
        ConnectionsScheme = 0x01,
        Curves            = 0x02,
        BottomRPolygons   = 0x04,
        RidgeRPolygons    = 0x08,
        TopRidgeRPolygons = 0x10,
        All = ConnectionsScheme & Curves & BottomRPolygons & RidgeRPolygons & TopRidgeRPolygons
    };

    virtual void debugDraw(int flags) override;

    MeshConnector createMesh(const Polygon2D& clusterPolygon,
        const std::vector<Polygon2D>& surroundingPolygons,
        const std::function<QVector3D(const QVector3D&)>& surroundingPoint3dFunction,
        const std::function<float(const GVector2D& pt)>& bottomEdgeHeightFunction) const;

    MeshConnector meshClusterPolygon(const Polygon2D& clusterPolygon, const std::function<float(const GVector2D&)>& bottomEdgeHeightFunction);

protected:

    std::unordered_map<int, std::shared_ptr<DuneCurve>> sideRidges; // key = vertexId
    std::unordered_map<int, std::shared_ptr<DuneCurve>> topRidges;  // key = connectionId
    std::vector<DuneFace> faces;

    std::function<float(const GVector2D&)> bottomHeightFunction;

protected:

    void cachePoints(DuneFace& face, const std::function<float(const GVector2D&)>& bottomHeightFunc);

    Polygon2D getSideRidgeRestrictionPolygon(const DuneVertex& vertex) const;
    Polygon2D getTopRidgeRestrictionPolygon(const DuneConnection& connection) const;
    Polygon2D getBottomRidgeRestrictionPolygon(const DuneVertex& vertex, const DuneVertex& nextVertex) const;

    float getSideRidgePointHeight(int vertexId, float curveParam) const;
    float getTopRidgePointHeight(int connectionId, float curveParam) const;

};

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

