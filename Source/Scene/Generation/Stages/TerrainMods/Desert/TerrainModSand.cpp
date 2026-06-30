#include "stdafx.h"
#include "TerrainModSand.h"
#include "SandSurfaceMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"
#include "Utils/Geom3DUtils.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"


namespace Generation
{
    TerrainMod<ETerrainMod::Sand>::TerrainMod(QSet<int>&& inArea, SandEmbankmentData&& sandData)
        : TerrainModBase(ETerrainMod::Sand, std::move(inArea))
        , sandData(std::move(sandData))
    {
        prepareSandQuadsData();
        showSandSurface();
    }

    // void TerrainMod<ETerrainMod::Sand>::postLoad(TerrainModBase* object)
    // {
    // }

    QSet<int> TerrainMod<ETerrainMod::Sand>::computeArea(const SandEmbankmentData& sandData)
    {
        QSet<int> area;
        area.reserve(sandData.slices.size() * 5);

        auto&& blockTree = Data::get()->getBlockQuadTree();
        auto&& cells = Data::get()->getTerrainCells()->getCells();

        for (const SandEmbankmentSlice& slice: sandData.slices)
        {
            const auto nodes = blockTree->find_all_nearest(slice.centerPoint.x, slice.centerPoint.z, slice.radius);
            for (const auto* node : nodes)
                area += node->data;
        }

        return area;
    }


    static float getInclineAngle(const QVector3D& vec)
    {
        return atan2f(vec.y(), GVector2D(vec).length());
    }

    static SandEmbankmentSlice processSandSliceAtPoint(const GVector2D& pt, const GVector2D& dir)
    {
        const auto& dem = Data::get()->getDEM();
        const auto& heightData = dem->heightData;

        constexpr float maxInclineAngle = 25.f;
        constexpr float noiseFreq = 0.02f;
        constexpr float step = 50.f;
        const float sliceArea = std::lerp(2.e4f, 4.e5f, (float)getGlobalNoiseValue(pt.x * noiseFreq, pt.z * noiseFreq, ENoiseUsage::TerrainHeight));
        std::unordered_map<GVector2D, float> demQueryCache;
        demQueryCache.reserve(100);

        const auto queryDem = [&demQueryCache, &heightData](const GVector2D& vec) -> float
        {
            const auto iter = demQueryCache.find(vec);
            if (iter != demQueryCache.end())
                return iter->second;
            else
            {
                const float result = heightData.sample(vec);
                demQueryCache[vec] = result;
                return result;
            }
        };

        const auto getSliceArea = [&queryDem, &step, &dir](const GVector2D& start, const GVector2D& end) -> float
        {
            const float startH = queryDem(start);
            const float endH   = queryDem(end);
            const int stepsCount = (end - start).length() / step;
            float area = 0.f;
            for (int i = 0; i < stepsCount - 1; ++i)
            {
                const float currT = i / (float)stepsCount;
                const float nextT = (i + 1) / (float)stepsCount;
                const GVector2D curr = std::lerp(start, end, currT);
                const GVector2D next = std::lerp(start, end, nextT);
                const QVector3D currDown(curr.x, queryDem(curr), curr.z);
                const QVector3D nextDown(curr.x, queryDem(next), curr.z);
                const QVector3D currUp(curr.x, std::lerp(startH, endH, currT), curr.z);
                const QVector3D nextUp(next.x, std::lerp(startH, endH, nextT), next.z);
                area += 0.5f * step * (std::max(currUp.y() - currDown.y(), 0.f) + std::max(nextUp.y() - nextDown.y(), 0.f));
            }
            return area;
        };

        GVector2D back = pt - dir * step * 3.f;
        GVector2D front = pt + dir * step * 3.f;
        float prevFrontVal = queryDem(back);

        constexpr int iterationsLimit = 40;
        int stepsCount = 6;

        while (true)
        {
            const QVector3D currBack(back.x, queryDem(back), back.z);
            const QVector3D currFront(front.x, queryDem(front), front.z);
            const float currentInclineAngle = radToDeg(getInclineAngle(currFront - currBack));
            
            const float currentArea = getSliceArea(back, front);
            if (currentArea >= sliceArea || prevFrontVal > currFront.y() || stepsCount > iterationsLimit)
                return SandEmbankmentSlice{
                    currFront,
                    currBack,
                    (back + front) * 0.5f,
                    stepsCount * 0.5f * step
                };
            if (currentInclineAngle > maxInclineAngle || currentArea <= 0.f)
            {
                back = back - step * dir;
            }
            else
            {
                front = front + step * dir;
                prevFrontVal = currFront.y();
            }
            ++stepsCount;
        }

        return SandEmbankmentSlice();
    }

    std::vector<QSharedPointer<TerrainModBase>> TerrainMod<ETerrainMod::Sand>::generateAll()
    {
        const auto& dem = Data::get()->getDEM();
        const auto ihlevels = Generation::Data::get()->getIsohypseMarkersByLevel();

        if (ihlevels.size() < 2)
            return std::vector<QSharedPointer<TerrainModBase>>();

        const auto& root = ihlevels.back().front();
        std::vector<QSharedPointer<TerrainModBase>> modsCreated;
        modsCreated.reserve(root->getPreflow().size() * 3);

        for (auto&& ihsVec : root->getPreflow())
        {
            for (auto&& ihs : ihsVec)
            {
                const Isohypse* ih = ihs.ih;
                if (ih)
                {
                    auto mod = createSandEmbankment(ih);
                    if (mod)
                        modsCreated <<= std::move(mod);
                }
            }
        }

        return modsCreated;
    }

    static bool checkPointForDesertBiome(const GVector2D& pt)
    {
        auto&& biomeDomain = Generation::Data::get()->getDomainAtSquare(pt.toGPoint(), EDomainType::Biome);
        if (!biomeDomain)
            return false;
        auto biomeData = biomeDomain->getData<EDomainType::Biome>();
        return biomeData->humidity == EHumidity::Desert;
    }

    QSharedPointer<TerrainModBase> TerrainMod<ETerrainMod::Sand>::createSandEmbankment(const Isohypse* ih)
    {
        const auto& points = ih->getCircularPoints();
        const float currentHeight = ih->getHeight();

        std::vector<QVector3D> sandContourPoints;
        sandContourPoints.reserve(points.getSize());

        // inclined surface
        const Polygon2D polygon(std::vector<GVector2D>(points.begin(), points.end()));

        SandEmbankmentData sandData;
        sandData.slices.reserve(points.getSize());
        sandData.isfullCircle = true;

        for (int i = 0; i < points.getSize(); ++i)
        {
            if (checkPointForDesertBiome(points[i]))
            {
                const GVector2D normal2d = polygon.getNormal(i);
                sandData.slices <<= processSandSliceAtPoint(points[i], -normal2d);
                sandContourPoints << points[i];
            }
            else
                sandData.isfullCircle = false;
        }

        if (sandData.slices.empty())
            return {};

        spawn<DLineMarker>(sandContourPoints, Colors::robinEggBlue, false, currentHeight);

        QSet<int> area = computeArea(sandData);

        return QSharedPointer<TerrainMod<ETerrainMod::Sand>>::create(std::move(area), std::move(sandData));
    }

    void TerrainMod<ETerrainMod::Sand>::prepareSandQuadsData()
    {
        auto cSlices = asCircular(sandData.slices);

        for (int i = 0; i < cSlices.getSize(); ++i)
        {
            if (!sandData.isfullCircle && (i == cSlices.getSize() - 1))
                break;

            SandPolygon sp;

            const std::vector<QVector3D> pts{
                cSlices[i].upPoint,
                cSlices.getNext(i).upPoint,
                cSlices.getNext(i).downPoint,
                cSlices[i].downPoint
            };
            const Segment2D seg1(pts[3], pts[0]);
            const Segment2D seg2(pts[2], pts[1]);
            if (seg1.intersects(seg2, false))
            {
                sp.polygon = Polygon2D(std::vector<GVector2D>{ pts[3], pts[1], pts[0], pts[2] });
                sp.heights = { pts[3].y(), pts[1].y(), pts[0].y(), pts[2].y() };
            }
            else
            {
                sp.polygon = Polygon2D(std::vector<GVector2D>{ pts[0], pts[1], pts[2], pts[3] });
                sp.heights = { pts[0].y(), pts[1].y(), pts[2].y(), pts[3].y() };
            }

            sandQuads <<= std::move(sp);
        }
    }

    void TerrainMod<ETerrainMod::Sand>::showSandSurface() const
    {
        for (const SandPolygon& sp: sandQuads)
        {
            std::vector<QVector3D> pts;
            pts.reserve(sp.heights.size());
            for (int i = 0; i < sp.heights.size(); ++i)
                pts <<= QVector3D(sp.polygon[i].x, sp.heights[i], sp.polygon[i].z);
            spawn<DSandSurfaceMarker>(pts, 0.f);
        }
    }

    static float getPointHeight(const SandPolygon& sp, const GVector2D& pt)
    {
        const float x1 = sp.polygon[0].x;
        const float x2 = sp.polygon[1].x;
        const float x3 = sp.polygon[2].x;
        const float z1 = sp.polygon[0].z;
        const float z2 = sp.polygon[1].z;
        const float z3 = sp.polygon[2].z;
        const float y1 = sp.heights[0];
        const float y2 = sp.heights[1];
        const float y3 = sp.heights[2];

        const float A = y1 * (z2 - z3) + y2 * (z3 - z1) + y3 * (z1 - z2);
        const float B = z1 * (x2 - x3) + z2 * (x3 - x1) + z3 * (x1 - x2);
        const float C = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2);
        const float D = -(A * x1 + B * y1 + C * z1);

        Q_ASSERT(B != 0.f);

        return (-D - C * pt.z - A * pt.x) / B;
    }

    void TerrainMod<ETerrainMod::Sand>::submitAll(ModAlterationsList* mal) const
    {
        auto&& clusterMap = Data::get()->getTerrainClustersMap();

        std::set<TerrainBlockClusterBase*> affectedClusters;
        for (int cell : area)
            affectedClusters.insert(clusterMap[cell].get());

        for(auto* cluster : affectedClusters)
        {
            auto&& verts = cluster->section->getVertices();
            for (IndexType i = 0; i < verts.size(); ++i)
            {
                TerrainMeshVertex prop = verts[i];
                for (const SandPolygon& sp: sandQuads)
                {
                    if (sp.polygon.contains(prop.position))
                    {
                        const float offsetY = getPointHeight(sp, prop.position);
                        if (offsetY > prop.position.y())
                        {
                            prop.position.setY(offsetY);
                            // TODO set desert texture and biome
                            Generation::setPackParam(&prop.packParams, 0, 1.f);
                            const std::vector<float> terrainWeights = {0.f, 0.f, 0.f, 1.f};
                            prop.terrainTexWeights = compileTexWeights(terrainWeights);

                            (*mal)[cluster->keyCell][i] << prop;
                        }
                        break;
                    }
                }
            }
        }
    }

    TerrainMeshVertex TerrainMod<ETerrainMod::Sand>::apply(const std::vector<TerrainMeshVertex>& alterations)
    {
        TerrainMeshVertex result;
        result.position.setY(std::numeric_limits<float>::max());

        for (auto&& alt : alterations)
            if (alt.position.y() < result.position.y())
                result = alt;

        return result;
    }

    void TerrainMod<ETerrainMod::Sand>::clearAll()
    {
        Data::get()->clearExactMarkers<DSandSurfaceMarker>();
    }

    QVector4D TerrainMod<ETerrainMod::Sand>::getDebugColor() const
    {
        return QVector4D(0.95f, 0.64f, 0.37f, 1.f);
    }
}

void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::Sand>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainModBase&>(object);
}

void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::Sand>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainModBase&>(object);
}
