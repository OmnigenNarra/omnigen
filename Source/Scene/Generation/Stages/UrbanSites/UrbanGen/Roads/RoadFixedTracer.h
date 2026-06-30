#pragma once
#include "Utils/Polygon.h"

struct RoadFixedTracerConfig
{
    // General config //
    // Size in units of the trace step
    float stepSize = 100.f;

    // If true, vertices during radial query with a height difference bellow maxIgnoredHeightDiff will be skipped and the road will continue straight
    bool filterSimilarHeights = true;
    float maxIgnoredHeightDiff = 30.f;

    // If true, base path vertices with a slope over maxAllowedSlope will be marked and stored at maxSlopeVertices
    bool useMaxSlope = true;
    float maxAllowedSlope = 0.17f;

    // If true, vertices that cross env bounds will be marked and stored at envBoundVertices
    bool queryForEnvBounds = true;

    // Radial trace config //
    // The number of points retrieved with each radial query
    int sampleSize = 12;
    // The radius of the radial query (360.f represents a full wrap around the original point)
    float queryDegrees = 75.f;

    // Optional bounds to keep
    std::optional<Polygon2D> bounds = {};

    RoadFixedTracerConfig() = default;
};

struct RoadFixedTracerResult
{
    std::vector<QVector3D> path;

    std::vector<std::pair<IndexType, IndexType>> maxSlopeVertices;
    std::vector<std::pair<IndexType, IndexType>> envBoundVertices;

    RoadFixedTracerResult() = default;
};

/**
 * Performs a fixed step trace for the best possible road path within a certain deviation.
 */
class RoadFixedTracer
{
public:
    RoadFixedTracer() = default;

    RoadFixedTracer(const RoadFixedTracerConfig& newConfig) : config(newConfig) {}

    // Trace a new road from source to target based on the tracer's config
    [[nodiscard]] RoadFixedTracerResult traceRoad(const QVector3D& source, const QVector3D& target, const std::optional<Polygon2D>& bounds) const;

    QVector3D getBestNearbyPoint(const std::vector<QVector3D>& pts, const std::vector<QVector3D>& currentPath) const;

    std::vector<QVector3D> queryNearbyPoints(const QVector3D& fromPt, const std::vector<QVector3D>& currentPath, const QVector3D& straightDir = {}) const;

    bool isWithinMaxSlope(const QVector3D& from, const QVector3D& seed) const;
    bool isCrossingEnvBounds(const QVector3D& from, const QVector3D& seed) const;

    bool hasSignificantHeight(const QVector3D& seed, const float targetHeight) const;

    RoadFixedTracerConfig config = {};
    mutable std::optional<Polygon2D> optBounds = {};
};
