#include "stdafx.h"
#include "DuneBarchan.h"
#include "Utils/Interpolation.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

#define DEBUG_BARCHANS 0

static std::tuple<float, float> getInterpolationParamsForPointOnDune(const GVector2D& left, const GVector2D& direction, float maxDistanceToLine, const GVector2D& point,
    const std::vector<GVector2D>& curveBottom, const std::vector<GVector2D>& curveRidge)
{
    const float currDistToLine = distanceFromPointToInfiniteLine(left, direction, point);
    const float t1 = currDistToLine / maxDistanceToLine;

    const auto [_1, d1, _2] = directionalBoundDistance(curveRidge, point);
    const auto [_3, d2, _4] = directionalBoundDistance(curveBottom, point);
    const float t2 = isZero(d1 + d2) ? 0.f : d2 / (d1 + d2);

    return { t1, t2 };
}


static float getDuneHeight(const GVector2D& pt, float t1, float t2, EInterpolation010 interpolationType1, EInterpolation01 interpolationType2, float maxHeight)
{
    if (isZero(t2))
        return getSandHeight(pt);

    const auto interp1 = Interpolation::getInterpolation010(interpolationType1, 1.f);
    const auto interp2 = Interpolation::getInterpolation01(interpolationType2, 2.f);

    const float h1 = std::lerp(0.f, maxHeight, (float)interp1->interpolate(t1));
    const float currentH = std::lerp(0.f, h1, (float)interp2->interpolate(t2));
    const float demHeight = Generation::Data::get()->getDEM()->heightData.sample(pt);

    return currentH + demHeight;
}


static GVector2D getBezierPeakPoint(const GVector2D& cpt0, const GVector2D& cpt1, const GVector2D& cpt2)
{
    // formula for bezier with 3 control points P = (1-t)^2*p0 + 2t(1-t)p1 + t*t*p2
    return 0.25f * cpt0 + 0.5f * cpt1 + 0.25f * cpt2;
}


static GVector2D getBezierControlPoint(const GVector2D& cpt0, const GVector2D& cpt2, const GVector2D& peakPt)
{
    return 2.f * peakPt - 0.5f * cpt0 - 0.5f * cpt2;
}


// returns control point, ridge curve and max height of dune
static std::tuple<GVector2D, BezierCurve2D, float> calculateRidgeCurve(const GVector2D& left, const GVector2D& right,
    const BezierCurve2D& frontCurve, const BezierCurve2D& backCurve,
    float frontAngle, float backAngle)
{
    const GVector2D midPoint = 0.5f * (left + right);
    const GVector2D frontPeak = frontCurve.evaluate(0.5f);
    const GVector2D backPeak = backCurve.evaluate(0.5f);
    const float L = (frontPeak - backPeak).length();
    const float tanA = fastTan(backAngle);
    const float tanB = fastTan(frontAngle);
    const float k = tanB / tanA;
    const float l2 = L / (k + 1.f);
    const GVector2D centerPeak = std::lerp(backPeak, frontPeak, k / (k + 1.f));

    GVector2D ridgeControlPoint = getBezierControlPoint(left, right, centerPeak);
    const float maxHeight = tanB * l2;

    return { std::move(ridgeControlPoint), BezierCurve2D({left, ridgeControlPoint, right}), maxHeight };
}

namespace Generation
{
    DesertClusterSubData<EDesertBlockSubtype::DuneBarchan>::DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData)
        : DesertClusterSubDataBase(inBaseData)
    {
    }

    std::unordered_set<int> DesertClusterSubData<EDesertBlockSubtype::DuneBarchan>::customGrow(const std::unordered_set<int>& candidates)
    {
        return customGrowFilterIslands(candidates, &baseData->cells);
        // customGrowWithCellsLayers(candidates, baseData->cells, layers, allLayersCells);
    }

    QSharedPointer<DesertSubClusterBase> DesertClusterSubData<EDesertBlockSubtype::DuneBarchan>::createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster)
    {
        return QSharedPointer<DesertSubCluster<EDesertBlockSubtype::DuneBarchan>>::create(cluster);
    }

    DesertSubCluster<EDesertBlockSubtype::DuneBarchan>::DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster)
        : DesertSubClusterBase(inCluster, EDesertBlockSubtype::DuneBarchan)
    {
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneBarchan>::generate()
    {
        auto&& diagram = Data::get()->getTerrainCells();
        const auto& coreCell = diagram->getCellAt(cluster->keyCell);
        const GVector2D windDirection = Data::get()->getWindVector(coreCell.getVoronoiCenter());
        const float windStrength = windDirection.length();

#if DEBUG_BARCHANS
        // debug draw cluster's cells
        for (int cellIdx : cluster->cells)
        {
            auto&& cell = diagram->getCells()[cellIdx];
            const Polygon2D& cellPolygon = cell.getPolygon();
            const bool isCenterCell = (cellIdx == *cluster->cells.begin());
            cellPolygon.debugPlot((isCenterCell ? Colors::red : Colors::gray), isCenterCell ? 0.f : -1.f);
        }
#endif

        // Get triangle inside polygon -------------------------------------------------------------------------------------------------------------------------------
        const Polygon2D clusterPolygon = Utils::makeBoundingPolygon(cluster->cells).front();
        const GVector2D polygonCenter = clusterPolygon.getCenter();
        const GVector2D dir = -windDirection.normalized();
        const GVector2D normal = dir.rotatedLeft90();

        const float polygonRadius = clusterPolygon.getRadius();
        const float radius = 0.8f * polygonRadius;
        GVector2D center = polygonCenter;
        const float duneBackFactor = 0.35f;

        const float step = polygonRadius * 0.2f;
        GVector2D frontLimitPoint = center + dir * step;
        GVector2D backLimitPoint = center - dir * step;

        while (true)
        {
            const GVector2D testPt = frontLimitPoint + dir * step;
            if (clusterPolygon.contains(testPt))
                frontLimitPoint = testPt;
            else
                break;
        }

        while (true)
        {
            const GVector2D testPt = backLimitPoint - dir * step;
            if (clusterPolygon.contains(testPt))
                backLimitPoint = testPt;
            else
                break;
        }

        center = std::lerp(backLimitPoint, frontLimitPoint, -0.42f);

        GVector2D front = center + dir * radius;
        GVector2D right = center + normal * radius;
        GVector2D left = center - normal * radius;
        while (true)
        {
            front = front + dir * radius;
            const GVector2D currentPeak = getBezierPeakPoint(left, front, right);
            if (!clusterPolygon.contains(currentPeak))
                break;
        }

        const float width = std::min(polygonRadius, (front - center).length() * getRandomFloat(0.1f, 0.42f));
        right = center + normal * width;
        left = center - normal * width;

        const float tAngle = std::min(width / 6000.f, 1.f);
        const float frontAngle = degToRad(std::lerp(30.f, 10.f, tAngle)); // [10, 20]
        const float backAngle = degToRad(std::lerp(40.f, 30.f, tAngle));  // [30, 35]

        // Create curves for dunes -----------------------------------------------------------------------------------------------------------------------------------

        const GVector2D deltaFrontVec = front - center;
        const GVector2D duneBack = center + deltaFrontVec * duneBackFactor;

        const BezierCurve2D frontCurve({ left, front,    right });
        const BezierCurve2D backCurve( { left, duneBack, right });

        const auto [duneCenter, centerCurve, maxDuneHeight] = calculateRidgeCurve(left, right, frontCurve, backCurve, frontAngle, backAngle);

        constexpr int stepsCount = 20;
        const std::vector<GVector2D> frontPolyline   = frontCurve.getPoints(stepsCount);
        const std::vector<GVector2D> centerPolyline = centerCurve.getPoints(stepsCount);
        const std::vector<GVector2D> backPolyline     = backCurve.getPoints(stepsCount);

#if DEBUG_BARCHANS
        // debug draw barchans curves
        spawn<DLineMarker>(frontPolyline, Colors::robinEggBlue, false, 200.f);
        spawn<DLineMarker>(centerPolyline, Colors::orange, false, 200.f);
        spawn<DLineMarker>(backPolyline, Colors::laRioja, false, 200.f);

        const auto cvec1 = {center, front};
        spawn<DLineMarker>(cvec1, Colors::red, false, 350.f);
#endif

        // Create polygons from curves -------------------------------------------------------------------------------------------------------------------------------
        std::vector<GVector2D> tempPoints;
        tempPoints.reserve(frontPolyline.size() + centerPolyline.size() + backPolyline.size() / 2);

        // remove start and end points of front and end curves to prevent duplicates
        auto frontCopy = frontPolyline;
        auto backCopy = backPolyline;
        frontCopy.pop_back();
        backCopy.pop_back();
        std::reverse(frontCopy.begin(), frontCopy.end());
        std::reverse(backCopy.begin(), backCopy.end());
        frontCopy.pop_back();
        backCopy.pop_back();

        tempPoints = std::move(frontCopy);
        tempPoints.insert(tempPoints.end(), centerPolyline.begin(), centerPolyline.end());
        const Polygon2D frontSlopePolygon(std::move(tempPoints));

        tempPoints = std::move(backCopy);
        tempPoints.insert(tempPoints.end(), centerPolyline.begin(), centerPolyline.end());
        const Polygon2D backSlopePolygon(std::move(tempPoints));

        // Height functions ------------------------------------------------------------------------------------------------------------------------------------------

        const float duneWidth = (right - left).length();

        const auto create3dFrontSlopeFunc = [&left, &dir, &frontPolyline, &centerPolyline, duneWidth, maxDuneHeight](const QVector3D& v)
        {
            const GVector2D pt = (GVector2D)v;
            const auto [t1, t2] = getInterpolationParamsForPointOnDune(left, dir, duneWidth, pt, frontPolyline, centerPolyline);
            const float duneHeight = getDuneHeight(pt, t1, t2, EInterpolation010::Sine, EInterpolation01::InversePower, maxDuneHeight);
            const float resultHeight = duneHeight;

            return QVector3D(v.x(), resultHeight, v.z());
        };

        const auto create3dBackSlopeFunc = [&left, &dir, &backPolyline, &centerPolyline, duneWidth, maxDuneHeight](const QVector3D& v)
        {
            const GVector2D pt = (GVector2D)v;
            const auto [t1, t2] = getInterpolationParamsForPointOnDune(left, dir, duneWidth, pt, backPolyline, centerPolyline);
            const float duneHeight = getDuneHeight(pt, t1, t2, EInterpolation010::Sine, EInterpolation01::Power, maxDuneHeight);
            const float resultHeight = duneHeight;

            return QVector3D(v.x(), resultHeight, v.z());
        };

        const auto create3dDune = [&frontSlopePolygon, &backSlopePolygon, &create3dFrontSlopeFunc, &create3dBackSlopeFunc](const QVector3D& v)
        {
            const GVector2D pt = (GVector2D)v;
            if (frontSlopePolygon.contains(pt))
                return create3dFrontSlopeFunc(v);
            else if (backSlopePolygon.contains(pt))
                return create3dBackSlopeFunc(v);
            return get3dSandPoint(v);
        };

        // Fill mesh -------------------------------------------------------------------------------------------------------------------------------------------------
        MeshConnector meshConnector;

        // Additional points for ridge ------------------------------------------------------
        std::vector<GVector2D> centerRidgePoints;
        centerRidgePoints.reserve(centerPolyline.size() * 2);
        for (const auto& pt: centerPolyline)
        {
            if (clusterPolygon.contains(pt, false))
            {
                centerRidgePoints << pt;
                const GVector2D offsetDuplicate = pt + dir;
                if (clusterPolygon.contains(offsetDuplicate, false))
                    centerRidgePoints << offsetDuplicate;
            }
        }
        // ----------------------------------------------------------------------------------

        auto meshingParams = getDefaultMeshingParams();
        const auto [geom2D, _] = meshPolygon2(clusterPolygon.getPts(), meshingParams);
        auto& vertsP = geom2D.vertices;
        auto& indP = geom2D.indices;
        meshConnector.addMesh(vertsP, indP, create3dDune);
        cluster->fillResultMesh(meshConnector);
    }
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneBarchan>& object, OmniBin<std::ios::out>& omniBin)
{

}

void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneBarchan>& object, OmniBin<std::ios::in>& omniBin)
{

}
