#pragma once
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/Polygon.h"

class ShorelineUtils final
{
public:
    template <typename T>
    static T randomWeightedChoice(const std::map<T, float>& choices)
    {
        static std::uniform_real_distribution<float> distribution(0, 1);

        float accumulatedWeight = std::accumulate(choices.begin(), choices.end(), 0.0f, [](auto acc, auto& v) { return acc + v.second; });

        float weightThreshold = distribution(Generation::gRandomEngine) * accumulatedWeight;
        float currentWeight = 0;

        for (auto&& [choice, weight] : choices)
        {
            currentWeight += weight;
            if (weightThreshold < currentWeight)
            {
                return choice;
            }
        }

        return choices.rbegin()->first;
    }

    // Get coverage of set s, starting with point p (without point excludedP).
    static QSet<GPoint> coverage(const QSet<GPoint>& s, const GPoint& p, const GPoint& excludedP = GPoint(-1, -1));

    // Split set of points if they are not directly connected with each other, NOTICE: intentional copy of qset
    static std::vector<QSet<GPoint>> splitSeparateSet(QSet<GPoint> set);

    // Split set of GPoints into set of squares containing same space with higher detail, i.e. detail = 3 will make 1 sq => 9 sq
    static std::set<std::pair<int, int>> splitSetIntoSquares(const QSet<GPoint>& set, int detail);

    static bool isNeighboring(const QSet<GPoint>& sourceSet, const QSet<GPoint>& neighborSet);

    // Find whenever 2 sets contains only one neighboring square, and if so, return one from source
    static std::optional<GPoint> findOnlyNeighboringSquare(const QSet<GPoint>& sourceSet, const QSet<GPoint>& neighborSet);

    // Find point in center of set
    static GPoint findMidpoint(const QSet<GPoint>& set);

    static std::pair<QSet<GPoint>, QSet<GPoint>> splitSetByVector(const QSet<GPoint>& set, const GVector2D& vector, std::optional<GVector2D> vectorPoint = std::nullopt);

    // Find shorelines paths along landmass polygon
    static std::tuple<std::vector<std::vector<QVector3D>>, bool> findShorelinesAlongLandmass(const std::vector<QVector3D>& landmass, const std::vector<std::vector<QVector3D>>& allSeasidePolygons);

    // Reduce distance between points of path 
    static void reduceDistanceBetweenPoints(std::vector<std::vector<QVector3D>>* shorelines, float distanceBetweenPoints, bool isCoast, std::optional<float> removeThreshold = std::nullopt);
};