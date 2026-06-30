#include "stdafx.h"
#include "StageGeneration_Ridges.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "RidgeMarker.h"
#include "../Landmasses/ShorelineMarker.h"
#include "../Landmasses/LandmassBoundMarker.h"
#include "../Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/StageTools/StageTools.h"

class DRidgeMarker;
class RidgePeakData;

#define DEBUG_PEAK_GRAPH 0

namespace Generation
{
    // Generate lines (trees) containing local height maxima
    bool StageGen<EGenerationStage::Ridges>::autoGen()
    {
        if (DRidgeMarker::generateAll())
        {
            Generation::Data::get()->initializeQueuedMarkers();

            auto&& ridgeRoots = Generation::Data::get()->getMarkers<DRidgeMarker>();
            if (ridgeRoots.empty())
                return false;

            std::vector<RidgePeakData> peakData;
            std::unordered_set<qint64> assignedRidges;

            for (auto&& ridge : ridgeRoots)
            {
                assignedRidges.emplace(ridge->getGuid());
                auto&& newData = ridge->getPeakData();
                peakData.insert(peakData.end(), std::make_move_iterator(newData.begin()), std::make_move_iterator(newData.end()));
            }

            calculateRidgelineHeight(&peakData);

            std::vector<QSharedPointer<DRidgeMarker>> ridges;
            Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);
            std::vector<QSharedPointer<DRidgeMarker>> ridgesToAssign = ridgeRoots;

            while(true)
            {
                std::vector<QSharedPointer<DRidgeMarker>> nextRidges;
                for (auto&& ridge : ridgesToAssign)
                {
                    if (auto&& children = assignHeightToRidge(ridge, peakData); !children.empty())
                        nextRidges.insert(nextRidges.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
                }

                if (nextRidges.empty())
                    break;

                ridgesToAssign.clear();
                ridgesToAssign = std::move(nextRidges);
            }
        }

        return true;
    }

    void StageGen<EGenerationStage::Ridges>::clear()
    {
        Data::get()->clearExactMarkers<DRidgeMarker>();
    }

    bool StageGen<EGenerationStage::Ridges>::validate()
    {
        auto&& landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();
        auto&& ridges = Generation::Data::get()->getMarkers<DRidgeMarker>();

        // Each landmass needs at least 1 ridge
        for (auto&& landmass : landmasses)
        {
            if (!std::any_of(ridges.begin(), ridges.end(), [&](auto& r) { return PolygonUtils::contains(r->getControlPoints().front(), landmass->getMainPolygon()); }))
            {
                OmniLog(ELoggingLevel::Error) <<= "At least one Landmass has no Ridges!";
                return false;
            }
        }

        return true;
    }

    void StageGen<EGenerationStage::Ridges>::finalize()
    {
        getStageTools<EGenerationStage::Ridges>()->changeTo3DRidges();
    }

    QSet<QSet<GPoint>> StageGen<EGenerationStage::Ridges>::computeRidgeValidLandmasses()
    {
        QSet<QSet<GPoint>> landmasses;

        for(auto&& landmass : Generation::Data::get()->getMarkers<DLandmassMarker>())
            landmasses += landmass->getSquares();

        return landmasses;
    }

    float computeDelta(float currentDelta, float maxDelta)
    {
        static std::uniform_real_distribution<float> d(0.f, 1.0f);
        return std::lerp(-maxDelta - std::clamp(currentDelta, -maxDelta, 0.0f), maxDelta - std::clamp(currentDelta, 0.0f, maxDelta), d(gRandomEngine));
    }

    float deltaFactorInfluences(const IHHeightGraphNode& node1, const IHHeightGraphNode& node2, ELandform landform)
    {
        if (landform == ELandform::Tablelands)
        {
            if (node1.ridgeId != node2.ridgeId)
                return 1.0f;
            else
                return 0.0f;
        }

        if (node1.ridgeId != node2.ridgeId)
            return 1.0f;
        else if (node1.ridgeTier != node2.ridgeTier)
            return 0.2f;
        else
            return 0.0f;
    }

    void StageGen<EGenerationStage::Ridges>::calculateRidgelineHeight(std::vector<RidgePeakData>* peakData)
    {
        OmniProfile("Calculate Ridgeline Peak Height");
        std::vector<IHHeightGraphNode> nodes(peakData->size());

        //init
        for (int ihId = 0; ihId < peakData->size(); ihId++)
        {
            nodes[ihId].pos = (*peakData)[ihId].peakPoint;
            nodes[ihId].ridgeId = (*peakData)[ihId].ridgeId;
            nodes[ihId].ridgeTier = (*peakData)[ihId].tier;
            nodes[ihId].rootRidgeId = (*peakData)[ihId].rootRidgeId;

            for (int otherIhId = 0; otherIhId < peakData->size(); otherIhId++)
            {
                if (ihId == otherIhId)
                    continue;

                nodes[ihId].edges[otherIhId] = IHHeightGraphEdge();
            }
        }

        // create graph connections
        for (int node = 0; node < nodes.size(); node++)
        {
            OmniProfile("Create Graph");
            GVector2D position(nodes[node].pos.x(), nodes[node].pos.z());
            auto domain = Generation::Data::get()->getDomainAtSquare(position.toGPoint(), EDomainType::Terrain);

            for (int edgeNode = 0; edgeNode < nodes.size(); edgeNode++)
            {
                if (nodes[node].edges.contains(edgeNode) && !domain->isPointInDomain(nodes[edgeNode].pos))
                {
                    nodes[node].edges.erase(edgeNode);
                    nodes[edgeNode].edges.erase(node);
                }
            }

            for (int edgeNode1 = 0; edgeNode1 < nodes.size(); edgeNode1++)
            {
                for (int edgeNode2 = 0; edgeNode2 < nodes.size(); edgeNode2++)
                {
                    if (edgeNode1 == edgeNode2)
                        continue;

                    if (!nodes[node].edges.contains(edgeNode1) || !nodes[node].edges.contains(edgeNode2))
                        continue;

                    auto edgeNod1ToNodeDir = (nodes[node].pos - nodes[edgeNode1].pos).normalized();
                    auto edgeNod2ToNodeDir = (nodes[node].pos - nodes[edgeNode2].pos).normalized();
                    auto edgeNode1ToEdgeNode2Dir = (nodes[edgeNode2].pos - nodes[edgeNode1].pos).normalized();

                    auto angleEdgeNode1 = angle180(edgeNod1ToNodeDir, edgeNode1ToEdgeNode2Dir);
                    auto angleEdgeNode2 = angle180(edgeNod2ToNodeDir, -edgeNode1ToEdgeNode2Dir);

                    if (angleEdgeNode1 > 90 || angleEdgeNode2 > 90)
                    {
                        auto edgeToRemove = angleEdgeNode1 < 90 ? edgeNode1 : edgeNode2;
                        nodes[node].edges.erase(edgeToRemove);
                        nodes[edgeToRemove].edges.erase(node);
                    }
                }
            }
        }

        // {isCycle, element_it}
        auto findIfStartOfCycle = [](const std::map<int, IHHeightGraphEdge>& nodeEdges, const std::map<int, IHHeightGraphEdge>& neighbourEdges) -> std::tuple<bool, std::map<int, IHHeightGraphEdge>::const_iterator>
        {
            auto cycleElement = std::find_if(nodeEdges.begin(), nodeEdges.end(), [neighbourEdges](auto& kv) { return neighbourEdges.contains(kv.first); });

            if (cycleElement == nodeEdges.end())
                return { false, nodeEdges.end() };

            // return end if cycle element is not first/last in cycle
            auto otherCycleElement = std::find_if(std::next(cycleElement), nodeEdges.end(), [neighbourEdges](auto& kv) { return neighbourEdges.contains(kv.first); });
            return { true, otherCycleElement == nodeEdges.end() ? cycleElement : nodeEdges.end() };
        };

        std::vector<bool> visited(nodes.size());
        std::list<int> queue;

        auto&& ridgeRoots = Generation::Data::get()->getMarkers<DRidgeMarker>();

        // find bfs starting node per domain
        for (auto&& [ignore, domain] : Generation::Data::get()->getAllDomains())
        {
            if (domain->getType() != EDomainType::Terrain)
                continue;

            // Despite the fact that there can be multiple occurrences of a certain number of nodes for each root only a single one is needed
            std::map<int, qint64> ridgeMap;
            auto rootRidgesInDomain = ridgeRoots | std::views::filter([&domain](const auto& ele) {return domain->isPointInDomain(ele->getControlPoints().front()); });
            for (auto&& ridge : rootRidgesInDomain)
            {
                auto&& guid = ridge->getGuid();
                int nodeCount = std::count_if(nodes.begin(), nodes.end(), [&guid](const auto& ele) {return ele.rootRidgeId == guid || ele.ridgeId == guid; });

                ridgeMap.emplace(nodeCount, ridge->getGuid());
            }

            if (ridgeMap.empty())
                continue;

            auto domainStartingPoint = std::find_if(nodes.begin(), nodes.end(), [&ridgeMap](const auto& ele) { return ele.ridgeId == ridgeMap.rbegin()->second; });
            if (domainStartingPoint == nodes.end())
                continue;

            auto&& domainData = domain->getData<EDomainType::Terrain>();

            (*domainStartingPoint).height = Landform::generateRidgeHeight(std::max(domainData->maxHeight * 0.9f, domainData->minHeight), domainData->maxHeight);
            queue.push_back(domainStartingPoint - nodes.begin());
        }

        // bfs
        while (true)
        {
            auto currentNodeId = queue.front();
            auto&& currentNode = nodes[currentNodeId];

            std::vector<QSharedPointer<DRidgeMarker>> ridges;
            Generation::Data::get()->getExactTreeMarkersFlat<DRidgeMarker>(&ridges);

            GVector2D position(currentNode.pos.x(), currentNode.pos.z());
            auto domain = Generation::Data::get()->getDomainAtSquare(position.toGPoint(), EDomainType::Terrain);
            auto&& domainData = domain->getData<EDomainType::Terrain>();
            float minHeight = domainData->minHeight;
            float maxHeight = domainData->maxHeight;
            float currentHeight = currentNode.height;
            int currentTier = currentNode.ridgeTier;
            auto&& tierData = domainData->landformInstanceParams->ridgeMaxTreeLevel;
            float tierHeight = (maxHeight - minHeight) / (((tierData.range.first + tierData.range.second) / 2) + 1);

            float sameRidgeAngle = domainData->ridgeGenParams.ridgelineAngle;
            float slopeAngle = domainData->ridgeGenParams.slopeAngle;

            auto currentRidgeGuid = currentNode.ridgeId;

            auto&& currentRidge = find_if(ridges.begin(), ridges.end(), [&currentRidgeGuid](const auto& ele) { return ele->getGuid() == currentRidgeGuid; });
            Q_ASSERT(currentRidge != ridges.end());

            auto&& parentRidge = (*currentRidge)->getParent();

            for (auto&& [nodeId, edgeData] : currentNode.edges)
            {
                auto delta = 0.0f;

                // Same mountain, but not a continuous ridgeline
                float angle = slopeAngle;

                auto nodeRidgeGuid = nodes[nodeId].ridgeId;

                auto&& nodeRidge = find_if(ridges.begin(), ridges.end(), [&nodeRidgeGuid](const auto& ele) { return ele->getGuid() == nodeRidgeGuid; });
                Q_ASSERT(nodeRidge != ridges.end());

                float connectionTypeFactor = 0.6f;

                // Different mountains
                if (nodes[nodeId].rootRidgeId != currentNode.rootRidgeId)
                {
                    connectionTypeFactor = 0.4f;
                    angle = sameRidgeAngle;
                }
                // Same Ridge
                else if (nodes[nodeId].ridgeId == currentNode.ridgeId)
                {
                    connectionTypeFactor = 0.9f;
                    angle = sameRidgeAngle;
                }
                // Continuous Ridge (parent - child relation)
                else if (!parentRidge.isNull() && !(*nodeRidge)->getParent().isNull())
                {
                    if (parentRidge.lock()->getGuid() == nodeRidgeGuid || (*nodeRidge)->getParent().lock()->getGuid() == currentRidgeGuid)
                    {
                        connectionTypeFactor = 0.75f;
                        angle = sameRidgeAngle;
                    }
                }

                float maxHeightDelta = (std::tanf(qDegreesToRadians(angle)) * distance(currentNode.pos, nodes[nodeId].pos)) * 0.8f;
                float sign = 1.0f;

                if (nodes[nodeId].height != -1)
                {
                    // Check if height difference is within margin, if no alter node height (change strength dependant on connection type and tier difference)
                    auto currentDelta = std::abs(currentHeight - nodes[nodeId].height);
                    if (currentDelta > maxHeightDelta)
                    {
                        if (nodes[nodeId].height > currentHeight)
                            sign = -1.0f;

                        delta = std::clamp(currentDelta - maxHeightDelta, 0.0f, tierHeight * connectionTypeFactor * std::max(1, std::abs(nodes[nodeId].ridgeTier - currentTier)));
                    }
                }
                else if (nodes[nodeId].ridgeTier > currentTier)
                {
                    sign = -1.0f;
                    delta = tierHeight * (nodes[nodeId].ridgeTier - currentTier);
                }
                else if (nodes[nodeId].ridgeTier < currentTier)
                    delta = tierHeight * (currentTier - nodes[nodeId].ridgeTier);

                if (delta > maxHeightDelta)
                    delta = maxHeightDelta;

                // Assign new height and check if it is within height margin
                if (nodes[nodeId].height != -1)
                {
                    if (nodes[nodeId].height += delta * sign >= minHeight)
                        nodes[nodeId].height += delta * sign;
                    else
                        nodes[nodeId].height = minHeight;
                }
                else if (nodes[nodeId].height = currentHeight + (delta * sign) >= minHeight)
                {
                    nodes[nodeId].height = currentHeight + (delta * sign);
                }
                else
                    nodes[nodeId].height = minHeight;
            }

            // bfs logic
            for (auto&& [node, edgeData] : currentNode.edges)
            {
                if (!visited[node])
                {
                    visited[node] = true;
                    queue.push_back(node);
                }
            }

            queue.pop_front();
            if (queue.empty())
                break;
        }

#if DEBUG_PEAK_GRAPH
        for (auto&& node : nodes)
        {
            for (auto&& [other_node_id, ignore] : node.edges)
            {
                QVector3D firstNode(node.pos.x() , node.height + 200, node.pos.z());
                QVector3D secondNode(nodes[other_node_id].pos.x(), nodes[other_node_id].height + 200, nodes[other_node_id].pos.z());

                Generation::Data::get()->createMarker<DLineMarker>(std::vector<QVector3D>{firstNode, secondNode}, QVector4D(0, 0, 1, 1), false);

                QVector4D color;

                switch (node.ridgeTier)
                {
                case 0: color = QVector4D(1, 0, 0, 1); break;
                case 1: color = QVector4D(1, 1, 0, 1); break;
                case 2: color = QVector4D(0, 1, 0, 1); break;
                case 3: color = QVector4D(0, 1, 1, 1); break;
                case 4: color = QVector4D(0, 0, 1, 1); break;
                case 5: color = QVector4D(1, 0, 1, 1); break;
                case 6: color = QVector4D(1, 1, 1, 1); break;
                case 7: color = QVector4D(0, 0, 0, 1); break;
                default:
                    break;
                }

                QVector3D newPos(node.pos.x(), node.height, node.pos.z());
                spawn<DLineMarker>(newPos, 1000.0f * (8 - node.ridgeTier), color);
            }
        }
#endif

        for (int i = 0; i < nodes.size(); ++i)
            (*peakData)[i].nodeHeight = nodes[i].height;
    }

    std::vector<QSharedPointer<DRidgeMarker>> StageGen<EGenerationStage::Ridges>::assignHeightToRidge(QSharedPointer<DRidgeMarker> ridge, const std::vector<RidgePeakData>& peakData)
    {
        auto&& cPts = ridge->getControlPoints();
        auto&& guid = ridge->getGuid();
        std::vector<float> heights(cPts.size(), -1.0f);

        auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(cPts.front().x(), cPts.front().z()).toGPoint(), EDomainType::Terrain);
        auto&& localMinimum = domain->getData<EDomainType::Terrain>()->minHeight;

        // Angle Computations for the special case (which might be obsolete, needs extensive testing)
        float angle;

        if (ridge->getParent().isNull())
        {
            auto&& mainRidgeAngleInfo = domain->getData<EDomainType::Terrain>()->landformInstanceParams->slopeAngleSameRidgesLevel0;
            std::uniform_int_distribution<> slopeChance(0, 100);
            auto factor = slopeChance(Generation::gRandomEngine) / 100.0f;
            angle = mainRidgeAngleInfo.getRandomValue();
        }
        else
        {
            auto&& angleInfo = domain->getData<EDomainType::Terrain>()->landformInstanceParams->ridgelineSlopeAngle;
            angle = domain->getData<EDomainType::Terrain>()->ridgeGenParams.ridgelineAngle;
        }

        auto&& ridgeNodes = peakData | std::views::filter([&guid](const auto& ele) {return ele.ridgeId == guid; });

        std::unordered_set<QVector3D> peaks;
        std::vector<int> peakIndices;

        for (auto&& node : ridgeNodes)
        {
            GVector2D peakPoint(node.peakPoint.x(), node.peakPoint.z());
            auto&& peak = std::find_if(cPts.begin(), cPts.end(), [&peakPoint](const auto& ele) { return peakPoint == GVector2D(ele.x(), ele.z()); });
            Q_ASSERT(peak != cPts.end());
            float height = node.nodeHeight;

            // Add additional height to tableland ridges according to their desired terrace height
            // This cannot be done earlier (on the graph stage) since due to the rather low slope angle, the graph will try to achieve 
            // homogeneous heights, while different types can have vast differences in height, which is desired
            if (domain->getData<EDomainType::Terrain>()->landform == ELandform::Tablelands)
            {
                auto landformVariation = domain->getData<EDomainType::Terrain>()->landformVariation;
                float tablelandHeight = 200.0f * PTablelandTypes[landformVariation][*ridge->getTablelandType()].desiredPrecipiceSteps;
                height += tablelandHeight;
            }

            peaks.emplace(peakPoint.x, height, peakPoint.z);
        }

        // Make it so all tablelands ridges are flat
        if (domain->getData<EDomainType::Terrain>()->landform == ELandform::Tablelands)
        {
            float height = 0.0f;
            if (peaks.size() == 0)
                height = ridge->getParent().lock()->getHeights()[0];
            else
                height = (*peaks.begin()).y();

            for (int i = 0; i < cPts.size(); ++i)
            {
                heights[i] = height;
            }

            ridge->setHeights(heights);
            ridge->showAs3D();

            return ridge->getChildren();
        }

        auto&& children = ridge->getChildren();
        for(auto&& child : children)
            if (auto&& childHeight = child->getHeights(); !childHeight.empty())
            {
                peakIndices.emplace_back(child->getSourcePointIdx());
                heights[child->getSourcePointIdx()] = childHeight[0];
            }

        for (int i = 0; i < cPts.size(); ++i)
        {
            GVector2D point(cPts[i].x(), cPts[i].z());

            if (i == 0)
            {
                if (auto&& parent = ridge->getParent(); parent)
                {
                    if (auto&& parentHeights = parent.lock()->getHeights(); !parentHeights.empty())
                    {
                        heights[i] = parentHeights[ridge->getSourcePointIdx()];
                        peakIndices.emplace_back(i);
                        continue;
                    }
                    // Check for sibling branches, and if they have assigned height
                    else
                    {
                        auto&& parentChildren = parent.lock()->getChildren();
                        auto&& siblingRidge = std::find_if(parentChildren.begin(), parentChildren.end(), [ridge](const auto& ele)
                            {return ele->getSourcePointIdx() == ridge->getSourcePointIdx() && ele->getGuid() != ridge->getGuid(); });

                        if (siblingRidge != parentChildren.end())
                        {
                            Q_ASSERT((*siblingRidge)->getGuid() != ridge->getGuid());
                            if (auto&& siblingHeights = (*siblingRidge)->getHeights(); !siblingHeights.empty())
                            {
                                heights[i] = siblingHeights[0];
                                peakIndices.emplace_back(i);
                                continue;
                            }
                        }
                    }
                }
            }

            auto&& peak = std::find_if(peaks.begin(), peaks.end(), [point](const auto& ele) { return point == GVector2D(ele.x(), ele.z()); });

            if (peak != peaks.end())
            {
                heights[i] = peak->y();
                peakIndices.emplace_back(i);
            }
        }

        // This is a unique case where the main ridge has no peaks but its children do
        if (peakIndices.empty())
        {
            // Take average height of children peaks
            auto&& children = ridge->getChildren();
            std::vector<float> peakHeights;
            for (auto&& child : children)
            {
                auto&& childGuid = child->getGuid();
                auto&& ridgeNodes = peakData | std::views::filter([&childGuid](const auto& ele) {return ele.ridgeId == childGuid; });
                for (auto&& node : ridgeNodes)
                    peakHeights.emplace_back(node.nodeHeight);
            }

            // Calculate max height of new main ridge peak
            float averageHeight = std::accumulate(peakHeights.begin(), peakHeights.end(), 0.0f) / peakHeights.size();
            float distance = GVector2D(cPts[0].x(), cPts[0].z()).dist(GVector2D(cPts[cPts.size() - 1].x(), cPts[cPts.size() - 1].z())) / 2;
            float heightDelta = std::tanf(qDegreesToRadians(angle)) * distance;

            // Add new peak to allow normal ridgeline height computations
            int newPeakIdx = cPts.size() / 2;
            heights[newPeakIdx] = averageHeight + heightDelta;
            peakIndices.emplace_back(newPeakIdx);
        }

        std::sort(peakIndices.begin(), peakIndices.end());

        QSharedPointer<DRidgeMarker> parent = nullptr;
        if(ridge->getParent())
            parent = ridge->getParent().lock();

        computeRidgeHeight(&heights, cPts, peakIndices, parent);

        ridge->setHeights(heights);
        ridge->showAs3D();

        return ridge->getChildren();
    }

    void Generation::StageGen<EGenerationStage::Ridges>::computeRidgeHeight(std::vector<float>* heights, const std::vector<QVector3D>& cPts, const std::vector<int>& sortedPeakIndices, QSharedPointer<DRidgeMarker> parentRidge)
    {
        auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(cPts.front().x(), cPts.front().z()).toGPoint(), EDomainType::Terrain);
        auto&& localMinimum = domain->getData<EDomainType::Terrain>()->minHeight;
        float angle, curvature;

        if (parentRidge.isNull())
        {
            auto&& mainRidgeAngleInfo = domain->getData<EDomainType::Terrain>()->landformInstanceParams->slopeAngleSameRidgesLevel0;
            std::uniform_int_distribution<> slopeChance(0, 100);
            auto factor = slopeChance(Generation::gRandomEngine) / 100.0f;
            angle = mainRidgeAngleInfo.getRandomValue();
            curvature = mainRidgeAngleInfo.flatness;
        }
        else
        {
            auto&& angleInfo = domain->getData<EDomainType::Terrain>()->landformInstanceParams->ridgelineSlopeAngle;
            angle = domain->getData<EDomainType::Terrain>()->ridgeGenParams.ridgelineAngle;
            curvature = angleInfo.flatness;
        }

        auto singlePeak = [&](int peakIdx, int passIdx)
        {
            GVector2D peak(cPts[peakIdx].x(), cPts[peakIdx].z());
            float peakHeight = (*heights)[peakIdx];
            float distance = peak.dist(GVector2D(cPts[passIdx].x(), cPts[passIdx].z()));
            float heightDelta = std::tanf(qDegreesToRadians(angle)) * distance;
            float ridgeMinHeight = peakHeight - heightDelta >= localMinimum ? peakHeight - heightDelta : localMinimum;
            (*heights)[passIdx] = ridgeMinHeight;

            if (ridgeMinHeight != peakHeight - heightDelta)
            {
                float maxDelta = peakHeight - localMinimum;
                angle = 90.0f - qRadiansToDegrees(std::atan(distance / maxDelta));
            }

            int firstIdx = peakIdx < passIdx ? peakIdx : passIdx;
            int secondIdx = peakIdx < passIdx ? passIdx : peakIdx;

            // Curve
            auto curveGen = hybrid_int_distribution<int>(0, 100, 0.4, curvature);
            float factor = curveGen(Generation::gRandomEngine) / 100.0f;
            float firstCoord = std::lerp(cPts[peakIdx < passIdx ? firstIdx : secondIdx].x(), cPts[peakIdx < passIdx ? secondIdx : firstIdx].x(), factor);
            float secondCoord = std::lerp(ridgeMinHeight, peakHeight, factor);
            std::vector<GVector2D> curveControlPoints;
            curveControlPoints.emplace_back(GVector2D(cPts[firstIdx].x(), peakIdx < passIdx ? peakHeight : ridgeMinHeight));
            curveControlPoints.emplace_back(GVector2D(firstCoord, secondCoord));
            std::uniform_int_distribution<> secondMidPointChance(0, 1);

            if (secondMidPointChance(Generation::gRandomEngine))
            {
                std::uniform_int_distribution<> rnd(0, 100);
                float xFactor = rnd(Generation::gRandomEngine) / 100.0f;
                float zFactor = rnd(Generation::gRandomEngine) / 100.0f;
                float x = std::lerp(cPts[peakIdx < passIdx ? firstIdx : secondIdx].x(), cPts[peakIdx < passIdx ? secondIdx : firstIdx].x(), xFactor);
                float z = std::lerp(ridgeMinHeight, peakHeight, zFactor);
                curveControlPoints.emplace_back(GVector2D(x, z));
            }

            curveControlPoints.emplace_back(GVector2D(cPts[secondIdx].x(), peakIdx < passIdx ? ridgeMinHeight : peakHeight));
            BezierCurve2D curve(curveControlPoints);
            auto pts = curve.getPoints(secondIdx - firstIdx);

            for (int idx = 1; idx < pts.size(); ++idx)
            {
                if (firstIdx + idx == peakIdx)
                    continue;

                int debugIdx = firstIdx + idx;
                (*heights)[debugIdx] = pts[idx].z;
            }

            Q_ASSERT((*heights)[peakIdx] == peakHeight && (*heights)[passIdx] == ridgeMinHeight);
        };

        auto twoPeaks = [&](int firstPeakIdx, int secondPeakIdx)
        {
            GVector2D firstPeak(cPts[firstPeakIdx].x(), cPts[firstPeakIdx].z());
            float firstPeakHeight = (*heights)[firstPeakIdx];
            GVector2D secondPeak(cPts[secondPeakIdx].x(), cPts[secondPeakIdx].z());
            float secondPeakHeight = (*heights)[secondPeakIdx];

            if (secondPeakIdx - firstPeakIdx <= 1)
                return;

            float highestPeakHeight = std::max(firstPeakHeight, secondPeakHeight);
            float distance = firstPeak.dist(secondPeak) / 2;
            float heightDelta = std::tanf(qDegreesToRadians(angle)) * distance;
            float ridgeMinHeight = highestPeakHeight - heightDelta >= localMinimum ? highestPeakHeight - heightDelta : localMinimum;
            int passIdx = (secondPeakIdx + firstPeakIdx) / 2;

            (*heights)[passIdx] = ridgeMinHeight;

            Q_ASSERT(firstPeakIdx < secondPeakIdx);
            Q_ASSERT(passIdx < secondPeakIdx);
            Q_ASSERT(firstPeakIdx < passIdx);

            // First curve
            auto curveGen = hybrid_int_distribution<int>(0, 100, 0.4, curvature);
            float factor = curveGen(Generation::gRandomEngine) / 100.0f;
            float firstCoord = std::lerp(cPts[firstPeakIdx].x(), cPts[passIdx].x(), factor);
            float secondCoord = std::lerp(ridgeMinHeight, firstPeakHeight, factor);
            GVector2D middlePoint1(firstCoord, secondCoord);

            firstCoord = std::lerp(cPts[secondPeakIdx].x(), cPts[passIdx].x(), factor);
            secondCoord = std::lerp(ridgeMinHeight, secondPeakHeight, factor);
            GVector2D middlePoint2(firstCoord, secondCoord);

            std::vector<GVector2D> curvePoints1;
            curvePoints1.emplace_back(GVector2D(cPts[firstPeakIdx].x(), firstPeakHeight));
            curvePoints1.emplace_back(middlePoint1);

            std::uniform_int_distribution<> secondMidPointChance(0, 1);
            if (secondMidPointChance(Generation::gRandomEngine))
            {
                std::uniform_int_distribution<> rnd(0, 100);
                float xFactor = rnd(Generation::gRandomEngine) / 100.0f;
                float zFactor = rnd(Generation::gRandomEngine) / 100.0f;
                float x = std::lerp(cPts[firstPeakIdx].x(), cPts[passIdx].x(), xFactor);
                float z = std::lerp(ridgeMinHeight, firstPeakHeight, zFactor);
                curvePoints1.emplace_back(GVector2D(x, z));
            }

            curvePoints1.emplace_back(GVector2D(cPts[passIdx].x(), ridgeMinHeight));
            BezierCurve2D curve1(curvePoints1);

            // Second curve
            std::vector<GVector2D> curvePoints2;
            curvePoints2.emplace_back(GVector2D(cPts[passIdx].x(), ridgeMinHeight));
            curvePoints2.emplace_back(middlePoint2);

            if (secondMidPointChance(Generation::gRandomEngine))
            {
                std::uniform_int_distribution<> rnd(0, 100);
                float xFactor = rnd(Generation::gRandomEngine) / 100.0f;
                float zFactor = rnd(Generation::gRandomEngine) / 100.0f;
                float x = std::lerp(cPts[secondPeakIdx].x(), cPts[passIdx].x(), xFactor);
                float z = std::lerp(ridgeMinHeight, secondPeakHeight, zFactor);
                curvePoints2.emplace_back(GVector2D(x, z));
            }

            curvePoints2.emplace_back(GVector2D(cPts[secondPeakIdx].x(), secondPeakHeight));
            BezierCurve2D curve2(curvePoints2);

            auto pts1 = curve1.getPoints(passIdx - firstPeakIdx);
            auto pts2 = curve2.getPoints(secondPeakIdx - passIdx);

            for (int idx = firstPeakIdx + 1; idx < secondPeakIdx; ++idx)
            {
                if (idx == passIdx)
                    continue;

                if (idx < passIdx)
                    (*heights)[idx] = pts1[idx - firstPeakIdx].z;
                else
                    (*heights)[idx] = pts2[idx - passIdx].z;
            }

            Q_ASSERT((*heights)[firstPeakIdx] == firstPeakHeight && (*heights)[secondPeakIdx] == secondPeakHeight && (*heights)[passIdx] == ridgeMinHeight);
        };

        // All other subridges
        if (sortedPeakIndices.front() != 0)
            singlePeak(sortedPeakIndices.front(), 0);

        if (sortedPeakIndices.back() != cPts.size() - 1)
            singlePeak(sortedPeakIndices.back(), cPts.size() - 1);

        if (sortedPeakIndices.size() > 1)
            for (int i = 0; i < sortedPeakIndices.size() - 1; ++i)
                twoPeaks(sortedPeakIndices[i], sortedPeakIndices[i + 1]);
    }
}