#include "stdafx.h"
#include "TriangleAdjacencyUtils.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include <unordered_set>
#include <tbb/parallel_for.h>
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/Common/Markers/MultiLineColoredMarker.h"
#include "Utils/Triangulation/Triangulation.h"


namespace TrianglesGraph
{
    static QVector3D calcTrianglePos(NodeAddress nodeAddress)
    {
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& cluster = clusterMap[nodeAddress.clusterIdx];
        auto&& v = cluster->section->mainBuffer->vertices;
        auto indices = cluster->section->getIndices();
        auto&& v0 = v[indices[nodeAddress.triangleIdx * 3 + 0]].position;
        auto&& v1 = v[indices[nodeAddress.triangleIdx * 3 + 1]].position;
        auto&& v2 = v[indices[nodeAddress.triangleIdx * 3 + 2]].position;

        return (v1 + v2 + v0) / 3.0f;
    }

    std::unordered_map<int, TriangleAdjacencyGraph> TriangleAdjacencyGraph::calcTrianglesGraphByClusters()
    {
        OmniProfile("Calc triangles graphs for clusters");

        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        std::unordered_set<IndexType> clustersIds;
        clustersIds.reserve(clusterMap.size() / 3);
        for (auto& cluster : clusterMap)
            clustersIds.insert(cluster->keyCell);
        std::vector<IndexType> clustersIdsVec(clustersIds.begin(), clustersIds.end());

        std::unordered_map<int, TriangleAdjacencyGraph> adjacentTrianglesByClusterId;
        adjacentTrianglesByClusterId.reserve(clustersIdsVec.size());
        for (const IndexType clusterId: clustersIdsVec)
        {
            TriangleAdjacencyGraph& graph = adjacentTrianglesByClusterId[clusterId];
            graph.nodes.resize(clusterMap[clusterId]->section->getIndexBufferSize() / 3);
        }

        tbb::parallel_for((size_t)0, clustersIdsVec.size(), [&](size_t i)
        {
            const IndexType clusterId = clustersIdsVec[i];
            auto&& cluster = clusterMap[clusterId];
            auto indices = cluster->section->getIndices();
            IndexType triOffset = cluster->section->getVertexBufferOffset();
            auto& graph = adjacentTrianglesByClusterId[clusterId];

            std::unordered_map<triangulation::Edge, size_t> edgesToTrianglesMap;
            edgesToTrianglesMap.reserve(indices.size());

            const auto processEdge = [&edgesToTrianglesMap, &graph, clusterId](IndexType index1, IndexType index2, IndexType triangleIndex)
            {
                constexpr size_t processedIdx = std::numeric_limits<size_t>::max();
                const triangulation::Edge edge{ (int)index1, (int)index2 };
                const auto iter = edgesToTrianglesMap.find(edge);
                if (iter == edgesToTrianglesMap.end())
                {
                    edgesToTrianglesMap[edge] = triangleIndex;
                }
                else if (iter->second != processedIdx)
                {
                    // add neighbors
                    Node& currNode     = graph.nodes[triangleIndex];
                    Node& neighborNode = graph.nodes[iter->second];
                    const float length = (currNode.pos - neighborNode.pos).length();
                    currNode.neighbors[currNode.neighborsCount]         = Neighbor{NodeAddress{ clusterId, iter->second  }, 0, length };
                    neighborNode.neighbors[neighborNode.neighborsCount] = Neighbor{NodeAddress{ clusterId, triangleIndex }, 0, length };
                    ++currNode.neighborsCount;
                    ++neighborNode.neighborsCount;
                    edgesToTrianglesMap[edge] = processedIdx; // edge is already processed
                }
            };

            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const IndexType i1 = indices[i] - triOffset;
                const IndexType i2 = indices[i + 1] - triOffset;
                const IndexType i3 = indices[i + 2] - triOffset;

                const IndexType triangleIdx = i / 3;
                const NodeAddress nodeAddress = NodeAddress{ clusterId, triangleIdx };
                graph.nodes[triangleIdx].nodeAddress = nodeAddress;
                graph.nodes[triangleIdx].pos = calcTrianglePos(nodeAddress);

                processEdge(i1, i2, triangleIdx);
                processEdge(i2, i3, triangleIdx);
                processEdge(i1, i3, triangleIdx);
            }
        });

        return adjacentTrianglesByClusterId;
    }


    TriangleAdjacencyGraph TriangleAdjacencyGraph::mergeClustersGraphs(std::unordered_map<int, TriangleAdjacencyGraph>& graphsMap)
    {
        OmniProfile("Merge cluster's graphs to global");
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        auto&& borderPoints = Generation::Data::get()->getTerrainBorderPoints();

        int totalCountNodes = 0;
        std::unordered_set<IndexType> clustersIds;
        clustersIds.reserve(graphsMap.size() / 3);
        for (const auto& [clusterId, graph]: graphsMap)
        {
            totalCountNodes += graph.getNodes().size();
            clustersIds.insert(clusterId);
        }
        std::vector<IndexType> clustersIdsVec(clustersIds.begin(), clustersIds.end());

        tbb::parallel_for((size_t)0, clustersIdsVec.size(), [&](size_t i)
        {
            const IndexType clusterId = clustersIdsVec[i];
            TriangleAdjacencyGraph& graph = graphsMap[clusterId];
            const auto indices = clusterMap[clusterId]->section->getIndices();
            const auto& vertices = clusterMap[clusterId]->section->mainBuffer->vertices;
            auto& nodes = graph.getNodes();

            const auto findClustersIds = [&](const Generation::BorderPoint& bp)
            {
                std::vector<int> clustersIds;
                clustersIds.reserve(4);
                for (auto&& [neighborClusterId, neighborFacesData] : bp.intermediateData)
                {
                    if (neighborClusterId != clusterId)
                        clustersIds << neighborClusterId;
                }
                return clustersIds;
            };

            const auto calcNeighborFaces = [&](const Generation::BorderPoint& bp, IndexType neighborClusterId)
            {
                std::vector<int> neighborFaces;
                neighborFaces.reserve(5);
                for (auto&& [blockId, faces] : bp.intermediateData[neighborClusterId].facesPerBlock)
                {
                    neighborFaces.insert(neighborFaces.end(), faces.begin(), faces.end());
                }
                return neighborFaces;
            };

            for (size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx)
            {
                Node& node = nodes[nodeIdx];
                if (node.neighborsCount == 3)
                    continue;

                // find border points
                const IndexType i1 = indices[nodeIdx * 3 + 0];
                const IndexType i2 = indices[nodeIdx * 3 + 1];
                const IndexType i3 = indices[nodeIdx * 3 + 2];

                const QHash<GVector2D, Generation::BorderPoint>::const_iterator bpIterArray[3] = {
                    borderPoints.constFind(vertices[i1].position),
                    borderPoints.constFind(vertices[i2].position),
                    borderPoints.constFind(vertices[i3].position)
                };

                for (int edge = 0; edge < 3; ++edge)
                {
                    const auto& bpIter1 = bpIterArray[edge];
                    if (bpIter1 == borderPoints.end())
                        continue;
                    const auto& bpIter2 = bpIterArray[(edge + 1) % 3];
                    if (bpIter2 == borderPoints.end())
                        continue;

                    // find neighbor clusters
                    auto&& neighborClustersIds1 = findClustersIds(*bpIter1);
                    auto&& neighborClustersIds2 = findClustersIds(*bpIter2);
                    auto&& intersection = intersect(neighborClustersIds1, neighborClustersIds2);
                    // Q_ASSERT(intersection.size() <= 1);
                    if (intersection.empty())
                        continue;
                    const int neighborClusterId = intersection.front();

                    // find neighbor faces
                    const auto neighborFaces1 = calcNeighborFaces(*bpIter1, neighborClusterId);
                    const auto neighborFaces2 = calcNeighborFaces(*bpIter2, neighborClusterId);
                    const auto commonNeighborFaces = intersect(neighborFaces1, neighborFaces2);
                    // Q_ASSERT(commonNeighborFaces.size() == 1);
                    if (commonNeighborFaces.empty())
                    {
                        const QVector3D& pos1 = bpIter1->intermediateData.begin()->second.v.position;
                        const QVector3D& pos2 = bpIter2->intermediateData.begin()->second.v.position;
                        spawn<DLineMarker>(pos1, 1000.f, Colors::yellow);
                        spawn<DLineMarker>(pos2, 1000.f, Colors::yellow);
                        continue;
                    }
                    const int neighborTriangleIdx = commonNeighborFaces.front();
                    const auto& neighborNode = graphsMap.at(neighborClusterId).nodes[neighborTriangleIdx];
                    const float length = (node.pos -neighborNode.pos).length();
                    node.neighbors[node.neighborsCount] = Neighbor{ NodeAddress{ (IndexType)neighborClusterId, (IndexType)neighborTriangleIdx }, 0, length };
                    ++node.neighborsCount;
                }
            }
        });

        // merge all subgraphs together
        TriangleAdjacencyGraph mergedGraph;
        mergedGraph.nodes.reserve(totalCountNodes);

        for (auto&& cluster: clusterMap)
        {
            const TriangleAdjacencyGraph& graph = graphsMap.at(cluster->keyCell);
            mergedGraph.nodes.insert(mergedGraph.nodes.end(), std::make_move_iterator(graph.nodes.begin()), std::make_move_iterator(graph.nodes.end()));
        }

        // indexMap [address] -> node index
        for (size_t i = 0; i < mergedGraph.nodes.size(); ++i)
        {
            const auto& node = mergedGraph.nodes[i];
            mergedGraph.indexMap[node.nodeAddress] = i;
        }

        return mergedGraph;
    }

    constexpr float flowLimit = 0.7f;

    static QVector3D getDebugColor(float t)
    {
        return QVector3D(t < 0.5f ? 0.8f : 0.f, 1.f - 2.f * fabsf(t - 0.5f), t < 0.5f ? 0.f : 1.f);
    }

    void TwiGraph::drawTwiGraph() const
    {
        OmniProfile("Draw TWI graph");
        std::vector<std::vector<DMultiLineColoredMarker::PointData>> lines;
        lines.reserve(twiGraph.size() * 3);

        for (size_t i = 0; i < twiGraph.size(); ++i)
        {
            const TwiNode& twiNode = twiGraph[i];
            for (const auto& neighbor: twiNode.out)
            {
                if (neighbor.isEmptyLink)
                    continue;

                const TwiNode& neighborTwiNode = twiGraph[neighbor.nodeIndex];
                const float t1 = getRelativeWetnessValue(i);
                const QVector3D color1 = getDebugColor(t1);
                const float t2 = getRelativeWetnessValue(neighbor.nodeIndex);
                const QVector3D color2 = getDebugColor(t2);

                lines <<= std::vector<DMultiLineColoredMarker::PointData>{ DMultiLineColoredMarker::PointData{ twiNode.pos, color1 }, DMultiLineColoredMarker::PointData{ neighborTwiNode.pos, color2 } };
            }
        }

        spawn<DMultiLineColoredMarker>(lines);
    }

    float TwiGraph::getRelativeWetnessValue(size_t index) const
    {
        return std::min(twiGraph[index].flow / flowLimit, 1.f);
    }

    float TwiGraph::getRelativeWetnessValue(const NodeAddress& nodeAddress) const
    {
        return getRelativeWetnessValue(baseGraph.getIndexMap().at(nodeAddress));
    }

    float TwiGraph::getRelativeWetnessValue(size_t clusterIdx, size_t triangleIdx) const
    {
        return getRelativeWetnessValue(NodeAddress{ clusterIdx, triangleIdx });
    }

    void TwiGraph::calculateGraph()
    {
        OmniProfile("Calc TWI graph");

        auto&& baseNodes = baseGraph.getNodes();

        twiGraph.resize(baseNodes.size());

        {
            OmniProfile("Init TWI graph");

            for (int i = 0; i < flowBuffers.size(); ++i)
                flowBuffers[i].resize(baseNodes.size(), 0.f);

            tbb::parallel_for((size_t)0, baseNodes.size(), [&](size_t i)
            {
                const auto& node = baseNodes[i];
                const QVector3D& pos = node.pos;
                const float currHeight = pos.y();
                TwiNode currNode;
                currNode.pos = pos;
                currNode.flow = 1.f; // init with 1 value
                for (int j = 0; j < node.neighborsCount; ++j)
                {
                    const NodeAddress& neighborAddress = node.neighbors[j].nodeAddress;
                    const size_t neighborIndex = baseGraph.getIndexMap().at(neighborAddress);
                    const auto& neighborNode = baseNodes[neighborIndex];
                    const QVector3D& neighborPos = neighborNode.pos;
                    const float neighborHeight = neighborPos.y();
                    const float deltaHeight = currHeight - neighborHeight;
                    const float deltaLength = node.neighbors[j].length; //(pos - neighborPos).length();
                    const float absorptionFactor = std::max(0.95f - 0.2f * fabsf(deltaHeight) / deltaLength, 0.f); // depends on surface steepness
                    const TwiNeighbor neighbor = TwiNeighbor{ deltaHeight, absorptionFactor, neighborAddress, neighborIndex, 0, false };

                    if (deltaHeight <= 0.f)
                        currNode.in[j] = neighbor;
                    if (deltaHeight >= 0.f)
                        currNode.out[j] = neighbor;
                    currNode.weightSum += 1.f - absorptionFactor;
                }
                currNode.weightSum = std::min(currNode.weightSum, 1.f);
                twiGraph[i] = std::move(currNode);
            });
        }

        {
            OmniProfile("Calculate buffer indices");

            tbb::parallel_for((size_t)0, twiGraph.size(), [&](size_t j)
            {
                TwiNode& twiNode = twiGraph[j];
                // set different buffer indices to outputs from nodes, according inputs to current node
                for (int i = 0; i < twiNode.in.size(); ++i)
                {
                    const auto& neighborIn = twiNode.in[i];
                    if (neighborIn.isEmptyLink)
                        continue;
                    const auto& neighborInAddr = neighborIn.nodeAddress;
                    TwiNode& nodeIn = twiGraph[neighborIn.nodeIndex];
                    for (int j = 0; j < nodeIn.out.size(); ++j)
                    {
                        auto& neighborOut = nodeIn.out[j];
                        if (neighborOut.isEmptyLink)
                            continue;
                        if (neighborOut.nodeAddress == neighborInAddr)
                        {
                            neighborOut.bufferIndex = i;
                            break;
                        }
                    }
                }
            });
        }
    }

    void TwiGraph::traverseNode(size_t index)
    {
        auto& twiNode = twiGraph[index];
        const float totalTransferValue = twiNode.flow * twiNode.weightSum;
        twiNode.flow = std::max(twiNode.flow - totalTransferValue, 0.f);

        for (auto& neighbor: twiNode.out)
        {
            if (neighbor.isEmptyLink)
                continue;
            auto& neighborNode = twiGraph[neighbor.nodeIndex];
            const float currentFlowValue = std::max(totalTransferValue * (1.f - neighbor.absorptionFactor) / twiNode.weightSum, 0.f);

            flowBuffers[neighbor.bufferIndex][neighbor.nodeIndex] = currentFlowValue;
        }
    }

    void TwiGraph::calculateTwiData()
    {
        OmniProfile("Calc TWI data");

        constexpr int iterationsCount = 4;
        for (int i = 0; i < iterationsCount; ++i)
        {
            {
                OmniProfile("Flow Simulation");

                tbb::parallel_for((size_t)0, twiGraph.size(), [&](size_t j)
                {
                    traverseNode(j);
                });
            }

            {
                OmniProfile("Accumulate and reset buffers");
                tbb::parallel_for((size_t)0, twiGraph.size(), [&](size_t j)
                {
                    auto& twiNode = twiGraph[j];
                    for (int k = 0; k < flowBuffers.size(); ++k)
                    {
                        twiNode.flow += flowBuffers[k][j];
                        flowBuffers[k][j] = 0.f;
                    }
                });
            }
        }
    }

}
