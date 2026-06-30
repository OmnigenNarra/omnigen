#include "stdafx.h"
#include "ShorelineUtils.h"



QSet<GPoint> ShorelineUtils::coverage(const QSet<GPoint>& s, const GPoint& p, const GPoint& excludedP)
{
    QSet<GPoint> visited;
    QQueue<GPoint> queue;

    auto&& addToQueue = [&](const GPoint& point, int x, int z)
    {
        GPoint newPoint = GPoint(point.x + x, point.z + z);
        if (s.contains(newPoint) && !visited.contains(newPoint) && newPoint != excludedP)
        {
            visited.insert(newPoint);
            queue.enqueue(newPoint);
        }
    };

    addToQueue(p, 0, 0);

    while (!queue.isEmpty())
    {
        GPoint point = queue.dequeue();
        addToQueue(point, 1, 0);
        addToQueue(point, 0, 1);
        addToQueue(point, -1, 0);
        addToQueue(point, 0, -1);
    }
    return visited;
}

std::vector<QSet<GPoint>> ShorelineUtils::splitSeparateSet(QSet<GPoint> set)
{
    std::vector<QSet<GPoint>> splitSet;

    while (!set.isEmpty())
    {
        QSet<GPoint> cov = coverage(set, *set.cbegin());
        set -= cov;
        splitSet << cov;
    }

    return splitSet;
}

std::set<std::pair<int, int>> ShorelineUtils::splitSetIntoSquares(const QSet<GPoint>& set, int detail)
{
    std::set<std::pair<int, int>> squares;

    for (auto&& sq : set)
        for (int x = 0; x < detail; x++)
            for (int z = 0; z < detail; z++)
                squares.insert({ sq.x * detail + x, sq.z * detail + z });

    return squares;
}

bool ShorelineUtils::isNeighboring(const QSet<GPoint>& sourceSet, const QSet<GPoint>& neighborSet)
{
    for (auto&& sq : sourceSet)
        if (neighborSet.contains({ sq.x + 1, sq.z }) ||
            neighborSet.contains({ sq.x - 1, sq.z }) ||
            neighborSet.contains({ sq.x, sq.z + 1 }) ||
            neighborSet.contains({ sq.x, sq.z - 1 }))
            return true;

    return false;
}

std::optional<GPoint> ShorelineUtils::findOnlyNeighboringSquare(const QSet<GPoint>& sourceSet, const QSet<GPoint>& neighborSet)
{
    GPoint onlyNeighbouring;

    int howManySeasideNeighbourIsland = 0;
    for (auto&& sq : sourceSet)
    {
        if (neighborSet.contains({ sq.x + 1, sq.z }) ||
            neighborSet.contains({ sq.x - 1, sq.z }) ||
            neighborSet.contains({ sq.x, sq.z + 1 }) ||
            neighborSet.contains({ sq.x, sq.z - 1 }))
        {
            onlyNeighbouring = sq;
            howManySeasideNeighbourIsland++;
        }

        if (howManySeasideNeighbourIsland > 1)
            return {};
    }

    return onlyNeighbouring;
}

GPoint ShorelineUtils::findMidpoint(const QSet<GPoint>& set)
{
    GPoint midPoint;

    for (auto&& p : set)
    {
        midPoint.x += p.x;
        midPoint.z += p.z;
    }

    midPoint.x = std::ceil((float)midPoint.x / set.size());
    midPoint.z = std::ceil((float)midPoint.z / set.size());

    return midPoint;
}

std::pair<QSet<GPoint>, QSet<GPoint>> ShorelineUtils::splitSetByVector(const QSet<GPoint>& set, const GVector2D& vector, std::optional<GVector2D> vectorPoint /*= std::nullopt*/)
{
    std::pair<QSet<GPoint>, QSet<GPoint>> splitSet;

    if (!vectorPoint)
        vectorPoint = ShorelineUtils::findMidpoint(set).midPoint();
    
    for (auto&& point : set)
    {
        QVector3D newV = (point.midPoint() - *vectorPoint).normalized();
        if (angle360(newV, vector) > 180.0f)
            splitSet.second.insert(point);
        else
            splitSet.first.insert(point);
    }

    return splitSet;
}

std::tuple<std::vector<std::vector<QVector3D>>, bool> ShorelineUtils::findShorelinesAlongLandmass(const std::vector<QVector3D>& landmass, const std::vector<std::vector<QVector3D>>& allSeasidePolygons)
{
    std::vector<std::vector<QVector3D>> newShorelines;
    bool isCoast = false;

    auto&& landmassCPts = asCircular(landmass);

    for (auto&& seasidePolygon : allSeasidePolygons)
    {
        std::optional<int> startIdx;
        for (int i = 0; i < landmass.size(); i++)
            if (!PolygonUtils::contains((landmass[i] + landmass[landmassCPts.findIdx(i, -1)]) * 0.5f, seasidePolygon, false))
            {
                startIdx = i;
                break;
            }

        isCoast = (bool)startIdx;

        // island
        if (!isCoast)
        {
            newShorelines << landmass;
            break;
        }
        // coast
        else
        {
            std::vector<QVector3D> newShoreline;

            for (int i = 0; i <= landmass.size(); i++)
            {
                auto idx = landmassCPts.findIdx(*startIdx, i);

                if (PolygonUtils::contains((landmass[idx] + landmass[landmassCPts.findIdx(idx, -1)]) * 0.5f, seasidePolygon, false))
                {
                    if (newShoreline.empty())
                        newShoreline << landmass[landmassCPts.findIdx(idx, -1)];

                    newShoreline << landmass[idx];
                }
                else if (!newShoreline.empty())
                {
                    newShorelines << newShoreline;
                    newShoreline.clear();
                }
            }
        }
    }

    return { newShorelines , isCoast };
}

void ShorelineUtils::reduceDistanceBetweenPoints(std::vector<std::vector<QVector3D>>* shorelines, float distanceBetweenPoints, bool isCoast, std::optional<float> removeThreshold /*= std::nullopt*/)
{
    for (auto&& shoreline : *shorelines)
    {
        std::vector<QVector3D> newShoreline;
        float shorelineDistance = 0;

        auto cShoreline = asCircular(shoreline);
        for (int i = 0; i < shoreline.size(); i++)
        {
            newShoreline << shoreline[i];

            if (isCoast && i == shoreline.size() - 1)
                break;

            auto&& p1 = shoreline[i];
            auto&& p2 = shoreline[cShoreline.findIdx(i, 1)];
            auto dist = p1.distanceToPoint(p2);
            auto dir = (p2 - p1).normalized();
            int pointsToAdd = dist / distanceBetweenPoints - 1;

            shorelineDistance += dist;
            for (int j = 0; j < pointsToAdd; j++)
                newShoreline << (newShoreline.back() + dir * distanceBetweenPoints);
        }

        if (removeThreshold && shorelineDistance <= *removeThreshold)
            shoreline = {};
        else
            shoreline = newShoreline;
    }

    shorelines->erase(std::remove_if(shorelines->begin(), shorelines->end(), [](auto& s) { return s.empty(); }), shorelines->end());
}