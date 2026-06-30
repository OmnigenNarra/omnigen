#pragma once
#include <QSharedPointer>
#include "Utils/CoreUtils.h"
#include "Graph.h"

#define DEBUG_SQUIG_AREA 0

namespace Generation
{
    struct SquigSquare
    {
        struct Weighter
        {
            float operator()(const QSharedPointer<SquigSquare>& A, const QSharedPointer<SquigSquare>& B);
        };

        QVector3D getMidpoint() const;
        std::optional<Segment2D> sharedSegment(const SquigSquare& other) const;
        std::optional<Segment2D> sharedSegment(const Segment2D& segment) const;
        bool contains(const GVector2D& p, bool includeEdge = true) const;
        void debugSquare(float height) const;

        // [-size, -size]
        GVector2D coords;

        // side length in world units
        float size;

        float squareSize;

        // 0: [-size, 0]
        // 1: [0, -size]
        // 2: [size, 0]
        // 3: [0, size]
        std::vector<QWeakPointer<SquigSquare>> neighbors[4];

        constexpr bool operator<=>(const SquigSquare&) const noexcept = default;
    };

    struct SquigSquareHash
    {
    public:
        size_t operator()(const SquigSquare square) const
        {
            return std::hash<float>()(square.coords.x) ^ std::hash<float>()(square.coords.z) ^ std::hash<float>()(square.size);
        }
    };

    struct SquigPathInfo
    {
        std::vector<QSharedPointer<SquigSquare>> path;
        std::vector<QSharedPointer<SquigSquare>> usedNaturalSquares;
        std::vector<QSharedPointer<SquigSquare>> leftoverNaturalSquares;
    };

    struct SquigNode
    {
        SquigPathInfo pathInfo;
        std::vector<QSharedPointer<SquigNode>> subpaths;
    };

    struct SquigArea
    {
        SquigArea(std::set<std::pair<int, int>> inSquares, float inSquareSize, double inStraightness, bool skipClustering = false);

        SquigPathInfo computeSquigPath(int depth, const GVector2D& inStartPoint, const GVector2D& inEndPoint);

        std::vector<QSharedPointer<SquigSquare>>& getBaseSquares() { return baseSquares; }

    //private:
        void buildFinalPath(int depth) const;
        void splitSquareOnPath(QSharedPointer<SquigSquare> sq) const;

        std::tuple<std::vector<QSharedPointer<SquigSquare>>, GPoint, GPoint> chooseInOutSquares(
            int sqIdx,
            const std::optional<Segment2D>& entrantSegment,
            const std::optional<Segment2D>& exitSegment,
            const std::array<std::array<QSharedPointer<SquigSquare>, 2>, 2>& newSquares) const;

        void debugDrawSquares() const;
        void computeSquigSquares(std::set<std::pair<int, int>>* gridSquares, bool skipClustering);
        void computeSquigNeighborhood();
        void createSquigGraph();
        int getMaxSquigSquareSize(std::set<std::pair<int, int>>* gridSquares, const std::pair<int, int>& sq);
        std::optional<std::tuple<QSharedPointer<SquigSquare>, int, QSharedPointer<SquigNode>>> pickNextSquareForRidgeTree(const std::vector<QSharedPointer<SquigNode>>& previousTier);
        void createNextRidgeTier(const std::vector<QSharedPointer<SquigNode>>& previousTier, int squigDepth);

        Graph<QSharedPointer<SquigSquare>, SquigSquare::Weighter> graph;
        std::vector<QSharedPointer<SquigSquare>> baseSquares;
        const float squareSize;
        const double straightness;
        const double invStraightness;

        mutable GVector2D startPoint;
        mutable GVector2D endPoint;
        mutable std::vector<QSharedPointer<SquigSquare>> workSquares;
        mutable std::vector<QSharedPointer<SquigSquare>> workPath;
    };
}