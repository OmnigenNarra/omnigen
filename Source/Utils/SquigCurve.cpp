#include "stdafx.h"
#include "SquigCurve.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Core/EditorGridDrawable.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Omnigen.h"

namespace Generation
{
    SquigArea::SquigArea(std::set<std::pair<int, int>> inSquares, float inSquareSize, double inStraightness, bool skipClustering)
        : squareSize(inSquareSize)
        , straightness(inStraightness)
        , invStraightness(1.0 - inStraightness)
    {
        computeSquigSquares(&inSquares, skipClustering);
        computeSquigNeighborhood();
        createSquigGraph();
    }

    void SquigArea::computeSquigSquares(std::set<std::pair<int, int>>* gridSquares, bool skipClustering)
    {
        while (!gridSquares->empty())
        {
            // Greedily create a new squig square.
            auto sqs = QSharedPointer<SquigSquare>::create();
            std::vector<std::pair<int, int>> candidates;
            for (auto&& sq : *gridSquares)
            {
                int s = skipClustering ? 1 : getMaxSquigSquareSize(gridSquares, sq);
                if (s > sqs->size)
                {
                    sqs->size = s;
                    candidates.resize(1);
                    candidates.back() = { sq.first, sq.second };
                }
                else if (s == sqs->size)
                {
                    candidates.resize(candidates.size() + 1);
                    candidates.back() = { sq.first, sq.second };
                }
            }

            // Pick a candidate
            int idx = std::uniform_int_distribution<int>(0, int(candidates.size()) - 1)(Generation::gRandomEngine);
            sqs->coords = GVector2D{ float(candidates[idx].first), float(candidates[idx].second) } * squareSize;
            sqs->squareSize = squareSize;

            // Remove all squares in chosen squid square.
            for (int x = candidates[idx].first; x < candidates[idx].first + sqs->size; ++x)
                for (int z = candidates[idx].second; z < candidates[idx].second + sqs->size; ++z)
                    gridSquares->erase({ x, z });

            // Store
            baseSquares << sqs;
        }
    }

    int SquigArea::getMaxSquigSquareSize(std::set<std::pair<int, int>>* gridSquares, const std::pair<int, int>& sq)
    {
        int size = 1;
        int result = size;

        while (true)
        {
            size *= 2;

            bool ok = true;
            for (int i = 0; i < size; ++i)
                for (int j = 0; j < size; ++j)
                    if (!gridSquares->contains({ sq.first + i, sq.second + j }))
                        ok = false;

            if (!ok)
                return result;
            else
                result = size;
        }
    }

    void SquigArea::computeSquigNeighborhood()
    {
        for (auto&& baseSquare : baseSquares)
        {
            baseSquare->neighbors[0].clear();
            baseSquare->neighbors[1].clear();
            baseSquare->neighbors[2].clear();
            baseSquare->neighbors[3].clear();

            for (auto&& otherSquare : baseSquares)
            {
                // Can't neighbor self
                if (*baseSquare == *otherSquare)
                    continue;

                // Get shared segment
                auto overlap = baseSquare->sharedSegment(*otherSquare);
                if (!overlap)
                    continue;

                // shortcuts
                float mx = baseSquare->coords.x;
                float mz = baseSquare->coords.z;
                float mX = mx + baseSquare->size * squareSize;
                float mZ = mz + baseSquare->size * squareSize;

                float x = otherSquare->coords.x;
                float z = otherSquare->coords.z;
                float X = x + otherSquare->size * squareSize;
                float Z = z + otherSquare->size * squareSize;

                // -x side
                if (mx == X)
                    baseSquare->neighbors[0] << otherSquare;

                // -z side
                if (mz == Z)
                    baseSquare->neighbors[1] << otherSquare;

                // +x side
                if (mX == x)
                    baseSquare->neighbors[2] << otherSquare;

                // +z side
                if (mZ == z)
                    baseSquare->neighbors[3] << otherSquare;
            }
        }
    }

    void SquigArea::createSquigGraph()
    {
        graph.reset();

        for (auto&& sqs : baseSquares)
            graph.addVertex(sqs);

        for (auto&& sqs : baseSquares)
            for (int i = 0; i < 4; ++i)
                for (auto&& wneighbor : sqs->neighbors[i])
                    if (auto neighbor = wneighbor.lock(); wneighbor)
                        graph.addEdge(sqs, neighbor);

        graph.calculateDistances();
    }

    void SquigArea::debugDrawSquares() const
    {
        for (auto&& sq : workPath)
            sq->debugSquare(50);
    }

    SquigPathInfo SquigArea::computeSquigPath(int depth, const GVector2D& inStartPoint, const GVector2D& inEndPoint)
    {
        startPoint = inStartPoint;
        endPoint = inEndPoint;
        workSquares = baseSquares;
        auto leftoverSquares = workSquares;

#if DEBUG_SQUIG_AREA
        debugDrawSquares();
#endif
        bool needReversePath = false;

        auto startSq = std::find_if(workSquares.begin(), workSquares.end(), [&](auto&& sq) { return sq->contains(startPoint, true); });
        auto endSq = std::find_if(workSquares.begin(), workSquares.end(), [&](auto&& sq) { return sq->contains(endPoint, true); });

        workPath.clear();
        if (auto path = graph.getOptimalPath(*startSq, *endSq); path)
            for (int i : path->verticesReached)
                workPath << graph.getVertices()[i];

        // Ensure start of path square contains only one end point when there are only 2 squares
        if (workPath.size() == 2 && workPath.front()->contains(startPoint) && workPath.front()->contains(endPoint))
        {
            needReversePath = true;
            auto temp = startPoint;
            startPoint = endPoint;
            endPoint = temp;
            std::reverse(workPath.begin(), workPath.end());
        }

        for (auto&& sq : workPath)
            removeOne(leftoverSquares, sq);

        auto usedSquares = workPath;

        buildFinalPath(depth);

        if (needReversePath)
            std::reverse(workPath.begin(), workPath.end());

        return { workPath, usedSquares, leftoverSquares };
    }

    auto SquigArea::pickNextSquareForRidgeTree(const std::vector<QSharedPointer<SquigNode>>& previousTier)
        -> std::optional<std::tuple<QSharedPointer<SquigSquare>, int, QSharedPointer<SquigNode>>>
    {
        static auto computePathLength = [](const std::vector<QSharedPointer<SquigSquare>>& path)
        {
            return distance(path.front()->getMidpoint(), path.back()->getMidpoint());
        };

        // Params
        static const float lengthThresholdLower = 0.1f;
        static const float lengthThresholdUpper = 0.2f;

        // Many additions possible per each step
        std::vector<std::tuple<QSharedPointer<SquigSquare>, int, QSharedPointer<SquigNode>>> candidates;
        auto&& vertices = graph.getVertices();

        // For each area square
        for (auto&& sq : baseSquares)
        {
            float minD = std::numeric_limits<float>::max();
            float minRawD = std::numeric_limits<float>::max();
            const QSharedPointer<SquigSquare>* closestSquare = nullptr;
            QSharedPointer<SquigNode> closestNode;
            int closestIdx = -1;

            // Find closest square of the previous tier
            for (auto&& previousNode : previousTier)
            {
                for (int i = 0; i < previousNode->pathInfo.path.size(); ++i)
                {
                    auto&& midpoint = previousNode->pathInfo.path[i]->getMidpoint();

                    // Find natural square
                    auto naturalSqIt = std::find_if(vertices.begin(), vertices.end(), [&](auto&& sq) { return sq->contains(midpoint, false); });
                    if (naturalSqIt == vertices.end())
                        continue;

                    // Get optimal path to this square
                    auto optimalPath = graph.getOptimalPath(sq, *naturalSqIt);
                    if (!optimalPath)
                        continue;

                    // Find the min distance squares
                    if (optimalPath->distance < minD)
                    {
                        minD = optimalPath->distance;
                        minRawD = distanceSquared(midpoint, sq->getMidpoint());
                        closestSquare = &naturalSqIt.value();
                        closestNode = previousNode;
                        closestIdx = i;
                    }
                    else if (optimalPath->distance == minD)
                    {
                        if (float rawD = distanceSquared(midpoint, sq->getMidpoint()); rawD < minRawD)
                        {
                            minRawD = rawD;
                            closestIdx = i;
                            closestSquare = &naturalSqIt.value();
                            closestNode = previousNode;
                        }
                    }
                }
            }

            if (closestSquare)
            {
                float parentLength = computePathLength(closestNode->pathInfo.path);

                // Must work - path lookup successfully performed during Find section above
                float dist = graph.getOptimalPath(sq, *closestSquare)->distance;
                if ((dist >= parentLength * lengthThresholdLower) && dist <= parentLength * lengthThresholdUpper)
                    candidates << std::tuple{ sq, closestIdx, closestNode };
            }
        }

        if (candidates.empty())
            return {};

        // Return random square tied for the minimum dist.
        return candidates[std::uniform_int_distribution<int>(0, int(candidates.size()) - 1)(Generation::gRandomEngine)];
    }

    void SquigArea::createNextRidgeTier(const std::vector<QSharedPointer<SquigNode>>& previousTier, int squigDepth)
    {
        static auto nodeUsesSquare = [](const std::vector<QSharedPointer<SquigNode>>& container, const QSharedPointer<SquigSquare>& sq)
        {
            for (auto&& node : container)
                for (auto&& usedSquare : node->pathInfo.usedNaturalSquares)
                    if ((usedSquare->coords == sq->coords) && (usedSquare->size == sq->size))
                        return true;

            return false;
        };

        // Preserve current squares' state
        // baseSquares will be modified during this tier's gen to avoid collisions
        // but it needs to be restored later
        auto currentSquares = baseSquares;

        std::vector<QSharedPointer<SquigNode>> newTier;

        while (true)
        {
            // Pick random leftover square from those of at least 50% length of the parent tier
            auto nextSubridge = pickNextSquareForRidgeTree(previousTier);
            if (!nextSubridge)
                break;

            auto&& [targetSquare, closestParentPathIdx, parentNode] = *nextSubridge;

            // Create subridge
            auto nextTierNode = QSharedPointer<SquigNode>::create();
            nextTierNode->pathInfo = computeSquigPath(squigDepth, parentNode->pathInfo.path[closestParentPathIdx]->getMidpoint(), targetSquare->getMidpoint() + QVector3D(100, 0, 100));
            parentNode->subpaths << nextTierNode;
            newTier << nextTierNode;

            // Just for this tier generation, remove squares used by this subridge from further consideration to avoid collisions.
            for (auto&& sq : nextTierNode->pathInfo.usedNaturalSquares)
            {
                if (nodeUsesSquare(previousTier, sq))
                    continue;

                removeOne(baseSquares, sq);

                // Margin
                for (int n = 0; n < 4; ++n)
                    for (auto&& neighbor : sq->neighbors[n])
                        if (!nodeUsesSquare(previousTier, neighbor))
                            removeOne(baseSquares, neighbor);
            }

            computeSquigNeighborhood();
            createSquigGraph();
        }

        // Tier is done. Restore saved squares.
        baseSquares = currentSquares;

        // Remove squares of previous tier - these may no longer be used
        if (!newTier.empty())
            for (auto&& parent : previousTier)
                for (auto&& sq : parent->pathInfo.usedNaturalSquares)
                {
                    removeOne(baseSquares, sq);

                    // Margin
                    for (int n = 0; n < 4; ++n)
                        for (auto&& neighbor : sq->neighbors[n])
                            removeOne(baseSquares, neighbor);
                }

        // Update graph
        computeSquigNeighborhood();
        createSquigGraph();

        // RECURSION
        if (!newTier.empty())
            createNextRidgeTier(newTier, squigDepth);
    }

    void SquigArea::splitSquareOnPath(QSharedPointer<SquigSquare> sq) const
    {
        // Identify must-include squares
        int idx = indexOf(workPath, sq);
        auto entrantSegment = (idx > 0) ? sq->sharedSegment(*workPath[idx - 1]) : std::nullopt;
        auto exitSegment = (idx < (int(workPath.size()) - 1)) ? sq->sharedSegment(*workPath[idx + 1]) : std::nullopt;

        // Create new squares
        std::array<std::array<QSharedPointer<SquigSquare>, 2>, 2> newSquares;
        for (int x = 0; x < 2; ++x)
            for (int z = 0; z < 2; ++z)
                newSquares[x][z] = QSharedPointer<SquigSquare>::create();

        float newSize = sq->size * 0.5f;
        newSquares[0][0]->coords = sq->coords;
        newSquares[1][0]->coords = sq->coords + GVector2D(newSize * squareSize, 0);
        newSquares[0][1]->coords = sq->coords + GVector2D(0, newSize * squareSize);
        newSquares[1][1]->coords = sq->coords + GVector2D(newSize * squareSize, newSize * squareSize);

        for (int x = 0; x < 2; ++x)
            for (int z = 0; z < 2; ++z)
            {
                newSquares[x][z]->size = newSize;
                newSquares[x][z]->squareSize = squareSize;
            }

        auto&& [subpath, entry, exit] = chooseInOutSquares(idx, entrantSegment, exitSegment, newSquares);

        // A = B
        if (subpath.front() == subpath.back())
        {
            subpath.pop_back();
        }
        // A | B (neighbors), likely to go directly.
        else if (subpath[0]->sharedSegment(*subpath[1]))
        {
            // Decide whether to link entry and exit directly
            bool directLink = hybrid_int_distribution<int>(0, 1, 0, 1.0 - pow(invStraightness, 2))(Generation::gRandomEngine);

            // Never indirect for edge cases.
            if (!entrantSegment || !exitSegment)
                directLink = true;

            if (!directLink)
            {
                subpath.resize(4);
                subpath.back() = subpath[1];

                // Add squares adjacent to the entry and exit squares
                if (entry.x == exit.x)
                {
                    subpath[1] = newSquares[1 - entry.x][entry.z];
                    subpath[2] = newSquares[1 - exit.x][exit.z];
                }
                else // entry.z == exit.z
                {
                    subpath[1] = newSquares[entry.x][1 - entry.z];
                    subpath[2] = newSquares[exit.x][1 - exit.z];
                }
            }
        }
        else // A / B (diagonal), must go through either of the adjacent squares
        {
            subpath.resize(3);
            subpath.back() = subpath[1];

            static auto uniformBoolDist = std::uniform_int_distribution<int>(0, 1);
            bool goThroughX = uniformBoolDist(Generation::gRandomEngine);
            subpath[1] = goThroughX ? newSquares[1 - entry.x][entry.z] : newSquares[entry.x][1 - entry.z];
        }

        // Replace source square with the subpath
        workPath.erase(workPath.begin() + idx);
        for (int i = 0; i < subpath.size(); ++i)
            workPath.insert(workPath.begin() + idx + i, subpath[i]);
    }

    std::tuple<std::vector<QSharedPointer<SquigSquare>>, GPoint, GPoint> SquigArea::chooseInOutSquares(
            int sqIdx,
            const std::optional<Segment2D>& entrantSegment,
            const std::optional<Segment2D>& exitSegment,
            const std::array<std::array<QSharedPointer<SquigSquare>, 2>, 2>& newSquares) const
    {
        std::vector<QSharedPointer<SquigSquare>> subpath;
        GPoint entry, exit;
        static auto straightDist = hybrid_int_distribution<int>(0, 1, 0, straightness);
        static auto uniformBoolDist = std::uniform_int_distribution<int>(0, 1);

        // Repeatable logic
        auto findSquareForSegment = [this, &subpath, &newSquares, sqIdx](const Segment2D& segment, GPoint* target, bool isEntry, int adj)
        {
            for (int x = 0; x < 2; ++x)
                for (int z = 0; z < 2; ++z)
                    if (newSquares[x][z]->sharedSegment(segment))
                        subpath.insert(subpath.begin() + adj, newSquares[x][z]);

            if (subpath.size() > (1 + adj))
            {
                int ppsq = sqIdx - 2;
                if (ppsq >= 0)
                {
                    // Pick considering ppsq
                    float d0 = distance(workPath[ppsq]->getMidpoint(), subpath[adj]->getMidpoint());
                    float d1 = distance(workPath[ppsq]->getMidpoint(), subpath[1 + adj]->getMidpoint());
                    int nearer = adj + ((d0 < d1) ? 0 : 1);
                    int farther = adj + ((d0 < d1) ? 1 : 0);

                    bool goStraight = straightDist(Generation::gRandomEngine);
                    subpath.erase(subpath.begin() + (goStraight ? farther : nearer));
                }
                else
                {
                    int offset = uniformBoolDist(Generation::gRandomEngine);

                    // Ensure picking different segment then opposite end of path
                    auto&& oppositeEndPoint = isEntry ? endPoint : startPoint;
                    if (subpath[adj]->contains(oppositeEndPoint))
                        offset = 0;
                    else if (subpath[adj + 1]->contains(oppositeEndPoint))
                        offset = 1;

                    subpath.erase(subpath.begin() + adj + offset);
                }
            }

            for (int x = 0; x < 2; ++x)
                for (int z = 0; z < 2; ++z)
                    if (newSquares[x][z] == subpath[adj])
                        *target = { x, z };
        };

        auto complementSingleSquare = [this, &newSquares, &entry, &exit, &subpath, sqIdx](bool isEntry)
        {
            GPoint& p1 = isEntry ? entry : exit;
            GPoint& p2 = isEntry ? exit : entry;

            bool goDiagonally = hybrid_int_distribution<int>(0, 1, 0.9, 0.9)(Generation::gRandomEngine);

            if (workPath[sqIdx]->contains(startPoint, true))
            {
                // start point inside given square
                if (newSquares[p2.x][p2.z]->contains(startPoint, true))
                {
                    subpath.insert(subpath.begin() + int(!isEntry), newSquares[p2.x][p2.z]);
                    return;
                }

                goDiagonally = newSquares[1 - p2.x][1 - p2.z]->contains(startPoint, true);
            }
            if (workPath[sqIdx]->contains(endPoint, true))
            {
                // end point inside given square
                if (newSquares[p2.x][p2.z]->contains(endPoint, true))
                {
                    subpath.insert(subpath.begin() + int(!isEntry), newSquares[p2.x][p2.z]);
                    return;
                }

                goDiagonally = newSquares[1 - p2.x][1 - p2.z]->contains(endPoint, true);
            }

            if (goDiagonally)
            {
                p1 = { 1 - p2.x, 1 - p2.z };
            }
            else
            {
                bool goThroughX = uniformBoolDist(Generation::gRandomEngine);

                if (workPath[sqIdx]->contains(startPoint, true))
                    goThroughX = newSquares[1 - p2.x][p2.z]->contains(startPoint, true);
                if (workPath[sqIdx]->contains(endPoint, true))
                    goThroughX = newSquares[1 - p2.x][p2.z]->contains(endPoint, true);

                p1 = { goThroughX ? (1 - p2.x) : p2.x, goThroughX ? p2.z : (1 - p2.z) };
            }

            subpath.insert(subpath.begin() + int(!isEntry), newSquares[p1.x][p1.z]);
        };

        // Single square
        if (!entrantSegment && !exitSegment)
        {
            // Both start and end are within this square!
            for (int x = 0; x < 2; ++x)
                for (int z = 0; z < 2; ++z)
                {
                    if (newSquares[x][z]->contains(startPoint, true))
                        entry = { x,z };

                    if (newSquares[x][z]->contains(endPoint, true))
                        exit = { x,z };
                }

            return { std::vector{newSquares[entry.x][entry.z], newSquares[exit.x][exit.z]}, entry, exit };
        }

        // Choose entry square if determined by entrant segment
        if (entrantSegment)
            findSquareForSegment(*entrantSegment, &entry, true, 0);

        // Choose exit square if determined by exit segment
        if (exitSegment)
            findSquareForSegment(*exitSegment, &exit, false, int(bool(entrantSegment)));

        // Choose entry square if only exit is given
        if (!entrantSegment)
            complementSingleSquare(true);

        // Choose exit square if only entry is given
        else if (!exitSegment)
            complementSingleSquare(false);

        return { subpath, entry, exit };
    }

    void SquigArea::buildFinalPath(int depth) const
    {
        // Start with greatest size
        float maxSize = 0;
        for (auto&& sq : workPath)
            if (sq->size > maxSize)
                maxSize = sq->size;

        for (float s = maxSize; s > (1.0f / pow(2.0f, depth)); s /= 2.0f)
            for (int i = 0; i < workPath.size(); ++i)
                if (workPath[i]->size == s)
                    splitSquareOnPath(workPath[i]);
    }

    QVector3D SquigSquare::getMidpoint() const
    {
        return QVector3D
        {
            coords.x + size * squareSize * 0.5f,
            0,
            coords.z + size * squareSize * 0.5f,
        };
    }

    std::optional<Segment2D> SquigSquare::sharedSegment(const SquigSquare& other) const
    {
        // Assume one square does not contain the other.

        // shortcuts
        float mx = coords.x;
        float mz = coords.z;
        float mX = mx + size * squareSize;
        float mZ = mz + size * squareSize;

        float x = other.coords.x;
        float z = other.coords.z;
        float X = x + other.size * squareSize;
        float Z = z + other.size * squareSize;

        // -x
        if (auto s = Segment2D({ mx, mz }, { mx, mZ }) & Segment2D({ X, z }, { X, Z }))
            return s;

        // +x
        if (auto s = Segment2D({ mX, mz }, { mX, mZ }) & Segment2D({ x, z }, { x, Z }))
            return s;

        // -z
        if (auto s = Segment2D({ mx, mz }, { mX, mz }) & Segment2D({ x, Z }, { X, Z }))
            return s;

        // +z
        if (auto s = Segment2D({ mx, mZ }, { mX, mZ }) & Segment2D({ x, z }, { X, z }))
            return s;

        return {};
    }

    std::optional<Segment2D> SquigSquare::sharedSegment(const Segment2D& segment) const
    {
        // shortcuts
        float mx = coords.x;
        float mz = coords.z;
        float mX = mx + size * squareSize;
        float mZ = mz + size * squareSize;

        // -x
        if (auto s = Segment2D({ mx, mz }, { mx, mZ }) & segment)
            return s;

        // +x
        if (auto s = Segment2D({ mX, mz }, { mX, mZ }) & segment)
            return s;

        // -z
        if (auto s = Segment2D({ mx, mz }, { mX, mz }) & segment)
            return s;

        // +z
        if (auto s = Segment2D({ mx, mZ }, { mX, mZ }) & segment)
            return s;

        return {};
    }

    float SquigSquare::Weighter::operator()(const QSharedPointer<SquigSquare>& A, const QSharedPointer<SquigSquare>& B)
    {
        return (A->getMidpoint() - B->getMidpoint()).length();
    }

    bool SquigSquare::contains(const GVector2D& p, bool includeEdge /*= true*/) const
    {
        float x = coords.x;
        float X = coords.x + size * squareSize;
        float z = coords.z;
        float Z = coords.z + size * squareSize;

        bool xok = (p.x > x) && (p.x < X);
        if (includeEdge)
            xok |= ((p.x == x) || (p.x == X));

        bool zok = (p.z >= z) && (p.z <= Z);
        if (includeEdge)
            zok |= ((p.z == z) || (p.z == Z));

        return xok && zok;
    }

    void SquigSquare::debugSquare(float height) const
    {
        QVector3D xz = { coords.x, height, coords.z };
        QVector3D Xz = { coords.x + size * squareSize, height, coords.z };
        QVector3D xZ = { coords.x, height, coords.z + size * squareSize };
        QVector3D XZ = { coords.x + size * squareSize, height, coords.z + size * squareSize };

        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ xz, Xz }, QVector4D(1, 1, 1, 0.75));
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ Xz, XZ }, QVector4D(1, 1, 1, 0.75));
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ XZ, xZ }, QVector4D(1, 1, 1, 0.75));
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ xZ, xz }, QVector4D(1, 1, 1, 0.75));
    }
}