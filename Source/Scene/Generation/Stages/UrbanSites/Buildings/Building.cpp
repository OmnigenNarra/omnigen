#include "stdafx.h"
#include "Building.h"

#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"

Building::Building(const int id, const QVector3D& inOriginPt, const BuildingData& inSizes)
    : sizes(inSizes), districtId(id), originPt(inOriginPt)
{
    bounds = BuildingSizes::getBoundsFromData(originPt, inSizes);
}

Building::Building(const int id, const QVector3D& inOriginPt, const Polygon2D& bbBounds, const float height)
    : bounds(bbBounds), districtId(id), originPt(inOriginPt)
{
    sizes = BuildingData{ Segment2D(bbBounds[0], bbBounds[1]).length(),
    Segment2D(bbBounds[1], bbBounds[2]).length(), height };
}

void Building::drawWireframe(const QVector4D& inColor)
{
    auto height = [](const QVector3D& pt) -> QVector3D
    {
        const auto pts = Generation::Utils::castPointTo3D(pt);
        return pts.empty() ? pt : pts[0];
    };

    auto toHeight = [](const QVector3D& pt, const float h) -> QVector3D
    {
        return { pt.x(), pt.y() + h, pt.z() };
    };

    auto&& pts = bounds.getPts();
    if (pts.size() < 4)
        return;

    for (auto&& seg : bounds.getPtsAsSegments())
    {
        markerLines.push_back({ height(seg.first), height(seg.second) });
    }

    std::vector<Segment2D> topSegments;
    for (auto&& pt : bounds)
    {
        markerLines.push_back({ height(pt), toHeight(pt, sizes.height) });
    }

    for (auto&& seg : bounds.getPtsAsSegments())
    {
        markerLines.push_back({ toHeight(seg.first, sizes.height), toHeight(seg.second, sizes.height) });
    }

    for (auto i = 0; i < markerLines.size(); i++)
    {
        auto&& line = markerLines[i];
        const auto marker = Generation::Data::get()->createMarker<DLineMarker>(line,
            i <= 3 ? Colors::white : inColor);

        markerIds << marker->getGuid();
    }

    /*computeForwardVector();

    const QVector3D start = height(forwardSegment.first);
    const QVector3D end = height(forwardSegment.second);*/

    /*const auto marker = Generation::Data::get()->createMarker<DLineMarker>(
        start, end, Colors::red, sizes.height / 2.f, ELineDecorator::Arrow);*/

    //markerIds << marker->getGuid();
}

void Building::computeForwardVector()
{
    const float extent = Segment2D(markerLines[1][0], markerLines[1][1]).length() / 2.f;
    const auto center = bounds.getCenter();

    const auto &line = markerLines[0];
    const Segment2D newSeg = Segment2D(line[0], line[1]);
    const auto midpoint = newSeg.midpoint();
    const GVector2D initDir = (newSeg.second - newSeg.first).normalized();

    const auto leftSegment = Segment2D(midpoint, midpoint + GVector2D(extent * initDir.rotatedLeft90()));
    const auto rightSegment = Segment2D(midpoint, midpoint + GVector2D(extent * initDir.rotatedRight90()));

    forwardSegment = distance(center, leftSegment.second) > distance(center, rightSegment.second) ? leftSegment : rightSegment;
    forwardVector = (forwardSegment.second - forwardSegment.first).normalized();
}

void Building::clearWireframe()
{
    for (auto&& id : markerIds)
        Generation::Data::get()->clearSingleExactMarker<DLineMarker>(id);

    markerIds.clear();
}

std::vector<Segment2D> Building::getBoundSegments() const
{
    if (markerLines.size() < 4)
        return {};

    std::vector<Segment2D> segments;
    for (auto i = 0; i < 4; i++)
    {
        auto&& line = markerLines[i];
        segments << Segment2D(line[0], line[1]);
    }

    return segments;
}

BuildingData BuildingSizes::getRandomSize()
{
    return sizes[dist(eng)];
}

Polygon2D BuildingSizes::getBoundsFromData(const GVector2D& originPt, const BuildingData& data)
{
    const auto frontEdgeMid = originPt + GVector2D(GVector2D{ 1.f, 0.f } * (data.width * 0.5f));
    const auto backEdgeMid = originPt + GVector2D(GVector2D{ -1.f, 0.f } * (data.width * 0.5f));

    std::vector<GVector2D> polyPts;
    polyPts << frontEdgeMid + GVector2D(GVector2D{ 0.f, 1.f } * (data.length * 0.5f));
    polyPts << frontEdgeMid + GVector2D(GVector2D{ 0.f, -1.f } * (data.length * 0.5f));
    polyPts << backEdgeMid + GVector2D(GVector2D{ 0.f, 1.f } * (data.length * 0.5f));
    polyPts << backEdgeMid + GVector2D(GVector2D{ 0.f, -1.f } * (data.length * 0.5f));

    return { polyPts };
}

BuildingData BuildingSizes::getSmallestAreaSize()
{
    float area = std::numeric_limits<float>::max();
    BuildingData chosenSize;

    for (auto&& size : sizes)
    {
        if (const auto a = (size.width * size.length); a < area)
        {
            area = a;
            chosenSize = size;
        }
    }

    return chosenSize;
}
