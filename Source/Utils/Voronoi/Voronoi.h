#pragma once

#include "Utils/Polygon.h"
#include "Utils/CoreUtils.h"

#include "VoronoiCore.h"
#include "Scene/OmnigenDrawable.h"

struct BoundingBox;

namespace Voronoi
{
    class GVoronoiCell;
    class Diagram;
    class BoxDiagram;
    class PolygonDiagram;
}

void omniSave(const Voronoi::GVoronoiCell& object, OmniBin<std::ios::out>& omniBin);
void omniSave(const Voronoi::Diagram& object, OmniBin<std::ios::out>& omniBin);
void omniSave(const Voronoi::BoxDiagram& object, OmniBin<std::ios::out>& omniBin);
void omniSave(const Voronoi::PolygonDiagram& object, OmniBin<std::ios::out>& omniBin);

void omniLoad(Voronoi::GVoronoiCell& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Voronoi::Diagram& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Voronoi::BoxDiagram& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Voronoi::PolygonDiagram& object, OmniBin<std::ios::in>& omniBin);

namespace Voronoi
{
    class VoronoiCore;

    class GVoronoiCell
    {
    public:
        GVoronoiCell() = default;
        GVoronoiCell(const GVector2D& inCellCenter) : cellCenter(inCellCenter) {}

        // Returns the cell perimeter as a Polygon2D.
        [[nodiscard]] const auto& getPolygon() const { return cellPolygon; }

        // Returns the voronoi center that belongs to this cell
        [[nodiscard]] const auto& getVoronoiCenter() const
        {
            return cellCenter;
        }

        // Returns a map of this cell's neighbors.
        //The keys are the neighbor cell indices and the values are the edge of this cell that is shared between each neighbor.
        [[nodiscard]] const auto& getNeighbors() const
        {
            Q_ASSERT_X(!neighborsMap.isEmpty(), "GVoronoiCell::getNeighbors()", "Neighbors calculation for the diagram is turned off.");

            return neighborsMap;
        }

        [[nodiscard]] std::unordered_set<int> getNeighborsSet() const
        {
            Q_ASSERT_X(!neighborsMap.isEmpty(), "GVoronoiCell::getNeighbors()", "Neighbors calculation for the diagram is turned off.");

            return std::unordered_set<int>(neighborsMap.keyBegin(), neighborsMap.keyEnd());
        }

        // Returns a map of this cell's neighbors that only share 1 single point.
        [[nodiscard]] const auto& getPointNeighbors() const
        {
            Q_ASSERT_X(!neighborsMap.isEmpty(), "GVoronoiCell::getPointNeighbors()", "Neighbors calculation for the diagram is turned off.");

            return pointNeighbors;
        }

        // Returns whether one or more of this cell's edges are part of the overall diagram perimeter.
        bool isCellBounded() const { return isBounded; }

        bool isNeighbor(const int cellId) const { return neighborsMap.contains(cellId); }

        auto begin() const
        {
            return cellPolygon.begin();
        }

        auto end() const
        {
            return cellPolygon.end();
        }

        const GVector2D& operator[](int i) const
        {
            return cellPolygon[i];
        }

        [[nodiscard]] constexpr const Polygon2D* operator->() const
        {
            return &cellPolygon;
        }
        [[nodiscard]] constexpr Polygon2D* operator->()
        {
            return &cellPolygon;
        }

        [[nodiscard]] constexpr const Polygon2D& operator*() const&
        {
            return this->cellPolygon;
        }
        [[nodiscard]] constexpr Polygon2D& operator*()&
        {
            return this->cellPolygon;
        }

    private:
        friend class Diagram;
        friend class BoxDiagram;
        friend class PolygonDiagram;
        friend class VoronoiCore;

        Polygon2D cellPolygon;
        bool isBounded = false;
        GVector2D cellCenter;
        QMap<int, std::array<int, 2>> neighborsMap;
        QMap<int, int> pointNeighbors;

        FRIEND_OMNIBIN_NS(GVoronoiCell);
    };

    class Diagram
    {
    public:
        // Returns all of the cells of this diagram in the form of a GVoronoiCell.
        [[nodiscard]] const auto& getCells() const { return cells; }
        // Returns a collection of only the centers of this diagram.
        [[nodiscard]] const auto& getCenters() const { return centers; }
        // Returns the cell of the diagram at the given index.
        [[nodiscard]] const GVoronoiCell& getCellAt(const int i) const { return cells[i]; }
        // Returns index of the cell in the diagram.
        [[nodiscard]] const int getCellIndexFromCenter(const GVector2D& inCenter) const { return indexOf(centers, inCenter); }
        // Returns the cell of the diagram that the given center belongs to.
        [[nodiscard]] const GVoronoiCell& getCellFromCenter(const GVector2D& inCenter) const { return cells[indexOf(centers, inCenter)]; }
        // Returns the given cell's neighbors.
        [[nodiscard]] const auto& getCellNeighborsAt(const int i) const { return cells[i].getNeighbors(); }
        // Returns a map that includes all cell ids of those cell neighbors that only share 1 point.
        [[nodiscard]] const auto& getCellPointNeighborsAt(const int i) const { return cells[i].getPointNeighbors(); }

        virtual GVector2D getCenter() const = 0;

        // Draw the voronoi cells with an optional color parameter
        virtual void drawCells(const QVector4D& withColour = QVector4D(1.f, 1.f, 1.f, 1.f), const float withHeight = 0) const;
        // Draw the voronoi centers with an optional color parameter
        virtual void drawCenters(const QVector4D& withColour = QVector4D(1.f, 1.f, 1.f, 1.f), const float withHeight = 0) const;
        // Draw the points that define the cell polygons with an optional color parameter
        virtual void drawCellControlPoints(const QVector4D& withColour = QVector4D(1.f, 1.f, 1.f, 1.f), const float withHeight = 0) const;

        // Print the cell points to log. The use of this is discouraged, you should prefer visual debugging when possible.
        virtual void printCellsToLog() const;

        // Clip this diagram to fit the given bounds. This is different than generating a diagram with certain bounds because this is a post-processing step.
        // Returns a hash map of center ids and the clipped cells if they were successfully clipped.
        // You should only really use this if you need to fit the diagram to concave bounds.
        // This is not ideal for large cell counts
        virtual QHash<int, std::optional<Polygon2D>> clipDiagramToPolygon(const Polygon2D& clippingPolygon) const;

        template<typename CheckLambda>
        bool expandCellularCluster(QSet<int>* currentCluster, int targetCell, const CheckLambda& L = []() { return true; }) const
        {
            bool expanded = false;

            auto&& neighbors = getCellNeighborsAt(targetCell);
            for (auto nit = neighbors.keyBegin(); nit != neighbors.keyEnd(); ++nit)
                if (L(*nit))
                {
                    (*currentCluster) << *nit;
                    expanded = true;
                }

            return expanded;
        }

        auto begin() const
        {
            return cells.begin();
        }

        auto end() const
        {
            return cells.end();
        }

        const GVoronoiCell& operator[](const int i) const
        {
            return cells[i];
        }

    protected:
        Diagram() = default;
        virtual ~Diagram() = default;

        virtual void calculateDiagram(const VoronoiCore& inDiagram);
        virtual void calculateDetailedNeighborEdges(const VoronoiCore& inDiagram);

        std::vector<GVoronoiCell> cells;
        std::vector<GVector2D> centers;
 
        FRIEND_OMNIBIN_NS(Diagram)

    private:
        int findSuitableNeighbourCellForPolygonMerging(const Polygon2D& polygonInCell, const GVoronoiCell& cell, const QHash<int, std::vector<Polygon2D>>& cellsClippedPolygons) const;
    };

    class CellUpdate : public Editable
    {
    public:
        CellUpdate() {}
        CellUpdate(const std::vector<GVector2D>& inCellCenters) : cellCenters(inCellCenters) { }
        CellUpdate(const GVector2D& inCellCenter) : cellCenters({ inCellCenter }) { }
        std::vector<GVector2D> cellCenters;
    };

    // A Voronoi Diagram bound by a bounding box perimeter.
    class BoxDiagram final : public Diagram
    {
    public:
        BoxDiagram() = default;

        // Create a diagram of the given centers bound by the given box.
        BoxDiagram(const std::vector<GVector2D>& inCenters, const BoundingBox& bb);

        // Returns the X - Z sizes of the perimeter bounding box
        [[nodiscard]] GVector2D getSize() const;
        // Returns the X - Z offsets of the perimeter bounding box
        [[nodiscard]] GVector2D getOffset() const;
        // Returns the center point of the perimeter bounding box
        [[nodiscard]] GVector2D getCenter() const override;

        // Returns the perimeter bounding box
        [[nodiscard]] const BoundingBox& getPerimeterBB() const { return bounds; }

        // Returns a new diagram relaxed by the given amount of times
        static BoxDiagram relaxDiagram(const BoxDiagram& diagram, const int relaxationLevel);
    private:
        BoundingBox bounds;

        FRIEND_OMNIBIN_NS(BoxDiagram);
    };

    // A Voronoi Diagram bound by a convex polygon perimeter.
    class PolygonDiagram final : public Diagram
    {
    public:
        PolygonDiagram() = default;

        // Create a diagram of the given centers bound by the polygon defined by the given 2D segments.
        PolygonDiagram(const std::vector<GVector2D>& inCenters, const std::vector<Segment2D>& perimeter);

        // Create a diagram of the given centers bound by the polygon defined by the given points. Points should be either fully CW or fully CCW.
        PolygonDiagram(const std::vector<GVector2D>& inCenters, const std::vector<GVector2D>& perimeter);

        // Create a diagram of the given centers bound by the given 2D polygon.
        PolygonDiagram(const std::vector<GVector2D>& inCenters, const Polygon2D& perimeter);

        // Returns the center point of the bounding polygon
        [[nodiscard]] GVector2D getCenter() const override;

        // Returns the bounding polygon
        [[nodiscard]] const Polygon2D& getPerimeterPolygon() const { return boundsPolygon; }

        // Returns a new diagram relaxed by the given amount of times
        static PolygonDiagram relaxDiagram(const PolygonDiagram& diagram, const int relaxationLevel);
    private:
        Polygon2D boundsPolygon;

        FRIEND_OMNIBIN_NS(PolygonDiagram);
    };
}

inline void omniSave(const Voronoi::GVoronoiCell& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.cellPolygon;
    omniBin << object.isBounded;
    omniBin << object.cellCenter;
    omniBin << object.neighborsMap;
    omniBin << object.pointNeighbors;
}

inline void omniLoad(Voronoi::GVoronoiCell& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.cellPolygon;
    omniBin >> object.isBounded;
    omniBin >> object.cellCenter;
    omniBin >> object.neighborsMap;
    omniBin >> object.pointNeighbors;
}

inline void omniSave(const Voronoi::Diagram& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.cells;
    omniBin << object.centers;
}

inline void omniLoad(Voronoi::Diagram& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.cells;
    omniBin >> object.centers;
}

inline void omniSave(const Voronoi::BoxDiagram& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Voronoi::Diagram&>(object);
    omniBin << object.bounds;
}

inline void omniLoad(Voronoi::BoxDiagram& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Voronoi::Diagram&>(object);
    omniBin >> object.bounds;
}

inline void omniSave(const Voronoi::PolygonDiagram& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Voronoi::Diagram&>(object);
    omniBin << object.boundsPolygon;
}

inline void omniLoad(Voronoi::PolygonDiagram& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Voronoi::Diagram&>(object);
    omniBin >> object.boundsPolygon;
}