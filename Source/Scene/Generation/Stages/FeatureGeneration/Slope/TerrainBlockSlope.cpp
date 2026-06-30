#include "stdafx.h"
#include "TerrainBlockSlope.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/Interpolation.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Utils/Resumable.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"

#define DEBUG_SLOPE_AXIS 0

namespace Generation
{
    using SlopeCluster = TerrainBlockCluster<ETerrainBlock::Slope>;

    float SlopeCluster::chance(const BlockChanceData& data)
    {
        if (data.steepness >= 0.2)
            return data.steepness;

        return 0.0f;
    }

    static GVector2D getRandomVector(const GVector2D& dir, float dispersionRangeDegrees)
    {
        const float angle = getRandomFloat(-dispersionRangeDegrees, dispersionRangeDegrees);
        return GVector2D::rotateDegrees(dir, angle);
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> SlopeCluster::generateMesh()
    {
        auto&& dem = Data::get()->getDEM();

        initializeShape();

        // const auto getMeshSegmentsSlope = [this](const GVector2D& p1, const GVector2D& p2)
        // {
        //     QVector3D P1 = { p1.x, heightData.sample(p1), p1.z };
        //     QVector3D P2 = { p2.x, heightData.sample(p2), p2.z };
        //     const float dist = distance(P1, P2);
        //     return dist / gMaxTriangleSideLength + 1;
        // };

        auto [geom, unused] = meshPolygon2(calculatePolygon().getPts());
        GeometryData<TerrainMeshVertex> geometry;
        auto& verts = geom.vertices;
        geometry.vertices.resize(verts.size());
        geometry.indices = std::move(geom.indices);

        for (int i = 0; i < verts.size(); ++i)
        {
            auto&& v = verts[i];
            // float h = heightData.sample(v);

            const float h = calculateHeightForPoint(v);
            TerrainMeshVertex finalPoint = { {v.x, h, v.z}, {}, *this };

            geometry.vertices[i] = std::move(finalPoint);
        }

        return spawnBatched(std::move(geometry), makeBatchParams());
    }

    void SlopeCluster::computeHeightField()
    {
        auto&& diagram  = Data::get()->getTerrainCells();
        auto&& dem      = Data::get()->getDEM();

        float maxX = std::numeric_limits<float>::min();
        float maxZ = std::numeric_limits<float>::min();
        float minX = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        for (int cellIdx : cells)
        {
            auto&& cell = diagram->getCells()[cellIdx];

            for (auto&& p : cell)
            {
                minX = std::min(minX, p.x);
                minZ = std::min(minZ, p.z);
                maxX = std::max(maxX, p.x);
                maxZ = std::max(maxZ, p.z);
            }
        }

        // Precompute Slope heightfield
        heightData = Heightfield(GVector2D(minX, minZ), GVector2D(maxX, maxZ), 100.0f);
        float maxD = 0.0f;

        for (int z = 0; z <= heightData.getSize().z; ++z)
            for (int x = 0; x <= heightData.getSize().x; ++x)
            {
                auto&& p = heightData.getPoint2D(x, z);
                auto [axisDistance, axisCoord] = computeAxisData(p, &maxD);

                const double tDescent = descentGen->interpolate(axisCoord);
                const double tContour = contourGen->interpolate(axisDistance / maxD);

                const double baseValue = dem->heightData.sample(p);
                const double axisValue = std::lerp(double(topH), double(botH), tDescent);
                const double finalValue = std::lerp(axisValue, baseValue, tContour);

                heightData.setHeight(x, z, finalValue);
            }

        // heightData.makePreview<DDemMarker>(Colors::red);
    }

    std::array<float, 2> SlopeCluster::computeAxisData(const GVector2D& p, float* maxD)
    {
        float minD = std::numeric_limits<float>::max();
        float axisCoord = -1.0f;

        for (int a = 0; a < axes.size(); ++a)
        {
            auto&& axis = axes[a];
            auto [nearestP, d, idx] = directionalBoundDistance(axis, p);
            if (d < minD)
            {
                minD = d;
                auto&& lengths = accLengths[a];
                axisCoord = (lengths[idx] + distance(axis[idx], nearestP)) / lengths.back();
            }

            *maxD = std::max(*maxD, d);
        }

        return { minD, axisCoord };
    }

    void TerrainBlockCluster<ETerrainBlock::Slope>::preprocessAxes()
    {
        accLengths.resize(axes.size());
        for (int a = 0; a < axes.size(); ++a)
        {
            auto&& axis = axes[a];
            auto&& lengths = accLengths[a];

            float l = 0.0f;
            lengths.resize(axis.size(), l);

            for (int i = 1; i < lengths.size(); ++i)
            {
                l += distance(axis[i - 1], axis[i]);
                lengths[i] = l;
            }
        }
    }

    void TerrainBlockCluster<ETerrainBlock::Slope>::calculateMinMaxHeight()
    {
        auto&& dem = Data::get()->getDEM();

        botH = std::numeric_limits<float>::max();
        topH = std::numeric_limits<float>::lowest();

        const Polygon2D polygon = calculatePolygon();

        for (const auto& pt: polygon.getPts())
        {
            const float h = dem->heightData.sample(pt);

            topH = std::max(h, topH);
            botH = std::min(h, botH);
        }
    }

    void TerrainBlockCluster<ETerrainBlock::Slope>::buildDescentTech()
    {
        static const std::map<ESlopeDescent, EInterpolation01> slopeInterpolationMap =
        {
            { ESlopeDescent::Linear,  EInterpolation01::Linear },
            { ESlopeDescent::Convex,  EInterpolation01::Power },
            { ESlopeDescent::Concave, EInterpolation01::InversePower },
        };

        static const std::map<ESlopeContour, EInterpolation01> slopeContourInterpolationMap =
        {
            { ESlopeContour::Linear,  EInterpolation01::Linear },
            { ESlopeContour::Convex,  EInterpolation01::Power },
            { ESlopeContour::Concave, EInterpolation01::InversePower },
        };

        descentGen = Interpolation::getInterpolation01(slopeInterpolationMap.at(slopeDescentType),    getRandomInt(1, 3));
        contourGen = Interpolation::getInterpolation01(slopeContourInterpolationMap.at(slopeContour), getRandomInt(1, 3));
    }

    QVector3D randomVector(float length)
    {
        std::uniform_real_distribution d(-length, length);
        return { d(gRandomEngine), d(gRandomEngine), d(gRandomEngine) };
    }

    void TerrainBlockCluster<ETerrainBlock::Slope>::initialize()
    {
        smoothingParams.weight = 0.42f;
        smoothingParams.smoothingRadius = 180.f;

        slopeDescentType = static_cast<ESlopeDescent>(getRandomInt(0, int(magic_enum::enum_count<ESlopeDescent>()) - 1));
        slopeContour     = static_cast<ESlopeContour>(getRandomInt(0, int(magic_enum::enum_count<ESlopeContour>()) - 1));

        buildDescentTech();
        calculateMinMaxHeight();
        // computeSlopeAxes(slopeContour);
        // preprocessAxes();
    }
    
    ClusterData<ETerrainBlock::Slope>::ClusterData(TerrainBlockMetaCluster<ETerrainBlock::Slope>* metaCluster, int id)
        : ClusterDataBase(id)
        , baseGradient(Data::get()->getDEM()->heightData.sampleGradient(Data::get()->getTerrainCells()->getCellAt(id)->getCenter()))
    {
        if (!baseGradient.isNull())
            baseGradient.normalize();
    }

    std::unordered_set<int> ClusterData<ETerrainBlock::Slope>::customGrow(const std::unordered_set<int>& candidates)
    {
        auto&& allCells = Data::get()->getTerrainCells()->getCells();
        auto&& dem = Data::get()->getDEM();

        std::unordered_set<int> added;
        added.reserve(candidates.size());

        for (int c : candidates)
        {
            const GVector2D gradient = dem->heightData.sampleGradient(allCells[c]->getCenter()).normalized();
            if (angle180(gradient, baseGradient) < 20.0f)
                added += c;
        }

        return customGrowFilterIslands(added, &cells);
    }

    std::vector<GVector2D> TerrainBlockCluster<ETerrainBlock::Slope>::createAxis()
    {
        auto&& dem = Data::get()->getDEM();
        const Polygon2D clusterPolygon = calculatePolygon();
        const GVector2D center = clusterPolygon.getCenter();
        std::vector<GVector2D> result;
        result << center;
        result.reserve(20);
        const float step = getRandomFloat(200.f, 500.f);

        GVector2D gradient = dem->heightData.sampleGradient(center);
        while (gradient.isNull())
            gradient = GVector2D(randomVector(1.0f)).normalized();

        GVector2D currVec = gradient;
        const float angleDispersion = 30.f;

        while (true)
        {
            result << result.back() + currVec * step;
            currVec = getRandomVector(currVec, angleDispersion);
            if (!clusterPolygon.contains(result.back()))
                break;
        }
        std::reverse(result.begin(), result.end());
        currVec = -gradient;
        while (true)
        {
            result << result.back() + currVec * step;
            currVec = getRandomVector(currVec, angleDispersion);
            if (!clusterPolygon.contains(result.back()))
                break;
        }

        return result;
    }

    void TerrainBlockCluster<ETerrainBlock::Slope>::initializeShape()
    {
        auto&& dem = Data::get()->getDEM();
        const Polygon2D clusterPolygon = calculatePolygon();
        const GVector2D center = clusterPolygon.getCenter();
        const float clusterRadius = clusterPolygon.getRadius();
        GVector2D gradient = dem->heightData.sampleGradient(center);
        while (gradient.isNull())
            gradient = GVector2D(randomVector(1.0f)).normalized();

#if DEBUG_SLOPE_AXIS
        const QVector3D bPt3D = { center.x, dem->heightData.sample(center), center.z };
        const GVector2D testPt = center + gradient * 250.f;
        const QVector3D testPt3D = { testPt.x, dem->heightData.sample(testPt), testPt.z };
        spawn<DLineMarker>(bPt3D, testPt3D, Colors::yellow, 0.f, ELineDecorator::Arrow);
#endif

        isConvex = getRandomInt(0, 1) > 0;
        const float curvatureFactor = getRandomFloat(0.9f, 1.1f);
        const float topPlatformFactor = getRandomFloat(0.1f, 0.15f);
        const float bottomPlatformFactor = getRandomFloat(0.1f, 0.15f);
        auto intersections = clusterPolygon.rayIntersections({ center, center + gradient * 1e6f });
        //Q_ASSERT(!intersections.empty());
        const float topL = intersections.empty() ? clusterRadius * 0.5f : intersections.begin().key();
        intersections = clusterPolygon.rayIntersections({ center, center - gradient * 1e6f });
        //Q_ASSERT(!intersections.empty());
        const float bottomL = intersections.empty() ? clusterRadius * 0.5f : intersections.begin().key();

        const float distFromFocalToCenter = clusterRadius * (curvatureFactor + 1.f);
        focalPoint = center + gradient * distFromFocalToCenter * (isConvex ? 1.f : -1.f);

        radius1 = distFromFocalToCenter - topL    * (1.f - (isConvex ? topPlatformFactor : bottomPlatformFactor));
        radius2 = distFromFocalToCenter + bottomL * (1.f - (isConvex ? bottomPlatformFactor : topPlatformFactor));

        const float offset = (radius2 - radius1) * getRandomFloat(0.03f, 0.1f);

        noiseSource.SetSeed(gRandomEngine());
        noiseSource.SetFrequency(getRandomFloat(5e-4f, 2e-3f));
        noiseSource.SetOctaveCount(getRandomInt(2, 5));
        noiseModel.SetModule(noiseSource);

        const GVector2D initialRadialDir = (center - focalPoint) * 10.f;
        constexpr float deltaAngle = 10.f;
        bool isAngleFound = false;
        guidingAxes.reserve(10);
        for(float angle = -90.f ; angle <= 90.f; angle += deltaAngle)
        {
            GVector2D currRadialDir = GVector2D::rotateDegrees(initialRadialDir, angle);
            if (!clusterPolygon.intersects(Segment2D(focalPoint, focalPoint + currRadialDir)))
            {
                if (isAngleFound)
                    break;
                else
                    continue;
            }
            isAngleFound = true;

            currRadialDir.normalize();

            const GVector2D normalVec = currRadialDir.rotatedLeft90();

            guidingAxes << Axis();
            Axis& axis = guidingAxes.back();
            constexpr int count = 30;
            axis.reserve(count + 1);
            for (int i = 0; i <= count; ++i)
            {
                const float t = i / (float)count;
                const GVector2D originalPoint = focalPoint + currRadialDir * std::lerp(radius1, radius2, t);
                const GVector2D offsetVec = normalVec * noiseModel.GetValue(originalPoint.x, originalPoint.z) * offset;
                axis.addPoint(originalPoint + offsetVec);
            }
        }

#if DEBUG_SLOPE_AXIS
        const auto color = Colors::random(false);
        for (const Axis& axis: guidingAxes)
        {
            std::vector<QVector3D> axis3d;
            axis3d.reserve(axis.pts.size());
            for (const GVector2D& pt: axis.pts)
                axis3d << QVector3D(pt.x, dem->heightData.sample(pt), pt.z);
            spawn<DLineMarker>(axis3d, color, false);
        }
#endif

    }

    float TerrainBlockCluster<ETerrainBlock::Slope>::calculateHeightForPoint(const GVector2D& pt)
    {
        const float distToFocalPt = distance(focalPoint, pt);
        const float startH = isConvex ? topH : botH;
        const float finishH = isConvex ? botH : topH;

        float h = startH;
        if (distToFocalPt > radius1)
        {
            if (distToFocalPt < radius2)
            {
                float minDistSq = std::numeric_limits<float>::max();
                float param = 0.f;
                float distSq2 = 0.f;
                float param2 = 0.f;
                for (int i = 0; i < guidingAxes.size(); ++i)
                {
                    const Axis& axis = guidingAxes[i];
                    const auto [t, distSq] = axis.getProjection(pt);
                    if (distSq < minDistSq)
                    {
                        minDistSq = distSq;
                        param = t;
                    }
                    else
                    {
                        if (distSq2 < distSq)
                        {
                            distSq2 = distSq;
                            param2 = t;
                        }
                        if (i != 1 && i != guidingAxes.size() - 1)
                        {
                            const float d1 = minDistSq;
                            const float d2 = distSq2;
                            if (d1 + d2 > 0.f)
                                param = std::lerp(param, param2, d1 / (d1 + d2));
                        }
                        break;
                    }
                    distSq2 = distSq;
                    param2 = t;
                }

                const float tDescent = descentGen->interpolate(param);
                h = std::lerp(startH, finishH, tDescent);
            }
            else
                h = finishH;
        }
        return h;
    }

    void TerrainBlockCluster<ETerrainBlock::Slope>::computeSlopeAxes(ESlopeContour contour)
    {
        auto&& dem = Data::get()->getDEM();
        axes.emplace_back(createAxis());
#if DEBUG_SLOPE_AXIS
        std::vector<QVector3D> axis3d;
        axis3d.reserve(axes.back().size());
        for (const GVector2D& pt: axes.back())
            axis3d << QVector3D(pt.x, dem->heightData.sample(pt), pt.z);
        spawn<DLineMarker>(axis3d, Colors::green, false);
#endif
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Slope>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        // 100% rock slab, reduced vegetation
        packParams = compilePackParams({ 0.0f, 0.8f });
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Slope>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
    omniBin << object.botH;
    omniBin << object.topH;
    omniBin << object.axes;
    omniBin << object.accLengths;
    omniBin << object.slopeDescentType;
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Slope>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
    omniBin >> object.botH;
    omniBin >> object.topH;
    omniBin >> object.axes;
    omniBin >> object.accLengths;
    omniBin >> object.slopeDescentType;

    object.buildDescentTech();
}
