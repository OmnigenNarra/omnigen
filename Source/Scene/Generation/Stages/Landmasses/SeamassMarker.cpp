#include "stdafx.h"
#include "SeamassMarker.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/StageTools/Landmasses/LandmassSelection.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineUtils.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"

void DSeamassMarker::generateSeamassMarkers(std::vector<QSharedPointer<DLandmassMarker>> landmasses)
{
    OmniProfile("Generating seamass markers");

    QSet<GPoint> waters = Generation::Data::get()->getAllSquares<EDomainType::Water>();

    std::vector<std::vector<QVector3D>> seamassesPolygons;
    std::vector<std::vector<QVector3D>> landmassesPolygonsInsideSeamasses;

    std::vector<QSharedPointer<DShorelineMarker>> allCoastShorelines;

    for (auto&& landmass : landmasses)
        if (landmass->isCoast())
            landmass->forEachShoreline([&](auto& s, bool isInner)
                {
                    if (!isInner) allCoastShorelines << s;
                    else seamassesPolygons << s->getControlPoints();
                });
        else
            landmassesPolygonsInsideSeamasses << landmass->getMainPolygon();

    auto hasLandmass = [&](const QSet<GPoint>& watersSet)
    {
        for(auto&& landmass : landmasses)
            if (landmass->isCoast())
            {
                auto landSquaresSets = ShorelineUtils::splitSeparateSet(landmass->findSeasideDomainSquares());

                if (std::any_of(landSquaresSets.begin(), landSquaresSets.end(), [&](auto& s) {return watersSet.contains(*s.begin()); }))
                    return true;

                for (auto&& shoreline : landmass->getInnerSeaShorelines())
                    if (watersSet.contains(*shoreline->getSquares().begin()))
                        return true;
            }

        return false;
    };

    auto&& watersSets = ShorelineUtils::splitSeparateSet(waters);
    for (auto&& watersSet : watersSets)
    {
        std::vector<QSharedPointer<DShorelineMarker>> shorelinesOnWaterSet;
        for (auto&& shoreline : allCoastShorelines)
            if (watersSet.contains(*shoreline->getSquares().begin()))
                shorelinesOnWaterSet << shoreline;


        if (shorelinesOnWaterSet.empty())
        {
            if (!hasLandmass(watersSet))
                seamassesPolygons << PolygonUtils::calculatePolygonsFromGridSquares(watersSet);

            continue;
        }

        auto&& [seamassPolygon, notUsedShorelines] = createSeamassPolygon(shorelinesOnWaterSet);
        seamassesPolygons << seamassPolygon;

        while (true)
        {
            if (notUsedShorelines.empty())
                break;

            std::tie(seamassPolygon, notUsedShorelines) = createSeamassPolygon(notUsedShorelines);
            seamassesPolygons << seamassPolygon;
        }
    }

    for (auto&& seamassPolygon : seamassesPolygons)
    {
        std::vector<std::vector<QVector3D>> holesInSeamass;
        for (auto&& landmassPolygon : landmassesPolygonsInsideSeamasses)
            if (auto&& [inside, _, dist] = ((GVector2D)landmassPolygon.front()).isInsidePolygon(seamassPolygon); inside || dist < 1.0f)
                holesInSeamass << landmassPolygon;

        spawn<DSeamassMarker>(seamassPolygon, holesInSeamass, 0, DDomain::Colors[EDomainType::Water]);
    }
}

std::tuple<std::vector<QVector3D>, std::vector<QSharedPointer<DShorelineMarker>>> DSeamassMarker::createSeamassPolygon(const std::vector<QSharedPointer<DShorelineMarker>>& shorelines)
{
    Polygon2D watermassNoShorelinePolygon;
    {
        QSet<GPoint> allWaterSquares = Generation::Data::get()->getAllSquares<EDomainType::Water>();
        auto waterSquareSets = ShorelineUtils::splitSeparateSet(allWaterSquares);

        QSet<GPoint> correctWaterSquares;
        if (!shorelines.empty())
            correctWaterSquares = *std::find_if(waterSquareSets.begin(), waterSquareSets.end(), [&](auto& a) { return a.contains(*shorelines.front()->getSquares().begin()); });
        else
            correctWaterSquares = waterSquareSets.front();

        auto polys = PolygonUtils::calculatePolygonsFromGridSquares(correctWaterSquares);
        watermassNoShorelinePolygon = *std::max_element(polys.begin(), polys.end(), [](auto& p1, auto& p2) { return PolygonUtils::calculateArea(p1) < PolygonUtils::calculateArea(p2); });
    }
    if (!watermassNoShorelinePolygon.isCW())
        watermassNoShorelinePolygon.reverseOrder();

    std::vector<QVector3D> watermassPolygon;

    std::vector<GVector2D> waterWithShorelinesPts;

    std::vector<GVector2D> shorelinesPts;
    for (auto&& shoreline : shorelines)
        shorelinesPts << shoreline->getControlPoints().front() << shoreline->getControlPoints().back();



    auto&& landPts = watermassNoShorelinePolygon.getPts();
    auto&& landCPts = asCircular(landPts);


    for (int i = 0; i < landPts.size(); i++)
    {
        waterWithShorelinesPts << landPts[i];

        Segment2D waterSegment(landPts[i], landPts[landCPts.findIdx(i, 1)]);

        std::map<float, GVector2D> shorelinePts;
        for (auto&& shorelinePt : shorelinesPts)
            if (waterSegment.dist(shorelinePt) <= 1.0f)
                shorelinePts[waterSegment.first.dist(shorelinePt)] = shorelinePt;

        for (auto&& [dist, pt] : shorelinePts)
        {
            if (pt != landPts[i] && pt != landPts[landCPts.findIdx(i, 1)])
                waterWithShorelinesPts << pt;

            shorelinesPts.erase(std::remove(shorelinesPts.begin(), shorelinesPts.end(), pt), shorelinesPts.end());
        }
    }

    Q_ASSERT(shorelinesPts.empty());


    auto&& pts = waterWithShorelinesPts;
    auto&& cPts = asCircular(waterWithShorelinesPts);

    auto findShorelineIdOnPolygonSegment = [&](int idx) -> std::optional<int>
    {
        for (int i = 0; i < shorelines.size(); i++)
            if (pts[idx] == GVector2D(shorelines[i]->getControlPoints().front()))
                return i;

        return {};
    };

    auto addPathToPoly = [&](const std::vector<QVector3D>& path)
    {
        watermassPolygon.insert(watermassPolygon.end(), path.begin() + 1, path.end() - 1);

        for (int i = 0; i < pts.size(); i++)
            if (pts[i] == GVector2D(path.back()))
                return i;

        // error
        return -1;
    };

    std::set<int> usedShorelines{ 0 };
    int nextIdx = 0;

    if (!shorelines.empty())
    {
        auto path = shorelines.front()->getControlPoints();
        watermassPolygon << path.front();
        nextIdx = addPathToPoly(path);
    }
    else
    {
        watermassPolygon << pts[nextIdx];
        nextIdx = cPts.findIdx(nextIdx, 1);
    }

    while (true)
    {
        if (pts[nextIdx] == (GVector2D)watermassPolygon.front())
            break;

        watermassPolygon << pts[nextIdx];

        if (auto nextShoreline = findShorelineIdOnPolygonSegment(nextIdx); nextShoreline)
        {
            Q_ASSERT(!usedShorelines.contains(*nextShoreline));
            usedShorelines.insert(*nextShoreline);

            nextIdx = addPathToPoly(shorelines[*nextShoreline]->getControlPoints());
        }
        else
            nextIdx = cPts.findIdx(nextIdx, 1);
    }

    std::vector<QSharedPointer<DShorelineMarker>> notUsedShorelines;
    for (int i = 0; i < shorelines.size(); i++)
        if (!usedShorelines.contains(i))
            notUsedShorelines << shorelines[i];

    return { watermassPolygon, notUsedShorelines };
}

void omniSave(const DSeamassMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DPolygonWithHolesMarker&>(object);
}

void omniLoad(DSeamassMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DPolygonWithHolesMarker&>(object);
}
