#include "stdafx.h"
#include "LandmassMarker.h"
#include "Editor/StageTools/Landmasses/LandmassSelection.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineUtils.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"


DLandmassMarker::DLandmassMarker(const std::vector<QSharedPointer<DShorelineMarker>>& inShorelines, const std::vector<QSharedPointer<DShorelineMarker>>& inInnerSeaShorelines)
    : DPolygonWithHolesMarker(createLandmassPolygon(inShorelines, &coast), createInnerSeamassPolygons(inInnerSeaShorelines), 0, DDomain::Colors[EDomainType::Terrain])
    , squares(findInsideSquares(mainPolygon, inShorelines, inInnerSeaShorelines))
    , shorelines(inShorelines)
    , innerSeaShorelines(inInnerSeaShorelines)
    , lock(false)
{
    makeName();
}

void DLandmassMarker::draw()
{
    setHovered(Design::LandmassSelection::isLandmassHovered(sharedFromThis()));

    DPolygonWithHolesMarker::draw();
}

void DLandmassMarker::setName(const QString& newName)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    name = newName;
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::setCoast(bool isCoast)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    coast = isCoast;
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::setSquares(const QSet<GPoint>& newSquares)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    squares = newSquares;
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::setShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& newShorelines)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    shorelines = newShorelines;
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::setInnerSeaShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& newInnerSeaShorelines)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    innerSeaShorelines = newInnerSeaShorelines;
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::setPolygons(const std::vector<QVector3D>& newMainPolygon, const std::vector<std::vector<QVector3D>>& newCutPolygons)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    mainPolygon = newMainPolygon;
    cutPolygons = newCutPolygons;
    updateGeometry();
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::setLocked(const bool& isLocked)
{
    lock = isLocked;
}

void DLandmassMarker::recalculateLandmassPolygons(bool updateVerts /*= true*/)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    mainPolygon = createLandmassPolygon(shorelines, &coast);
    cutPolygons = createInnerSeamassPolygons(innerSeaShorelines);
    squares = findInsideSquares(mainPolygon, shorelines, innerSeaShorelines);

    if (updateVerts)
        updateGeometry();
    emit Editable::modified(sharedFromThis());
}

void DLandmassMarker::updateGeometry()
{
    auto& vertices = getActiveGeometry()->vertices;
    auto& triangles = getActiveGeometry()->indices;

    vertices = mainPolygon;
    for (auto&& poly : cutPolygons)
        vertices.insert(vertices.end(), poly.begin(), poly.end());

    std::vector<std::vector<QVector3D>> polygonWithCuts{ mainPolygon };
    polygonWithCuts.insert(polygonWithCuts.end(), cutPolygons.begin(), cutPolygons.end());
    triangles = mapbox::earcut(polygonWithCuts);

    // Fix winding order
    for (int i = 0; i < triangles.size(); i += 3)
    {
        IndexType& i0 = triangles[i];
        IndexType i1 = triangles[i + 1];
        IndexType& i2 = triangles[i + 2];

        if (GVector2D::crossProduct(vertices[i0], vertices[i1], vertices[i2]) > 0.f)
            std::swap(i0, i2);
    }

    GVector2D center;
    for (auto&& p : vertices)
        center += p;
    center /= float(vertices.size());

    vertices.push_back(center);

    cacheBoundingBox();
}

bool DLandmassMarker::isInside(const GVector2D& point, bool ignoreInnerSeas, float minDistanceFromShoreline)
{
    if (auto&& [inside, _, dist] = point.isInsidePolygon(mainPolygon); inside && dist > minDistanceFromShoreline)
    {
        if (!ignoreInnerSeas)
            for (auto&& innerSea : innerSeaShorelines)
                if (auto&& [inside, _, dist] = point.isInsidePolygon(innerSea->getControlPoints()); inside || dist <= minDistanceFromShoreline)
                    return false;

        return true;
    }

    return false;
}

bool DLandmassMarker::isInside(const Segment2D& segment, bool ignoreInnerSeas, float minDistanceFromShoreline)
{

    if (auto&& [inside, _, dist] = segment.isInsidePolygon(mainPolygon); inside && dist > minDistanceFromShoreline)
    {
        if (!ignoreInnerSeas)
            for (auto&& innerSea : innerSeaShorelines)
                if (auto&& [inside, _, dist] = segment.isInsidePolygon(innerSea->getControlPoints()); inside || dist <= minDistanceFromShoreline)
                    return false;

        return true;
    }

    return false;
}

bool DLandmassMarker::isInside(const std::vector<QVector3D>& path, bool ignoreInnerSeas, float minDistanceFromShoreline)
{
    for (IndexType i = 0; i < path.size() - 1; i++)
        if (!isInside(Segment2D(path[i], path[i + 1]), ignoreInnerSeas, minDistanceFromShoreline))
            return false;

    return true;
}

bool DLandmassMarker::isInsideLand(const GVector2D& point, bool ignoreInnerSeas, float minDistanceFromShoreline)
{
    auto&& landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();

    for (auto&& landmass : landmasses)
        if (landmass->isInside(point, minDistanceFromShoreline, ignoreInnerSeas))
            return true;

    return false;
}

bool DLandmassMarker::isInsideLand(const Segment2D& segment, bool ignoreInnerSeas, float minDistanceFromShoreline)
{
    auto&& landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();

    for (auto&& landmass : landmasses)
        if (landmass->isInside(segment, minDistanceFromShoreline, ignoreInnerSeas))
            return true;

    return false;
}

QSet<GPoint> DLandmassMarker::findSeasideDomainSquares()
{
    QSet<GPoint> terrainOnlySquares;

    for (auto&& sq : squares)
        if (Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Water) && Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain))
            terrainOnlySquares += sq;

    return terrainOnlySquares;
}

QSet<GPoint> DLandmassMarker::findTerrainDomainSquares()
{
    QSet<GPoint> terrainOnlySquares;

    for (auto&& sq : squares)
        if (!Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Water))
            terrainOnlySquares += sq;

    return terrainOnlySquares;
}

bool DLandmassMarker::contains(const QSharedPointer<DShorelineMarker>& shoreline) const
{
    if (auto shoreline_it = std::find(shorelines.begin(), shorelines.end(), shoreline); shoreline_it != shorelines.end())
        return true;

    if (auto shoreline_it = std::find(innerSeaShorelines.begin(), innerSeaShorelines.end(), shoreline); shoreline_it != innerSeaShorelines.end())
        return true;

    return false;
}

bool DLandmassMarker::removeShoreline(const QSharedPointer<DShorelineMarker>& shoreline)
{
    if (auto shoreline_it = std::find(shorelines.begin(), shorelines.end(), shoreline); shoreline_it != shorelines.end())
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        shorelines.erase(shoreline_it);
        emit Editable::modified(sharedFromThis());
        return true;
    }

    if (auto shoreline_it = std::find(innerSeaShorelines.begin(), innerSeaShorelines.end(), shoreline); shoreline_it != innerSeaShorelines.end())
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        innerSeaShorelines.erase(shoreline_it);
        emit Editable::modified(sharedFromThis());
        return true;
    }

    return false;
}

void DLandmassMarker::clearShorelines()
{
    emit Editable::aboutToBeModified(sharedFromThis());
    shorelines.clear();
    innerSeaShorelines.clear();
    emit Editable::modified(sharedFromThis());
}

bool DLandmassMarker::addShoreline(const QSharedPointer<DShorelineMarker>& shoreline)
{
    if (std::find(shorelines.begin(), shorelines.end(), shoreline) == shorelines.end())
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        shorelines.push_back(shoreline);
        emit Editable::modified(sharedFromThis());
        return true;
    }

    return false;
}

bool DLandmassMarker::addInnerSeaShoreline(const QSharedPointer<DShorelineMarker>& shoreline)
{
    if (std::find(innerSeaShorelines.begin(), innerSeaShorelines.end(), shoreline) == innerSeaShorelines.end())
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        innerSeaShorelines.push_back(shoreline);
        emit Editable::modified(sharedFromThis());
        return true;
    }

    return false;
}

std::vector<QVector3D> DLandmassMarker::createLandmassPolygon(const std::vector<QSharedPointer<DShorelineMarker>>& shorelines, bool* isCoast)
{
    Polygon2D landmassNoShorelinePolygon;
    {
        QSet<GPoint> allTerrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
        auto terrainSquareSets = ShorelineUtils::splitSeparateSet(allTerrainSquares);

        QSet<GPoint> correctTerrainSquares;
        if (!shorelines.empty())
            correctTerrainSquares = *std::find_if(terrainSquareSets.begin(), terrainSquareSets.end(), [&](auto& a) { return a.contains(*shorelines.front()->getSquares().begin()); });
        else
            correctTerrainSquares = terrainSquareSets.front();

        auto polys = PolygonUtils::calculatePolygonsFromGridSquares(correctTerrainSquares);
        landmassNoShorelinePolygon = *std::max_element(polys.begin(), polys.end(), [](auto& p1, auto& p2) { return PolygonUtils::calculateArea(p1) < PolygonUtils::calculateArea(p2); });
    }
    if (landmassNoShorelinePolygon.isCW())
        landmassNoShorelinePolygon.reverseOrder();

    *isCoast = true;
    // check if is coast
    if (shorelines.size() == 1)
    {
        auto&& onlyShorelinePts = shorelines.front()->getControlPoints();
        *isCoast = landmassNoShorelinePolygon.findPointOnIndexedEdge(onlyShorelinePts.front(), 0.5f) && landmassNoShorelinePolygon.findPointOnIndexedEdge(onlyShorelinePts.back(), 0.5f);
    
        if (!*isCoast)
            return onlyShorelinePts;
    }

    std::vector<QVector3D> landmassPolygon;

    std::vector<GVector2D> landWithShorelinesPts;

    std::vector<GVector2D> shorelinesPts;
    for (auto&& shoreline : shorelines)
        shorelinesPts << shoreline->getControlPoints().front() << shoreline->getControlPoints().back();



    auto&& landPts = landmassNoShorelinePolygon.getPts();
    auto&& landCPts = asCircular(landPts);


    for (int i = 0; i < landPts.size(); i++)
    {
        landWithShorelinesPts << landPts[i];

        Segment2D landSegment(landPts[i], landPts[landCPts.findIdx(i, 1)]);

        std::map<float, GVector2D> shorelinePts;
        for (auto&& shorelinePt : shorelinesPts)
            if (landSegment.dist(shorelinePt) <= 1.0f)
                shorelinePts[landSegment.first.dist(shorelinePt)] = shorelinePt;

        for (auto&& [dist, pt] : shorelinePts)
        {
            if (pt != landPts[i] && pt != landPts[landCPts.findIdx(i, 1)])
                landWithShorelinesPts << pt;

            shorelinesPts.erase(std::remove(shorelinesPts.begin(), shorelinesPts.end(), pt), shorelinesPts.end());
        }
    }

    Q_ASSERT(shorelinesPts.empty());


    auto&& pts = landWithShorelinesPts;
    auto&& cPts = asCircular(landWithShorelinesPts);

    auto findShorelineIdOnPolygonSegment = [&](int idx) -> std::optional<int>
    {
        for (int i = 0; i < shorelines.size(); i++)
            if (pts[idx] == GVector2D(shorelines[i]->getControlPoints().front()))
                return i;

        return {};
    };

    auto addPathToPoly = [&](const std::vector<QVector3D>& path)
    {
        landmassPolygon.insert(landmassPolygon.end(), path.begin() + 1, path.end() - 1);

        for (int i = 0; i < pts.size(); i++)
            if (pts[i] == GVector2D(path.back()))
                return i;

        // error
        return -1;
    };

    std::set<int> usedShorelines;
    int nextIdx = 0;

    if (!shorelines.empty())
    {
        auto path = shorelines.front()->getControlPoints();
        landmassPolygon << path.front();
        nextIdx = addPathToPoly(path);
    }
    else
    {
        landmassPolygon << pts[nextIdx];
        nextIdx = cPts.findIdx(nextIdx, 1);
    }

    while (true)
    {
        if (pts[nextIdx] == (GVector2D)landmassPolygon.front())
            break;

        landmassPolygon << pts[nextIdx];

        if (auto nextShoreline = findShorelineIdOnPolygonSegment(nextIdx); nextShoreline)
        {
            Q_ASSERT(!usedShorelines.contains(*nextShoreline));
            usedShorelines.insert(*nextShoreline);

            nextIdx = addPathToPoly(shorelines[*nextShoreline]->getControlPoints());
        }
        else
            nextIdx = cPts.findIdx(nextIdx, 1);
    }

    return landmassPolygon;
}

std::vector<std::vector<QVector3D>> DLandmassMarker::createInnerSeamassPolygons(const std::vector<QSharedPointer<DShorelineMarker>>& innerSeaShorelines)
{
    std::vector<std::vector<QVector3D>> innerSeamassPolygons;

    for (auto&& shoreline : innerSeaShorelines)
        innerSeamassPolygons << shoreline->getControlPoints();

    return innerSeamassPolygons;
}

QSet<GPoint> DLandmassMarker::findInsideSquares(std::vector<QVector3D>& landmassPolygon, const std::vector<QSharedPointer<DShorelineMarker>>& shorelines, const std::vector<QSharedPointer<DShorelineMarker>>& innerSeaShorelines)
{
    QSet<GPoint> insideSquares;


    QSet<GPoint> shorelineSquares;
    for (auto&& shoreline : shorelines)
        shorelineSquares += shoreline->getSquares();

    for (auto&& shoreline : innerSeaShorelines)
        shorelineSquares += shoreline->getSquares();

    QSet<GPoint> squaresToCheck = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
    squaresToCheck -= shorelineSquares;

    auto squareSetsToCheck = ShorelineUtils::splitSeparateSet(squaresToCheck);

    for (auto&& squares : squareSetsToCheck)
        if (std::get<bool>(squares.begin()->midPoint().isInsidePolygon(landmassPolygon)))
            insideSquares += squares;

    return insideSquares;
}

void DLandmassMarker::makeName()
{
    static int nameCounter = 0;
    name = "Landmass " + QString::number(++nameCounter);
}

void omniSave(const DLandmassMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DPolygonWithHolesMarker&>(object);
    omniBin << object.name;
    omniBin << object.coast;
    omniBin << object.squares;

    object.shorelinesGuids.clear();
    for(auto&& shoreline : object.shorelines)
        object.shorelinesGuids << shoreline->getGuid();

    object.innerSeaShorelinesGuids.clear();
    for (auto&& shoreline : object.innerSeaShorelines)
        object.innerSeaShorelinesGuids << shoreline->getGuid();

    omniBin << object.shorelinesGuids;
    omniBin << object.innerSeaShorelinesGuids;
    omniBin << object.lock;
}

void omniLoad(DLandmassMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DPolygonWithHolesMarker&>(object);
    omniBin >> object.name;
    omniBin >> object.coast;
    omniBin >> object.squares;
    omniBin >> object.shorelinesGuids;
    omniBin >> object.innerSeaShorelinesGuids;
    omniBin >> object.lock;
}