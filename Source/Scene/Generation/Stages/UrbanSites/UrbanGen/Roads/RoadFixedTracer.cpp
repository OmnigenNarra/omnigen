#include "stdafx.h"
#include "RoadFixedTracer.h"

#include "Scene/Generation/Stages/UrbanLayout/UrbanUtils.h"

RoadFixedTracerResult RoadFixedTracer::traceRoad(const QVector3D& source, const QVector3D& target, const std::optional<Polygon2D>& bounds) const
{
    RoadFixedTracerResult resultData;

    resultData.path << UrbanUtils::heightQuery(source);

    if (bounds.has_value())
        optBounds = *bounds;

    while (true)
    {
        if (resultData.path.size() > 3000)
            break;

        auto&& lastPoint = resultData.path.back();

        auto newSeeds = queryNearbyPoints(lastPoint, resultData.path,
            (target - lastPoint).normalized());

        if (newSeeds.empty())
            break;

        const auto nextSeed = getBestNearbyPoint(newSeeds, resultData.path);
        if (distance(static_cast<GVector2D>(nextSeed), static_cast<GVector2D>(target)) < (config.stepSize * 2.f))
            break;

        if (config.useMaxSlope)
        {
            if (!resultData.path.empty() && !isWithinMaxSlope(lastPoint, nextSeed))
            {
                // Mark
                if (!resultData.maxSlopeVertices.empty() && resultData.maxSlopeVertices.back().second == resultData.path.size() - 1)
                    resultData.maxSlopeVertices.back().second = resultData.path.size();
                else
                    resultData.maxSlopeVertices.push_back(std::pair(IndexType(resultData.path.size() - 1), IndexType(resultData.path.size())));
            }
        }

        if (config.queryForEnvBounds)
        {
            if (isCrossingEnvBounds(lastPoint, nextSeed))
            {
                resultData.envBoundVertices << std::pair(IndexType(resultData.path.size() - 1), IndexType(resultData.path.size()));
            }
        }

        resultData.path << nextSeed;
    }

    resultData.path << UrbanUtils::heightQuery(target);

    optBounds.reset();

    return resultData;
}

QVector3D RoadFixedTracer::getBestNearbyPoint(const std::vector<QVector3D>& pts, const std::vector<QVector3D>& currentPath) const
{
    Q_ASSERT(!pts.empty());

    if (pts.size() == 1)
        return pts[0];

    auto&& lastPoint = currentPath.empty() ? currentPath.front() : currentPath.back();
    const auto offsetV = (currentPath.back() - lastPoint).normalized() * config.stepSize;
    const auto midPoint = lastPoint + offsetV;

    std::map<float, int> heightMap;
    for (auto i = 0; i < pts.size(); i++)
    {
        // Calculate desired elevation
        const float elevation = qAbs((pts[i]).y() / offsetV.length() - currentPath.back().y()
            / (currentPath.back() - midPoint).length());

        heightMap[elevation] = i;
    }

    return pts[heightMap.begin()->second];
}

std::vector<QVector3D> RoadFixedTracer::queryNearbyPoints(const QVector3D& fromPt, const std::vector<QVector3D>& currentPath, const QVector3D& straightDir) const
{
    std::vector<QVector3D> potentialPoints;

    const auto dir = straightDir.isNull() ? (currentPath.back() - fromPt).normalized() : straightDir;
    const auto offsetV = dir * config.stepSize;
    const auto straightPt = UrbanUtils::heightQuery(fromPt + offsetV, UrbanUtils::getPointHeightAverage(currentPath));

    if (currentPath.empty())
        return { straightPt };

    potentialPoints << straightPt;

    const float deviation = config.queryDegrees;
    const float decrement = (deviation * 2.f) / config.sampleSize;

    const auto lineVec = straightPt - fromPt;
    float currentDegrees = deviation;

    for (int i = 0; i < config.sampleSize - 1; i++)
    {
        const auto rotatedVector = GVector2D::rotateDegrees(lineVec.normalized(), currentDegrees);
        const GVector2D newOffset = rotatedVector * offsetV.length();
        const auto newPoint = UrbanUtils::heightQuery(fromPt + newOffset, UrbanUtils::getPointHeightAverage(currentPath));

        if (optBounds.has_value())
            if (!optBounds->containsConcave(newPoint))
                continue;

        if (!config.filterSimilarHeights)
            potentialPoints << newPoint;
        else
            if (hasSignificantHeight(newPoint, straightPt.y()))
                potentialPoints << newPoint;

        currentDegrees -= decrement;

        if (currentDegrees < -deviation)
            break;
    }

    if (potentialPoints.empty())
        return { straightPt };
    
    return potentialPoints;
}

bool RoadFixedTracer::isWithinMaxSlope(const QVector3D& from, const QVector3D& seed) const
{
    const float heightDelta = abs(from.y() - seed.y());
    const float slope = heightDelta / config.stepSize;

    return slope <= config.maxAllowedSlope;
}

bool RoadFixedTracer::isCrossingEnvBounds(const QVector3D& from, const QVector3D& seed) const
{
    return false;
}

bool RoadFixedTracer::hasSignificantHeight(const QVector3D& seed, const float targetHeight) const
{
    if (seed.y() != targetHeight)
        if (abs(seed.y() - targetHeight) < config.maxIgnoredHeightDiff)
            return false;

    return true;
}
