#include "stdafx.h"
#include "DistrictNetwork.h"

#include "Omnigen.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/UrbanSite.h"

DistrictNetwork::DistrictNetwork(NetworkTopologyData data, bool perimeterEdges)
    : sizeData(std::move(data)), computePerimeterEdges(perimeterEdges)
{
    areaBoundingBox = sizeData.urbanSiteArea.getEnclosingBB();

    const int xDomainSize = static_cast<int>((areaBoundingBox.sizes.x() / GRID_SEGMENT_WIDTH) * pointDensity);
    const int zDomainSize = static_cast<int>((areaBoundingBox.sizes.z() / GRID_SEGMENT_WIDTH) * pointDensity);

    networkSize = QSize(xDomainSize, zDomainSize);
    center = findInitialSeed();

    auto districtCenters = calculateNonCentralSeeds();
    districtCenters.insert(districtCenters.begin(), center);

    districts.reserve(districtCenters.size());
    diagram = Voronoi::PolygonDiagram(districtCenters, Polygon2D::findConvexHull(sizeData.urbanSiteArea.getPts()));
}

void DistrictNetwork::showNetwork(bool drawCenters) const
{
    if (drawCenters)
    {
        diagram.drawCenters({ 1.f, 1.f, 0.f, 1.f });
    }

    diagram.drawCells({ 1.f, 0.f, 1.f, 1.f });

    diagram.drawCellControlPoints();
}

std::vector<Segment2D> DistrictNetwork::getPrimaryRoadEdges() const
{
    OmniProfile("Road Edge Gathering");

    std::vector<std::pair<Segment2D, GVector2D>> edges;

    for (auto&& cell : diagram)
    {
        auto&& snappedCells = Polygon2D::boolOp(cell.getPolygon(), sizeData.urbanSiteArea,
            Polygon2D::EBoolOp::Intersection);

        if (snappedCells.empty())
            continue;

        for (auto&& snappedCell : snappedCells)
        {
            auto cPts = snappedCell.getCPts();
            for (auto i = 0; i < snappedCell.getPts().size(); i++)
            {
                auto i2 = cPts.findIdx(i, 1);

                /*if (!computePerimeterEdges)
                {
                    if (sizeData.urbanSiteArea.findPointOnIndexedEdge(snappedCell[i])
                        && sizeData.urbanSiteArea.findPointOnIndexedEdge(snappedCell[i2]))
                        continue;
                }*/

                edges.push_back(std::make_pair(Segment2D{ snappedCell[i], snappedCell[i2] }, cell->getCenter()));
            }
        }
    }

    std::vector<Segment2D> pEdges;

    for (auto& key : edges | std::views::keys)
        pEdges << key;

    return pEdges;
}

void DistrictNetwork::calculateDistricts(const std::vector<DistrictCreationInfo>& info)
{
    for (auto&& districtInfo : info)
    {
        auto&& districtCenter = diagram.getCenters()[districtInfo.id];

        auto newDistrict = District(districtCenter == center ? true : false, districtCenter, districtInfo.id, this);
        newDistrict.setBounds(districtInfo.roadBounds);

        districts.emplace_back(newDistrict);
    }

    auto&& roadPatterns = getDistrictsRoadPatterns(sizeData.urbanSiteSize);
    for (auto i = 0; i < districts.size(); i++)
    {
        auto&& district = districts[i];
        district.calculateRoadDiagram(roadPatterns[i]);
    }
}

GVector2D DistrictNetwork::findInitialSeed(bool bUseZAxis /* = false */) const
{
    std::mt19937 eng(Generation::gRandomEngine);

    const BoundingBox testBox = findBestBoxForInitialSeed();

    GVector2D bottomLeft;
    GVector2D topLeft;
    GVector2D topRight;
    GVector2D bottomRight;

    auto getBoundingBoxSubBox = [&](float scale) 
    {
        const auto scaledBoundingSize = testBox.sizes * scale;
        const auto scaledBoundingNbl = testBox.nbl + (testBox.sizes - scaledBoundingSize) / 2;

        bottomLeft = { scaledBoundingNbl.x(), scaledBoundingNbl.z() };
        topLeft = { scaledBoundingNbl.x(), scaledBoundingNbl.z() + scaledBoundingSize.z() };
        topRight = { scaledBoundingNbl.x() + scaledBoundingSize.x(), scaledBoundingNbl.z() + scaledBoundingSize.z() };
        bottomRight = { scaledBoundingNbl.x() + scaledBoundingSize.x(), scaledBoundingNbl.z() };

        const std::vector<GVector2D> quadVertices = { bottomLeft, topLeft, topRight, bottomRight };
        return sizeData.urbanSiteArea.containsAny(quadVertices);
    };

    for (float i = 0.1f; i <= 1.0f; i += 0.1f)
    {
        if (getBoundingBoxSubBox(i))
            break;
    }

    GVector2D seed;

    while (true)
    {
        std::uniform_real_distribution<float> xDistribution(bottomLeft.x, topRight.x);
        std::uniform_real_distribution<float> zDistribution(bottomLeft.z, topRight.z);

        seed = GVector2D(xDistribution(eng), zDistribution(eng));
        if (sizeData.urbanSiteArea.contains(seed))
            break;
    }

    return seed;
}

BoundingBox DistrictNetwork::findBestBoxForInitialSeed() const
{
    return areaBoundingBox;
}

int DistrictNetwork::getDefaultNumberOfDistricts(const EUrbanSize& urbanSize) const
{
    switch (urbanSize)
    {
        case EUrbanSize::Outpost :
            return 1;
        case EUrbanSize::Village :
            return 5;
        case EUrbanSize::Town :
            return 10;
        case EUrbanSize::LargeTown :
            return 14;
        case EUrbanSize::HugeTown :
            return 18;
        default:
            return 1;
    }
}

std::vector<GVector2D> DistrictNetwork::calculateNonCentralSeeds() const
{
    // TODO: Use config for number of seeds + rings
    const int numberOfDistricts = getDefaultNumberOfDistricts(sizeData.urbanSiteSize);
    const int numberOfRings = numberOfDistricts < 10 ? 1 : 2;

    return getSeedsInRingsDistribution(sizeData.urbanSiteArea, center, numberOfDistricts, numberOfRings);
}

std::vector<GVector2D> DistrictNetwork::getSeedsInRingsDistribution(const Polygon2D& area, const GVector2D& center, const int numberOfSeeds, const int numberOfRings) const
{
    if (numberOfRings <= 0)
        return {};

    std::vector<GVector2D> seeds;

    std::mt19937 eng(Generation::gRandomEngine);

    const int numberOfSeedsPerRing = numberOfSeeds / numberOfRings;
    const int numberOfSeedsInFinalRing = numberOfSeedsPerRing + numberOfSeeds % numberOfRings;

    const float distanceStep = 1 / (float)(numberOfRings + 1);
    float distanceFromCenter = 0;

    for (int ring = 1; ring <= numberOfRings; ring++)
    {
        std::vector<GVector2D> seedsInRing;

        const int numberOfSeedsInRing = ring != numberOfRings ? numberOfSeedsPerRing : numberOfSeedsInFinalRing;

        distanceFromCenter += distanceStep;
        std::uniform_real_distribution<float> distanceFromCenterDistribution(distanceFromCenter, distanceFromCenter + distanceStep);

        const float angleDiffStep = 360.f / numberOfSeedsInRing;
        float angleDiff = 0.0f;

        while (true)
        {
            if (int(seedsInRing.size()) >= numberOfSeedsInRing)
                break;

            std::uniform_real_distribution<float> angleDiffDistribution(angleDiff, angleDiff + angleDiffStep);

            auto intersections = area.rayIntersections(Segment2D(center, center + GVector2D::rotateDegrees({ 10'000,10'000 }, angleDiffDistribution(eng))));
            if (intersections.isEmpty())
                continue;

            GVector2D newPoint = center + ((std::get<GVector2D>(intersections.first()) - center) * distanceFromCenterDistribution(eng));

            if (area.containsConcave(newPoint))
                seedsInRing.push_back(newPoint);

            angleDiff += angleDiffStep;
        }

        seeds.insert(seeds.end(), seedsInRing.begin(), seedsInRing.end());
    }

    return seeds;
}

std::vector<ERoadPattern> DistrictNetwork::getDistrictsRoadPatterns(const EUrbanSize& urbanSize) const
{
    std::mt19937& eng = Generation::gRandomEngine;
    std::bernoulli_distribution radialCenter(0.5f);

    auto getVillageRoadPatterns = [&]()
    {
        std::vector<ERoadPattern> roadPatterns;

        for (const auto& district : districts)
        {
            if (district.getIsTownCenter() && radialCenter(eng))
                roadPatterns.push_back(ERoadPattern::Radial);
            else
                roadPatterns.push_back(ERoadPattern::Organic);
        }

        return roadPatterns;
    };

    auto getTownRoadPatterns = [&]()
    {
        std::vector<ERoadPattern> roadPatterns;

        std::bernoulli_distribution gridTypeDistribution(0.33f);

        for (const auto& district : districts)
        {
            if (district.getIsTownCenter() && radialCenter(eng))
                roadPatterns.push_back(ERoadPattern::Radial);
            else if (gridTypeDistribution(eng))
                roadPatterns.push_back(ERoadPattern::CircularGrid);
            else
                roadPatterns.push_back(ERoadPattern::Grid);
        }

        return roadPatterns;
    };

    switch (urbanSize)
    {
            using enum EUrbanSize;
        case Outpost :
        case Village :
            return getVillageRoadPatterns();

        case Town :
        case LargeTown :
        case HugeTown :
            return getTownRoadPatterns();

        default:
            return std::vector<ERoadPattern>(districts.size(), ERoadPattern::Organic);
    }
}
