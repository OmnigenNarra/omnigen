#include "stdafx.h"
#include "District.h"

#include "DistrictNetwork.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"

#include "Utils/Clipper/clipper.hpp"

District::District(const bool isTownCenter, const GVector2D& inCenter, const int inId, DistrictNetwork* inParent)
    : center(inCenter), isTownCenter(isTownCenter), id(inId), parent(inParent)
{
    auto&& cell = parent->getDiagram()[inId];

    for (auto it = cell.getNeighbors().keyBegin(); it != cell.getNeighbors().keyEnd(); ++it)
    {
        neighbors.push_back(*it);
    }

    bounds = cell.getPolygon();
    mapCenter = cell.getVoronoiCenter();
}

void District::calculateRoadDiagram(const ERoadPattern& withPattern)
{
    const auto seeds = gatherRoadSeeds(withPattern);
    if (!seeds)
        return;

    dominantRoadPattern = withPattern;

    diagram = Voronoi::BoxDiagram(*seeds, bounds.getEnclosingBB());
}

//TODO: REFACTOR
std::optional<std::vector<GVector2D>> District::gatherRoadSeeds(const ERoadPattern& withPattern)
{
    std::vector<GVector2D> seeds;
    auto&& voronoiCell = parent->getDiagram()[id];

    seedsBounds = Polygon2D::inflatePolygonByScale(bounds, 0.9f);

    auto filterNewSeeds = [&](const std::vector<GVector2D>& newSeeds)
    {
        if (seeds.empty())
        {
            for (auto&& seed : newSeeds)
            {
                if (voronoiCell.getPolygon().containsConcave(seed))
                    seeds.push_back(seed);
            }

            return;
        }

        for (auto&& seed : seeds)
            for (auto&& newSeed : newSeeds)
                if (distance(seed, newSeed) >= minimumSeedDistance)
                {
                    if (voronoiCell.getPolygon().contains(newSeed) && seedsBounds.containsConcave(seed))
                        seeds.push_back(newSeed);
                }
    };

    switch (withPattern)
    {
    case ERoadPattern::Grid:
        filterNewSeeds(getGridSeeds(parent->getCenter(), { 900, 1500 }, 0.05f));
        break;
    case ERoadPattern::CircularGrid:
        filterNewSeeds(getCircularGridSeeds(parent->getCenter(), { 900, 1500 }, 
            6, { 900, 1200 }, 0.05f));
        break;
    case ERoadPattern::Radial:
        filterNewSeeds(getRadialSeeds(1500, true));
        break;
    case ERoadPattern::Organic:
        filterNewSeeds(getOrganicSeeds(6));
        break;
    }

    if (seeds.size() < 2)
        return {};

    return std::move(seeds);
}

std::vector<GVector2D> District::getGridSeeds(const GVector2D& rotateTowards, const std::pair<float, float>& densityMinMax, float deviationChance) const
{
    std::vector<GVector2D> internalSeeds;

    auto&& enclosingBB = seedsBounds.getEnclosingBB();

    float minZ = enclosingBB.nbl.z() - enclosingBB.sizes.z() * 1.5f;
    float minX = enclosingBB.nbl.x() - enclosingBB.sizes.x() * 1.5f;
    float maxZ = enclosingBB.nbl.z() + enclosingBB.sizes.z() * 1.5f;
    float maxX = enclosingBB.nbl.x() + enclosingBB.sizes.x() * 1.5f;

    GVector2D directionToPoint = (center - rotateTowards).normalized();
    float angleToPoint = GVector2D{ 0, -1 }.angle(directionToPoint);

    GVector2D currentPos{ minX, minZ };

    std::mt19937& eng = Generation::gRandomEngine;

    while (true)
    {
        std::uniform_real_distribution<float> gridDistribution(densityMinMax.first, densityMinMax.second);
        std::bernoulli_distribution gridDeviation(deviationChance);

        float densityZ = gridDistribution(eng);
        float densityX = densityMinMax.first;

        if (currentPos.z > maxZ)
            break;

        while (currentPos.x < maxX)
        {
            if (gridDeviation(eng))
            {
                currentPos.x += densityX;
                continue;
            }

            internalSeeds.emplace_back(center + GVector2D::rotateDegrees({ center - currentPos }, angleToPoint));
            currentPos.x += densityX;
        }
        currentPos.x = minX;

        currentPos.z += densityZ;
    }

    return internalSeeds;
}

std::vector<GVector2D> District::getCircularGridSeeds(const GVector2D& circleCenter, const std::pair<float, float>& radiusDensityMinMax, const float angleDiff,
    const std::pair<float, float>& circuitDensityMinMax, float deviationChance) const
{
    std::vector<GVector2D> internalSeeds;

    auto&& enclosingBB = seedsBounds.getEnclosingBB();

    float minZ = enclosingBB.nbl.z();
    float minX = enclosingBB.nbl.x();
    float maxZ = enclosingBB.nbl.z() + enclosingBB.sizes.z();
    float maxX = enclosingBB.nbl.x() + enclosingBB.sizes.x();

    GVector2D midPoint{ minX + (maxX - minX) / 2, minZ + (maxZ - minZ) / 2 };

    float maxRadius = (circleCenter - midPoint).length() + std::max(std::abs(circleCenter.x - maxX), std::abs(circleCenter.z - maxZ));

    float currentRadius = radiusDensityMinMax.first;

    std::mt19937& eng = Generation::gRandomEngine;

    while (true)
    {
        std::bernoulli_distribution gridDeviation(deviationChance);

        if (currentRadius > maxRadius)
            break;

        int angleMultiplier = 1;

        float cosLawPower = 2 * currentRadius * currentRadius;
        float circuitEdgeLenght = std::sqrt(cosLawPower - cosLawPower * std::cos(angleDiff * std::_Pi / 180));

        while (circuitEdgeLenght * angleMultiplier < circuitDensityMinMax.first)
            angleMultiplier++;

        if (circuitEdgeLenght * angleMultiplier > circuitDensityMinMax.second)
            break;

        GVector2D currentPos{ circleCenter.x + currentRadius, circleCenter.z };
        float currentAngle = 0;

        while (currentAngle < 360.0f)
        {
            if (gridDeviation(eng))
            {
                currentAngle += angleDiff * angleMultiplier;;
                continue;
            }

            internalSeeds.emplace_back(circleCenter + GVector2D::rotateDegrees({ currentPos - circleCenter }, currentAngle));
            currentAngle += angleDiff * angleMultiplier;
        }

        std::uniform_real_distribution<float> radiusDistribution(radiusDensityMinMax.first, radiusDensityMinMax.second);
        currentRadius += radiusDistribution(eng);
    }

    return internalSeeds;
}

std::vector<GVector2D> District::getOrganicSeeds(const int numberOfSeeds /* = 8 */) const
{
    // Calculate the enclosing rectangle of the site
    float xMin = std::numeric_limits<float>::max();
    float xMax = -1.f;
    float zMin = std::numeric_limits<float>::max();
    float zMax = -1.f;

    for (auto&& pt : seedsBounds)
    {
        xMin = std::fmin(xMin, pt.x);
        xMax = std::fmax(xMax, pt.x);
        zMin = std::fmin(zMin, pt.z);
        zMax = std::fmax(zMax, pt.z);
    }


    float pointDensity = 150.0f;

    const int xDomainSize = static_cast<int>((xMax - xMin) * pointDensity);
    const int zDomainSize = static_cast<int>((zMax - zMin) * pointDensity);

    auto districtSize = QSize(xDomainSize, zDomainSize);

    std::vector<GVector2D> internalSeeds;
    internalSeeds.emplace_back(seedsBounds.getCenter());

    std::mt19937& eng = Generation::gRandomEngine;

    //TODO: Potential inf loop; bogoshort
    float seedDensity = 2000;
    int tries = 0;
    while (int(internalSeeds.size()) < numberOfSeeds - 1)
    {
        std::uniform_real_distribution<float> xDistribution((0 + (seedDensity - 1) / 2), (qAbs(districtSize.width() - seedDensity)));
        std::uniform_real_distribution<float> zDistribution((0 + (seedDensity - 1) / 2), (qAbs(districtSize.height() - seedDensity)));

        const GVector2D newPoint = GVector2D(
            (xDistribution(eng) / pointDensity + xMin),
            (zDistribution(eng) / pointDensity + zMin)
        );

        bool bIsValidPoint = true;

        for (const auto& point : diagram.getCenters())
        {
            if (qAbs(point.x - newPoint.x) < seedDensity || qAbs(point.z - newPoint.z) < seedDensity)
            {
                bIsValidPoint = false;
                break;
            }
        }

        if (!bIsValidPoint || !seedsBounds.contains(newPoint))
        {
            if (++tries > 60)
                break;

            continue;
        }

        internalSeeds.push_back(newPoint);
    }

    return internalSeeds;
}

std::vector<GVector2D> District::getRadialSeeds(const float radius, const bool withCenter, const int numberOfSeeds /* = 8 */) const
{
    std::vector<GVector2D> internalSeeds;
    if (withCenter)
        internalSeeds.emplace_back(seedsBounds.getCenter());

    GVector2D currentSeed = GVector2D(seedsBounds.getCenter().x + radius, seedsBounds.getCenter().z + radius);
    const float step = radius;

    GVector2D lineVec = { currentSeed - seedsBounds.getCenter() };
    GVector2D offset = { lineVec.normalized() * step };

    float increment = 360 / numberOfSeeds;

    float currentDegrees = increment;
    for (auto i = 0; i < numberOfSeeds; i++)
    {
        const auto rotatedVector = GVector2D::rotateDegrees(lineVec.normalized(), currentDegrees);
        const GVector2D newOffset = rotatedVector * offset.length();

        const GVector2D newPoint = seedsBounds.getCenter() + newOffset;

        internalSeeds.emplace_back(newPoint);
        currentDegrees += increment;
    }

    return internalSeeds;
}

std::vector<Segment2D> District::getRoadEdges() const
{
    std::vector<std::pair<Segment2D, GVector2D>> edges;

    for (auto&& cell : diagram)
    {
        auto&& snappedCells = Polygon2D::boolOp(cell.getPolygon(), bounds,
            Polygon2D::EBoolOp::Intersection);

        if (snappedCells.empty())
            continue;

        for (auto&& snappedCell : snappedCells)
        {
            auto cPts = snappedCell.getCPts();
            for (auto i = 0; i < snappedCell.getPts().size(); i++)
            {
                auto i2 = cPts.findIdx(i, 1);

                if (bounds.findPointOnIndexedEdge(snappedCell[i]) && bounds.findPointOnIndexedEdge(snappedCell[i2]))
                    continue;

                edges.push_back(std::make_pair(Segment2D{ snappedCell[i], snappedCell[i2] }, cell->getCenter()));
            }
        }
    }

    std::vector<Segment2D> edgesToReturn;

    for (auto& key : edges | std::views::keys)
        edgesToReturn << key;

    return edgesToReturn;
}

void District::showRoadDiagram() const
{
    diagram.drawCells();
    diagram.drawCenters(QVector4D(0, 0, 1, 1));
    diagram.drawCellControlPoints(QVector4D(1, 0, 1, 1));
}
