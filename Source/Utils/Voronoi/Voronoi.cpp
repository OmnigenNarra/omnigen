#include "stdafx.h"
#include "Voronoi.h"

#include "../Polygon.h"
#include "Omnigen.h"

#include "VoronoiCore.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/Sections/Profiler/OmnigenProfilerSection.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Utils/Clipper/clipper.hpp"

namespace Voronoi
{
    void Diagram::drawCells(const QVector4D& withColour, const float withHeight /* = 0 */) const
    {
        OmniProfile("Drawing cells");
        for (auto&& cell : cells)
        {
            cell.getPolygon().debugPlot(withColour, withHeight > 1.f ? withHeight : 20);
        }
    }

    void Diagram::drawCenters(const QVector4D& withColour, const float withHeight /* = 0 */) const
    {
        std::vector<QVector3D> points;

        for (const auto& center : centers)
        {
            QVector3D vertex3D = QVector3D(
                (center.x),
                withHeight > 1.f ? withHeight : 30,
                (center.z)
            );
            points << vertex3D;
        }

        Generation::Data::get()->createMarker<DPointCloudMarker>(points, withColour);
    }

    void Diagram::drawCellControlPoints(const QVector4D& withColour, const float withHeight /* = 0 */) const
    {
        if (cells.empty())
            return;

        std::vector<QVector3D> points;
        for (auto& cell : cells) 
        {
            for (const auto& point : cell)
            {
                QVector3D vertex3D = QVector3D(
                    (point.x),
                    withHeight > 1.f ? withHeight : 40,
                    (point.z)
                );
                points << vertex3D;
            }
        }
        
        Generation::Data::get()->createMarker<DPointCloudMarker>(points, withColour);
    }

    void Diagram::printCellsToLog() const
    {
        if (cells.empty())
            return;

        int helper = 0;
        for (const auto& cell : cells)
        {
            OmniLog(ELoggingLevel::Info) << "Cell " <<= helper;
            int innerHelper = 0;
            for (auto& pt : cell)
            {
                OmniLog(ELoggingLevel::Info) << "   Point " << innerHelper << ": " << pt.x << ", " <<= pt.z;
                innerHelper++;
            }

            helper++;
        }
    }

    int Diagram::findSuitableNeighbourCellForPolygonMerging(const Polygon2D& polygonInCell, const GVoronoiCell& cell, const QHash<int, std::vector<Polygon2D>>& cellsClippedPolygons) const
    {
        for (auto& n_i : cell.getNeighbors().keys())
        {
            auto&& neighbourCell = cells[n_i];
            auto&& neighbourPolygons = cellsClippedPolygons[n_i];

            if (neighbourPolygons.size() == 1)
            {
                for (auto& pt : neighbourCell)
                {
                    if (polygonInCell.findPointOnIndexedEdge(pt).has_value())
                    {
                        return n_i;
                    }
                }
            }
        }

        return -1;
    };

    QHash<int, std::optional<Polygon2D>> Diagram::clipDiagramToPolygon(const Polygon2D& clippingPolygon) const
    {
        QHash<int, std::vector<Polygon2D>> cellsClippedPolygons;

        for (auto i = 0; i < cells.size(); i++)
        {
            auto&& cell = cells[i];
            ClipperLib::Clipper newClipper{};

            std::vector<ClipperLib::IntPoint> APts;
            for (auto&& pt : cell.getPolygon())
                APts.emplace_back(pt.x, pt.z);

            std::vector<ClipperLib::IntPoint> areaPts;
            for (auto&& pt : clippingPolygon)
            {
                areaPts.emplace_back(pt.x, pt.z);
            }

            //TODO: Clipping gets weird if there is a hole in the site OR the resulting polygon!!
            newClipper.AddPath(APts, ClipperLib::PolyType::ptSubject, true);
            newClipper.AddPath(areaPts, ClipperLib::PolyType::ptClip, true);

            ClipperLib::Paths clippedPathsInCells;
            newClipper.Execute(ClipperLib::ClipType::ctIntersection, clippedPathsInCells, ClipperLib::pftNonZero, ClipperLib::pftNonZero);


            std::vector<Polygon2D> polygons;

            for (auto&& path : clippedPathsInCells)
            {
                std::vector<GVector2D> newPts;

                for (auto&& pt : path)
                    newPts.emplace_back(pt.X, pt.Y);

                polygons.push_back(Polygon2D(newPts));
            }

            cellsClippedPolygons.insert(i, polygons);
        }

        QHash<int, std::vector<Polygon2D>> cellsPolygonsToMerge;

        for (auto i = 0; i < cells.size(); i++)
        {
            auto&& cell = cells[i];
            auto&& polygons = cellsClippedPolygons[i];

            if (polygons.size() <= 1)
            {
                cellsPolygonsToMerge[i].insert(cellsPolygonsToMerge[i].end(), polygons.begin(), polygons.end());
            }
            else
            {
                auto&& biggestPolygon = std::max_element(polygons.begin(), polygons.end(), [](const Polygon2D& lhs, const Polygon2D& rhs) {return lhs.getArea() < rhs.getArea(); });
                cellsPolygonsToMerge[i].push_back(*biggestPolygon);

                for (auto it = polygons.begin(); it != polygons.end(); it++)
                {
                    if (it == biggestPolygon)
                        continue;

                    auto&& polygon = *it;
                    auto&& neighbourId = findSuitableNeighbourCellForPolygonMerging(polygon, cell, cellsClippedPolygons);

                    if (neighbourId != -1)
                    {
                        cellsPolygonsToMerge[neighbourId].push_back(polygon);
                    }
                }
            }
        }

        QHash<int, std::optional<Polygon2D>> cellsMergedPolygons;

        for (auto i = 0; i < cells.size(); i++)
        {
            auto&& polygonsToMerge = cellsPolygonsToMerge[i];

            if (polygonsToMerge.empty())
            {
                cellsMergedPolygons.insert(i, {});
            }
            else if (polygonsToMerge.size() == 1)
            {
                cellsMergedPolygons.insert(i, polygonsToMerge.front());
            }
            else
            {
                auto&& mainPolygon = polygonsToMerge.front();

                for (auto it = std::next(polygonsToMerge.begin()); it != polygonsToMerge.end(); it++)
                {
                    auto&& polygon = *it;

                    ClipperLib::Clipper newClipper{};

                    std::vector<ClipperLib::IntPoint> mainPolygonPoints;
                    for (auto&& pt : mainPolygon)
                        mainPolygonPoints.emplace_back(pt.x, pt.z);

                    std::vector<ClipperLib::IntPoint> polygonPoints;
                    for (auto&& pt : polygon)
                        polygonPoints.emplace_back(pt.x, pt.z);


                    newClipper.AddPath(mainPolygonPoints, ClipperLib::PolyType::ptSubject, true);
                    newClipper.AddPath(polygonPoints, ClipperLib::PolyType::ptClip, true);

                    ClipperLib::Paths mergedPolygonPaths;
                    newClipper.Execute(ClipperLib::ClipType::ctUnion, mergedPolygonPaths, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

                    std::vector<GVector2D> newPts;
                    for (auto&& pt : mergedPolygonPaths.front())
                        newPts.emplace_back(pt.X, pt.Y);

                    mainPolygon = Polygon2D(newPts);
                }

                cellsMergedPolygons.insert(i, mainPolygon);
            }
        }

        return cellsMergedPolygons;
    }

    void Diagram::calculateDiagram(const VoronoiCore& inDiagram)
    {
        OmniProfile("Voronoi Calculate Diagram");

        auto checkPoly = [](const Polygon2D& inPoly) -> bool
        {
            for (auto& p : inPoly)
                if ((p.x < 0 || p.x > getMaxGridCoord()) || (p.z < 0 || p.z > getMaxGridCoord()))
                    return false;

            return true;
        };

        for (auto& [pt, poly, isBounded, neighbors] : inDiagram.constructData())
        {
            Q_ASSERT(checkPoly(poly));

            //TODO: Make this more precise
            GVoronoiCell cell = GVoronoiCell(pt);
            cell.cellPolygon = poly;
            cell.isBounded = isBounded;

            for (auto it = neighbors.keyValueBegin(); it != neighbors.keyValueEnd(); ++it)
            {
                const auto [cellIdx, pts] = *it;

                const int idx1 = indexOf(poly.getPts(), pts[0]);
                const int idx2 = indexOf(poly.getPts(), pts[1]);
                Q_ASSERT(idx1 == lastIndexOf(poly.getPts(), pts[0]));
                Q_ASSERT(idx2 == lastIndexOf(poly.getPts(), pts[1]));
                Q_ASSERT(idx1 != -1 && idx2 != -1);

                if (idx1 != idx2)
                    cell.neighborsMap.insert(cellIdx, { idx1, idx2 });
                else
                    cell.pointNeighbors.insert(cellIdx, idx1);
                    
            }

            cells.push_back(cell);
        }
    }

    void Diagram::calculateDetailedNeighborEdges(const VoronoiCore& inDiagram)
    {
        OmniProfile("Voronoi Detailed Neighbors");

        for (int i = 0; i < cells.size(); i++)
        {
            auto&& cell = cells[i];
            const auto& cellPoly = *cell;
            auto cPts = cellPoly.getCPts();
            for (auto it = cell.neighborsMap.keyValueBegin(); it != cell.neighborsMap.keyValueEnd(); ++it)
            {
                auto [center, pts] = *it;
                auto& neighborPoly = *cells[center];

                Segment2D edgesSeg = Segment2D(cPts[pts[0]], cPts[pts[1]]);

                auto neighborCPts = neighborPoly.getCPts();
                for (int i = 0; i < neighborCPts.getSize(); i++)
                {
                    int i2 = neighborCPts.findIdx(i, 1);
                    const Segment2D testSegment = Segment2D(neighborCPts[i], neighborCPts[i2]);

                    if (edgesSeg.hasPoint(testSegment.first) && edgesSeg.hasPoint(testSegment.second))
                    {
                        if (qAbs(edgesSeg.length() - testSegment.length()) < 2.f)
                            continue;

                        if (edgesSeg.length() > testSegment.length())
                        {
                            if (edgesSeg.first == testSegment.first)
                            {
                                if (!contains(cell->getPts(), testSegment.second))
                                {
                                    cell->addPoint(pts[1], testSegment.second, true);
                                    pts[1] = indexOf(cellPoly.getPts(), testSegment.second);
                                }
                            }
                            else
                            {
                                if (!contains(cell->getPts(), testSegment.first))
                                {
                                    cell->addPoint(pts[1], testSegment.first, true);
                                    pts[1] = indexOf(cellPoly.getPts(), testSegment.first);
                                }
                            }
                        }
                    }
                    else if (testSegment.hasPoint(edgesSeg.first) && testSegment.hasPoint(edgesSeg.second))
                    {
                        if (qAbs(edgesSeg.length() - testSegment.length()) < 2.f)
                            continue;

                        if (edgesSeg.length() < testSegment.length())
                        {
                            if (testSegment.first == edgesSeg.first)
                            {
                                if (!contains(cells[center]->getPts(), edgesSeg.second))
                                {
                                    cells[center]->addPoint(1, edgesSeg.second, true);
                                }
                            }
                            else
                            {
                                if (!contains(cells[center]->getPts(), edgesSeg.first))
                                {
                                    cells[center]->addPoint(1, edgesSeg.first, true);
                                }
                            }
                        }
                    }
                }

                for (auto& pt : neighborPoly)
                {
                    for (auto& cellPt : cellPoly)
                    {
                        if (cellPt == pt)
                            continue;

                        if (qAbs(cellPt.x - pt.x) < 2 && qAbs(cellPt.z - pt.z) < 2)
                        {
                            const_cast<GVector2D&>(cellPt) = GVector2D(std::min(cellPt.x, pt.x), std::min(cellPt.z, pt.z));
                            const_cast<GVector2D&>(pt) = cellPt;
                        }
                    }
                }
            }
        }
    }

    BoxDiagram::BoxDiagram(const std::vector<GVector2D>& inCenters, const BoundingBox& bb)
        : Diagram(), bounds(bb)
    {
        Q_ASSERT_X(!inCenters.empty(), "BoxDiagram()", "Provided centers array is empty");

        centers = inCenters;
        
        OmniProfile("Box Voronoi Total");
        const auto diagram = VoronoiCore(inCenters, bb);

        Diagram::calculateDiagram(diagram);
        Diagram::calculateDetailedNeighborEdges(diagram);
    }

    GVector2D BoxDiagram::getSize() const
    {
        return GVector2D{ bounds.sizes.x(), bounds.sizes.z() };
    }

    GVector2D BoxDiagram::getOffset() const
    {
        return GVector2D{ bounds.nbl.x(), bounds.nbl.z() };
    }

    GVector2D BoxDiagram::getCenter() const
    {
        const GVector2D size = getSize();
        return { bounds.nbl.x() + size.x / 2, bounds.nbl.z() + size.z / 2 };
    }

    BoxDiagram BoxDiagram::relaxDiagram(const BoxDiagram& diagram, const int relaxationLevel)
    {
        auto getNewDiagram = [](const BoxDiagram& inDiagram) -> BoxDiagram
        {
            std::vector<GVector2D> newCenters;
            for (auto& cell : inDiagram)
            {
                newCenters << cell.getPolygon().getCenter();
            }

            return BoxDiagram(newCenters, inDiagram.getPerimeterBB());
        };

        auto currentIteration = diagram;

        for (int i = 0; i < relaxationLevel; i++)
        {
            currentIteration = std::move(getNewDiagram(currentIteration));
        }

        return std::move(currentIteration);
    }

    PolygonDiagram::PolygonDiagram(const std::vector<GVector2D>& inCenters, const std::vector<Segment2D>& perimeter)
    {
        Q_ASSERT_X(!inCenters.empty(), "PolygonDiagram()", "Provided centers array is empty");

        std::vector<GVector2D> pts;
        for (auto& seg : perimeter)
        {
            pts.push_back(seg.first);
        }

        centers = inCenters;
        boundsPolygon = Polygon2D(pts);

        const auto diagram = VoronoiCore(inCenters, boundsPolygon);
        Diagram::calculateDiagram(diagram);

        //TODO: Make this optional
        Diagram::calculateDetailedNeighborEdges(diagram);
    }

    PolygonDiagram::PolygonDiagram(const std::vector<GVector2D>& inCenters, const std::vector<GVector2D>& perimeter)
    {
        Q_ASSERT_X(!inCenters.empty(), "PolygonDiagram()", "Provided centers array is empty");

        centers = inCenters;
        boundsPolygon = Polygon2D(perimeter);

        const auto diagram = VoronoiCore(inCenters, boundsPolygon);
        Diagram::calculateDiagram(diagram);

        //TODO: Make this optional
        Diagram::calculateDetailedNeighborEdges(diagram);
    }

    PolygonDiagram::PolygonDiagram(const std::vector<GVector2D>& inCenters, const Polygon2D& perimeter)
    {
        Q_ASSERT_X(inCenters.size() > 1, "PolygonDiagram()", "Provided centers array is too small");

        centers = inCenters;
        boundsPolygon = perimeter;

        const auto diagram = VoronoiCore(inCenters, perimeter);
        Diagram::calculateDiagram(diagram);

        //TODO: Make this optional
        Diagram::calculateDetailedNeighborEdges(diagram);
    }

    GVector2D PolygonDiagram::getCenter() const
    {
        return boundsPolygon.getCenter();
    }

    PolygonDiagram PolygonDiagram::relaxDiagram(const PolygonDiagram& diagram, const int relaxationLevel)
    {
        auto getNewDiagram = [](const PolygonDiagram& inDiagram) -> PolygonDiagram
        {
            std::vector<GVector2D> newCenters;
            for (auto& cell : inDiagram)
            {
                newCenters << cell.getPolygon().getCenter();
            }

            return PolygonDiagram(newCenters, inDiagram.getPerimeterPolygon());
        };

        auto currentIteration = diagram;

        for (int i = 0; i < relaxationLevel; i++)
        {
            currentIteration = std::move(getNewDiagram(currentIteration));
        }

        return std::move(currentIteration);
    }
}
