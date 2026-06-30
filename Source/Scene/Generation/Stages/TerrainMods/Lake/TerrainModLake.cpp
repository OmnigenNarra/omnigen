#include "stdafx.h"
#include "TerrainModLake.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "LakeSurfaceMarker.h"

namespace Generation
{
    TerrainMod<ETerrainMod::Lake>::TerrainMod(QSet<int> inArea, Polygon2D inAreaPoly, std::vector<ControlPoint> inPts)
        : TerrainModBase(ETerrainMod::Lake, std::move(inArea))
        , controlPoints(std::move(inPts))
        , areaPolygon(std::move(inAreaPoly))
    {
    }

    void TerrainMod<ETerrainMod::Lake>::postLoad(TerrainModBase* object)
    {
        static_cast<TerrainMod<ETerrainMod::Lake>*>(object)->showWaterSurface();
    }

    std::vector<QSharedPointer<TerrainModBase>> TerrainMod<ETerrainMod::Lake>::generateAll()
    {
        auto lakeSeeds = chooseLakeSeeds();

        std::vector<QSharedPointer<TerrainModBase>> modsCreated;
        for (int seed : lakeSeeds)
            if (auto mod = createLake(computeLakeArea(seed)); mod)
                modsCreated << mod;

        return modsCreated;
    }

    std::unordered_set<int> TerrainMod<ETerrainMod::Lake>::chooseLakeSeeds()
    {
        auto&& dem = Data::get()->getDEM();
        auto&& ridgeQTree = Data::get()->getMarkerQuadTree<DRidgeMarker>();

        std::unordered_set<int> results;

        for (auto&& metaClusterVec : Data::get()->getTerrainMetaClusters())
            for (auto&& metaCluster : metaClusterVec)
                for (auto&& cluster : metaCluster->getClusters())
                    if (cluster->type == ETerrainBlock::Flatland)
                    {
                        auto clusterArea = Utils::makeBoundingPolygon(cluster->cells).front();
                        auto clusterCenter = clusterArea.getCenter();

                        // Can't spawn near Ridges
                        auto* ridgeNode = ridgeQTree.find_nearest(clusterCenter.x, clusterCenter.z, 2000);
                        if (ridgeNode)
                            continue;

                        // Always spawn in hollow areas
                        auto ced = dem->getCellElevationData(clusterArea);
                        if (ced.height < ced.minH)
                        {
                            results.insert(randomObjectFromSet(cluster->cells, gRandomEngine));
                            continue;
                        }

                        // Can spawn if flat enough
                        if (ced.steepness < 0.03)
                        {
                            auto&& cell =  randomObjectFromSet(cluster->cells, gRandomEngine);
                            auto&& diagram = Data::get()->getTerrainCells();
                            auto&& terrainCells = diagram->getCells();
                            auto&& cellCenter = terrainCells[cell]->getCenter();

                            auto meshQuery = Utils::castPointTo3DAdv(cellCenter)[0];
                            Q_ASSERT(meshQuery.cluster == cluster);

                            auto&& vertexBuffer = cluster->section->mainBuffer->vertices;
                            auto&& triangles = cluster->section->getIndices();
                            IndexType vIdx = triangles[meshQuery.triangleHit];
                            auto&& v = vertexBuffer[vIdx];

                            // TODO: Parametrize
                            if (randomChance() < 0.1f * v.humidity)
                            {
                                results.insert(cell);
                                continue;
                            }
                        }
                    }

        return results;
    }

    QSet<int> TerrainMod<ETerrainMod::Lake>::computeLakeArea(int seed)
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& terrainCells = diagram->getCells();
        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        spawn<DLineMarker>(terrainCells[seed]->getCenter(), 30000, Colors::cyan);

        QSet<int> lakeArea = { seed };

        // Make hull
        QSet<int> hull;

        auto hullCheck = [&](int idx) { return (clusterMap[idx]->type == ETerrainBlock::Flatland) && !lakeArea.contains(idx); };
        diagram->expandCellularCluster(&hull, seed, hullCheck);

        // TODO: parametrize
        const int targetSize = std::uniform_int_distribution<int>(5, 15)(gRandomEngine);

        // Grow randomly up to targetSize or limits
        while (lakeArea.size() < targetSize)
        {
            bool expanded = false;

            for (auto hullCopy = hull; int id : hullCopy)
            {
                lakeArea.insert(id);

                hull.remove(id);

                if (diagram->expandCellularCluster(&hull, id, hullCheck))
                {
                    expanded = true;
                    break;
                }
            }

            if (!expanded)
                break;
        }

        return lakeArea;
    }

    QSharedPointer<TerrainModBase> TerrainMod<ETerrainMod::Lake>::createLake(const QSet<int>& area)
    {
        static std::uniform_real_distribution<float> shoreMarginDist(100, 500);
        static std::uniform_int_distribution<int> bottomMarginDist(100, 500);
        
        Polygon2D lakePoly = Utils::makeBoundingPolygon(convertQSetToSTL(area)).front();
        auto cPts = lakePoly.getCPts();

        std::vector<ControlPoint> newControlPoints;
        for (int i = 0; i < cPts.getSize(); ++i)
        {
            int i2 = cPts.findIdx(i, 1);
            std::vector<GVector2D> edgePts = splitSegment(Segment2D{ cPts[i], cPts[i2] }, FFirstLastPolicy::First, false);
            for (auto&& ep : edgePts)
            {
                ControlPoint cp;
                cp.position = ep;
                cp.shoreMargin = shoreMarginDist(gRandomEngine);
                cp.bottomMargin = cp.shoreMargin + bottomMarginDist(gRandomEngine);
                newControlPoints <<= cp;
            }
        }
            
        return QSharedPointer<TerrainMod<ETerrainMod::Lake>>::create(area, lakePoly, newControlPoints);
    }

    void TerrainMod<ETerrainMod::Lake>::submitAll(ModAlterationsList* mal) const
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
                if (!GVector2D(prop.position).isInsidePolygon(areaPolygon.getPts()))
                    continue;

                float offset = calculateVertexOffset(prop);

                // Sculpt lake pan
                prop.position.setY(prop.position.y() - offset);

                if (offset > 0)
                {
                    // Params below water
                    prop.humidity = 1.0f;
                    setPackParam(&prop.packParams, 1, 0.0f);
                    prop.biomeTexWeights = 0;
                }
                else
                {
                    // Params above water
                    prop.humidity = std::min(1.0f, prop.humidity + 0.5f);
                    surfaceHeight = std::min(surfaceHeight, prop.position.y());
                }

                (*mal)[cluster->keyCell][i] << prop;
            }
        }

        // Env bounds
        auto boundWithHoles = Utils::makeBoundingPolygon(convertQSetToSTL(area));
        auto&& outerBound = boundWithHoles[0].getCPts();

        auto envBoundPoly = boundWithHoles[0] * 0.995f;

        auto&& cells = Data::get()->getTerrainCells()->getCells();
        for (int i = 0; i < outerBound.getSize(); ++i)
        {
            int i2 = outerBound.findIdx(i, 1);

            for (int lakeCell : area)
            {
                auto&& lakeCellPoly = cells[lakeCell]->getPts();
                if (!contains(lakeCellPoly, outerBound[i]) || !contains(lakeCellPoly, outerBound[i2]))
                    continue;

                auto envBound = QSharedPointer<EnvBound>::create(clusterMap[lakeCell]->keyCell);
                envBound->line = { envBoundPoly[i], envBoundPoly[i2] };
                //spawn<DLineMarker>(envBound->line, Colors::red, false, 1000);
                Data::get()->addEnviroBound(envBound);
            }
        }

        showWaterSurface();
    }

    float TerrainMod<ETerrainMod::Lake>::calculateVertexOffset(const TerrainMeshVertex& v) const
    {
        // Distances
        std::vector<float> cpDistances(controlPoints.size());
        float dMin = std::numeric_limits<float>::max();
        for (int i = 0; i < controlPoints.size(); ++i)
        {
            cpDistances[i] = distance(GVector2D(v.position), controlPoints[i].position);
            dMin = std::min(dMin, cpDistances[i]);

            if (dMin < 1.0f)
                return 0.0f;
        }

        // Weighted offset
        float weightedOffset = 0.0f;
        float wSum = 0.0f;
        for (int i = 0; i < controlPoints.size(); ++i)
        {
            float d = cpDistances[i];
            float shoreMargin = controlPoints[i].shoreMargin;
            float bottomMargin = controlPoints[i].bottomMargin;

            float offset = 0.0f;
            if (d < shoreMargin)
                offset = 0.0f;
            else if (d > bottomMargin)
                offset = depth;
            else
                offset = std::lerp(0, depth, (d - shoreMargin) / (bottomMargin - shoreMargin));

            float w = 1.0f - std::min(1.0f, d / (2.0f * dMin));
            wSum += w;

            weightedOffset += w * offset;
        }

        if (wSum == 0.0f)
            return depth;

        weightedOffset /= wSum;
        return weightedOffset;
    }

    void TerrainMod<ETerrainMod::Lake>::showWaterSurface() const
    {
        spawn<DLakeSurfaceMarker>(areaPolygon, surfaceHeight - 10.0f);
    }

    TerrainMeshVertex TerrainMod<ETerrainMod::Lake>::apply(const std::vector<TerrainMeshVertex>& alterations)
    {
        TerrainMeshVertex result;
        result.position.setY(std::numeric_limits<float>::max());

        for (auto&& alt : alterations)
            if (alt.position.y() < result.position.y())
                result = alt;

        return result;
    }

    void TerrainMod<ETerrainMod::Lake>::clearAll()
    {
        Data::get()->clearExactMarkers<DLakeSurfaceMarker>();
    }

    QVector4D TerrainMod<ETerrainMod::Lake>::getDebugColor() const
    {
        static auto color = QVector4D(0, 0.7, 0.7, 1);
        return color;
    }
}

void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::Lake>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainModBase&>(object);
    omniBin << object.controlPoints;
    omniBin << object.areaPolygon;
    omniBin << object.surfaceHeight;
    omniBin << object.depth;
}

void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::Lake>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainModBase&>(object);
    omniBin >> object.controlPoints;
    omniBin >> object.areaPolygon;
    omniBin >> object.surfaceHeight;
    omniBin >> object.depth;
}
