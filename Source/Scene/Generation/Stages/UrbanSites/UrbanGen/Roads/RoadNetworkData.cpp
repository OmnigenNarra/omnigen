#include "stdafx.h"
#include "RoadNetworkData.h"

#include "Scene/Generation/OmnigenGenerationData.h"

RoadNetworkData::RoadNetworkData(const bool complexTrace)
    : needsComplexTrace(complexTrace)
{
    roadGraphHighLevel = AdjacencyGraph(false);
    roadGraphLowLevel = AdjacencyGraph(false);
}

void RoadNetworkData::computeRoadGraphHigh()
{
    for (auto&& seg : baseEdges)
    {
        if (seg.correspondingRoad)
        {
            std::vector<QVector3D> filteredPts;

            filteredPts.push_back(seg.edge.first);

            for (auto&& pt : seg.correspondingRoad->getControlPoints())
                if (seg.edge.hasPoint(GVector2D(pt)))
                    filteredPts << GVector2D(pt);

            filteredPts.push_back(seg.edge.second);

            if (distance(filteredPts[0], filteredPts[1]) > distance(filteredPts[1], filteredPts.back()))
            {
                filteredPts[0] = seg.edge.second;
                filteredPts.back() = seg.edge.first;
            }

            insertToGraph(filteredPts, &roadGraphLowLevel);

            //spawn<DLineMarker>(filteredPts, Colors::red, false, 7.000f);
        }
        else
        {
            insertToGraph({ seg.edge.first, seg.edge.second }, &roadGraphHighLevel);
        }
    }

    for (auto&& node : roadGraphHighLevel.getNodes())
    {
        for (auto&& n : node->neighbors)
        {
            if (contains(roadTargets, Segment2D(node->data, n->data)) ||
                contains(roadTargets, Segment2D(n->data, node->data)))
                continue;

            roadTargets.push_back(Segment2D(node->data, n->data));
        }
    }

   /* for (auto&& seg : roadTargets)
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ seg.first, seg.second }, Colors::red, false, 2'000.f);*/
}

void RoadNetworkData::computeRoadGraphLow()
{
    if (!bounds.getPts().empty())
        insertToGraph(std::vector<QVector3D>(bounds.getPts().begin(), bounds.getPts().end()), &roadGraphLowLevel);

    for (auto&& marker : roads)
    {
        insertToGraph(marker->getControlPoints(), &roadGraphLowLevel);
    }
}

std::vector<Polygon2D> RoadNetworkData::getEnclosingRegions() const
{
    const auto data = UrbanUtils::getDataFromAdjacencyGraph(roadGraphLowLevel);

    const MCBComputer mcb(data);
    return mcb.getLots();
}

void RoadNetworkData::insertToGraph(const std::vector<QVector3D>& pts, AdjacencyGraph* graph)
{
    for (auto i = 0; i < pts.size() - 1; i++)
    {
        auto&& pt1 = pts[i];
        auto&& pt2 = pts[i + 1];

        const auto r1 = graph->addNode(pt1);
        const auto r2 = graph->addNode(pt2);

        if (r1 == r2)
            continue;

        graph->addEdge(r1, r2);
    }
}