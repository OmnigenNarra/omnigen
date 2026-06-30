#pragma once
#include <QMap>
#include <QVector>
#include <QVector3D>
#include "Editor/Sections/OmniLog/OmniLogger.h"
#include "Utils/OmniBin/OmniBinQt.h"

struct PathDetails
{
    float distance = 0;
    std::vector<int> verticesReached;
};

inline void omniSave(const PathDetails& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.distance;
    omniBin << object.verticesReached;
}

inline void omniLoad(PathDetails& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.distance;
    omniBin >> object.verticesReached;
}

template<typename Data, typename WeightPred>
class Graph
{
public:
	Graph() = default;

	const auto& getVertices() const { return vertices; }
	const auto& getEdges() const { return edges; }
	const auto& getPaths() const { return paths; }

	void addVertex(const Data& v)
	{
        // No duplicates
        for (auto&& pv : vertices)
            if (pv == v)
                return;

        // Find index
        int idx = 0;
        while (vertices.contains(idx))
            ++idx;

        vertices[idx] = v;
	}

	void removeVertex(int index)
	{
        if (!vertices.contains(index))
            return;

        vertices.remove(index);

        while (true)
        {
            bool found = false;
            for (auto&& [from, to] : edges.keys())
            {
                if (from == index || to == index)
                {
                    found = true;
                    edges.remove({ from, to });
                }
            }

            if (!found)
                break;
        }
	}

    void removeVertex(const Data& v)
    {
        for (auto it = vertices.keyValueBegin(); it != vertices.keyValueEnd(); ++it)
            if ((*it).second == v)
                return removeVertex((*it).first);
    }
		
    void addEdge(const Data& v1, const Data& v2)
    {
        auto [source, target] = getValueKeys<2>({ v1, v2 });
        if (source < 0 || target < 0)
            return;

        edges[{source, target}] = computeWeight(vertices[source], vertices[target]);
    }

    void removeEdge(const Data& v1, const Data& v2)
    {
        auto [source, target] = getValueKeys<2>({ v1, v2 });
        edges.remove({ source, target });
    }

    void calculateDistances()
    {
        for (int key : vertices.keys())
            calculateDistancesFromSource(key);
    }

    std::optional<PathDetails> getOptimalPath(const Data& v1, const Data& v2) const
    {
        auto [source, target] = getValueKeys<2>({ v1, v2 });
        if (source == target)
            return PathDetails{ 0.0f, std::vector{source} };

        if (!paths.contains({source, target}))
            return {};

        return paths[{source, target}];
    }

    void reset()
    {
        vertices.clear();
        edges.clear();
        paths.clear();
    }

private:
    template<size_t s>
    std::array<int, s> getValueKeys(const std::array<Data, s>& data) const
    {
        std::array<int, s> result;
        std::fill(result.begin(), result.end(), -1);

        for (int key : vertices.keys())
            for (size_t i = 0; i < s; ++i)
                if (vertices[key] == data[i])
                    result[i] = key;

        return result;
    }

    void calculateDistancesFromSource(int source)
    {
        QMap<int, float> distancesFromSource;
        QMap<int, bool> nodeIncludedInPath;
        QMap<int, int> parents;

        for (int key : vertices.keys())
        {
            distancesFromSource[key] = std::numeric_limits<float>::max();
            nodeIncludedInPath[key] = false;
        }

        distancesFromSource[source] = 0.0f;

        for (int from_key : vertices.keys())
        {
            int minDistanceVertex = utilityMinimumDistanceVertexIndex(distancesFromSource, nodeIncludedInPath);
            nodeIncludedInPath[minDistanceVertex] = true;

            for (int to_key : vertices.keys())
            {
                if (!nodeIncludedInPath[to_key] && edges.contains({ minDistanceVertex, to_key }) && ((distancesFromSource[minDistanceVertex] + edges[{ minDistanceVertex, to_key }] < distancesFromSource[to_key])))
                {
                    parents[to_key] = minDistanceVertex;
                    distancesFromSource[to_key] = distancesFromSource[minDistanceVertex] + edges[{ minDistanceVertex, to_key }];
                }
            }

            calculateSpanningTrees(source, distancesFromSource, parents);
        }
    }

    int utilityMinimumDistanceVertexIndex(const QMap<int, float>& dist, const QMap<int, bool>& sPath)
    {
        float shortestDistance = std::numeric_limits<float>::max();
        int minIndex = -1;

        for (int to_key : vertices.keys())
        {
            if (!sPath[to_key] && (dist[to_key] < shortestDistance))
            {
                shortestDistance = dist[to_key];
                minIndex = to_key;
            }
        }

        return minIndex;
    }

    void calculateSpanningTrees(int source, const QMap<int, float>& dist, const QMap<int, int>& parents)
    {
        for (int to_key : vertices.keys())
        {
            if (to_key == source)
                continue;

            PathDetails path{ dist[to_key], returnPath({source}, parents, to_key) };
            if (path.verticesReached.size() > 1)
                paths[{source, to_key}] = path;
        }
    }

    std::vector<int> returnPath(const std::vector<int>& previousPath, const QMap<int, int>& parents, int index)
    {
        std::vector<int> result = previousPath;

        if (!parents.contains(index))
            return result;

        result = returnPath(result, parents, parents[index]);
        result.push_back(index);
        return result;
    }
		
public:
    QMap<int, Data> vertices;
    QMap<QPair<int, int>, float> edges;
    QMap<QPair<int, int>, PathDetails> paths;
    WeightPred computeWeight;
};

template<typename Data, typename WeightPred>
inline void omniSave(const Graph<Data, WeightPred>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.vertices;
    omniBin << object.edges;
    omniBin << object.paths;
}

template<typename Data, typename WeightPred>
inline void omniLoad(Graph<Data, WeightPred>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.vertices;
    omniBin >> object.edges;
    omniBin >> object.paths;
}

template<typename Data, typename WeightPred>
auto createGraph(const WeightPred&)
{
	return Graph<Data, WeightPred>();
}

template<typename Data, typename DistPred>
QString toQString(const Graph<Data, DistPred>& graph)
{
    QString output;

    output += ("Vertices (" + toQString(graph.getVertices().size()) + "):\n");
    for (int key : graph.getVertices().keys())
        output += toQString(graph.getVertices()[key]) + "\n";

    output += "\n";

    output += "Edges (" + toQString(graph.getEdges().size()) + "):\n";
    for (auto it = graph.getEdges().keyValueBegin(); it != graph.getEdges().keyValueEnd(); ++it)
        output += toQString(graph.getVertices()[(*it).first.first]) + " -> " + toQString(graph.getVertices()[(*it).first.second]) + " <" + toQString((*it).second) + ">\n";

    output += "\n";

    output += "Optimal paths (" + toQString(graph.getPaths().size()) + "):\n";
    for (auto it = graph.getPaths().keyValueBegin(); it != graph.getPaths().keyValueEnd(); ++it)
    {
        output += toQString(graph.getVertices()[(*it).first.first]) + " -> " + toQString(graph.getVertices()[(*it).first.second]) + "\n";
        for (auto&& key : (*it).second.verticesReached)
            output += " -> " + toQString(graph.getVertices()[key]);

        output += "\n-----\n";
    }

    output += "\n";
    return output;
}