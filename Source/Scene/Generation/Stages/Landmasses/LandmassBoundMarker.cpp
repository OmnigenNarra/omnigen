#include "stdafx.h"
#include "LandmassBoundMarker.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Omnigen.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"
#include "Scene/Generation/Stages/Layout/StageGeneration_Layout.h"

DLandmassBound::DLandmassBound(const std::vector<QVector3D>& inControlPoints)
    : DIsohypseBound(inControlPoints)
{
}

bool DLandmassBound::generateAll(bool optimize)
{
    auto terrainDomains = Generation::Data::get()->getAllDomains<EDomainType::Terrain>();
    std::vector<Segment2D> workUnitEdges;
    QSet<Segment2D> ddStubEdges;
    std::vector<std::vector<Segment2D>> boundEdges;

    // Gather "island" area information or take Terrain Domains if no Water used
    auto shorelineMarkers = Generation::Data::get()->getMarkers<DShorelineMarker>();

    if (!shorelineMarkers.empty())
    {
        auto landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();

        for (auto&& landmass : landmasses)
        {
            QSet<GPoint> shorelineArea;
            for (auto&& shoreline : landmass->getShorelines())
                shorelineArea += shoreline->getSquares();
            for (auto&& shoreline : landmass->getInnerSeaShorelines())
                shorelineArea += shoreline->getSquares();

            auto localPerimeters = computePerimeterForSquares(landmass->getSquares() + shorelineArea);
            boundEdges.push_back(std::get<std::vector<Segment2D>>(localPerimeters));
        }
    }
    else
    {
        // Gather unit edges
        for (auto&& [hnd, td] : terrainDomains)
        {
            auto outsideEdges = td->getPerimeter();

            for (auto&& [a, b] : outsideEdges)
            {
                GVector2D unit = GVector2D((a.x == b.x) ? 0 : GRID_SEGMENT_WIDTH, (a.z == b.z) ? 0 : GRID_SEGMENT_WIDTH);
                for (GVector2D head = a; head < b; head = head + unit)
                {
                    Segment2D s{ head, head + unit };
                    workUnitEdges << s;
                }
            }
        }

        for (auto&& [hnd, td] : terrainDomains)
        {
            // Remove all edges shared with other Terrain domains.
            for (auto&& [handle, domain] : terrainDomains)
            {
                if (domain == td)
                    continue;

                auto sharedEdges = Generation::Utils::computeSharedPerimeter(td, domain);
                for (auto&& edge : sharedEdges)
                    workUnitEdges = removeAll(workUnitEdges, edge);
            }
        }

        boundEdges.push_back(workUnitEdges);
    }

    for (auto unitEdges : boundEdges)
    {
        std::vector<Segment2D> finalEdges;

        if (optimize)
        {
            // Rejoin edges
            int offset = 1;
            for (int idx = 0; idx < unitEdges.size(); idx += offset)
            {
                // New edge
                Segment2D joinedEdge = unitEdges[idx];
                GVector2D unit = joinedEdge.second - joinedEdge.first;

                // Append the most following edges possible.
                for (offset = 1; (idx + offset < unitEdges.size()); ++offset)
                {
                    auto nextUnit = unitEdges[idx + offset].second - unitEdges[idx + offset].first;
                    bool isNext = (unitEdges[idx + offset].first == joinedEdge.second);
                    if (isNext && nextUnit == unit)
                        joinedEdge.second = unitEdges[idx + offset].second;
                    else
                        break;
                }

                finalEdges << joinedEdge;
            }
        }
        else
        {
            finalEdges = unitEdges;
        }

        if (finalEdges.empty())
            return true;

        // Create control points
        //Take longest segment as first element (guarantees it's an outer edge segment)
        Segment2D longestSeg;
        int longestSegLength = 0;
        for (auto&& segment : finalEdges)
        {
            int segLength = 0;
            if (segment.first.x == segment.second.x)
                segLength = segment.first.z > segment.second.z ? segment.first.z - segment.second.z : segment.second.z - segment.first.z;
            else
                segLength = segment.first.x > segment.second.x ? segment.first.x - segment.second.x : segment.second.x - segment.first.x;

            if (segLength > longestSegLength)
            {
                longestSegLength = segLength;
                longestSeg = segment;
            }
        }

        std::vector<QVector3D> controlPoints = { longestSeg.first, longestSeg.second };
        removeOne(finalEdges, longestSeg);

        while (finalEdges.size() > 1)
        {
            int i = 0;
            for (auto&& edge : finalEdges)
            {
                if (edge.first == GVector2D(controlPoints.back()))
                {
                    controlPoints << edge.second;
                    removeOne(finalEdges, edge);
                    i = 0;
                    break;
                }
                else if (edge.second == GVector2D(controlPoints.back()))
                {
                    controlPoints << edge.first;
                    removeOne(finalEdges, edge);
                    i = 0;
                    break;
                }
                i++;
            }
            // Delete leftover segments (inner sea bounds)
            if (i >= finalEdges.size())
            {
                finalEdges.clear();
                break;
            }
        }

        Generation::Data::get()->createMarker<DLandmassBound>(controlPoints);
    }

    return true;
}

QSet<GPoint> DLandmassBound::getContinentInside(const GPoint& pureTerrainSquare)
{
    // Get all terrain squares
    QSet<GPoint> allTerrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();

    // Subtract all water squares
    allTerrainSquares -= Generation::Data::get()->getAllSquares<EDomainType::Water>();

    // Partition pure terrain squares
    auto partitionedTerrain = Omnigen::get()->partitionSquares(allTerrainSquares);

    for (auto&& inside : partitionedTerrain)
        if (inside.contains(pureTerrainSquare))
            return inside;

    return {};
}
