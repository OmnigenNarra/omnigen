#include "stdafx.h"
#include "TerrainBlock.h"

#include <Omnigen.h>
#include "Scene/Generation/OmnigenGeneration.h"
#include <Editor/Sections/Profiler/OmnigenProfiler.h>
#include <Mathematics/IntrSegment3Triangle3.h>
#include <Mathematics/Segment.h>
#include <noise/noise.h>
#include "../Lithomap/StageGeneration_Lithomap.h"

#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "../Lithomap/LithoCluster.h"
#include "Utils/Interpolation.h"

#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>

#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"

#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/SoilMaterial/AssetSoilMaterial.h"
#include "Scene/Generation/Common/Markers/MultiLineColoredMarker.h"

#define SMOOTH_PROFILE 0

namespace Generation
{
    void TerrainBlockClusterBase::registerBorderData()
    {
        auto faces = section->getIndices();

        for (quint32 i = 0; i < faces.size(); i += 3)
            registerBorderFace(section->mainBuffer->vertices, { faces.begin() + i, 3 }, i / 3);
    }

    void TerrainBlockClusterBase::registerBorderFace(const std::vector<TerrainMeshVertex>& vertexBuffer, const std::span<IndexType>& face, quint32 fi)
    {
        for (IndexType i : face)
        {
            auto comp2D = [&](const BorderPointInfo& bpi)
            {
                const auto& p = vertexBuffer[i].position;
                return bpi.pos.x() == p.x() && bpi.pos.z() == p.z();
            };

            // Each cluster should fill borderPoints during init
            auto&& bps = borderPoints[keyCell];
            auto bpit = std::ranges::find_if(bps, comp2D);
            if (bpit != bps.end())
                Data::get()->registerBorderFacePoint(keyCell, keyCell, vertexBuffer[i], fi);
        }
    }

    TerrainBlockClusterBase::TerrainBlockClusterBase(ETerrainBlock inType, const std::unordered_set<int>& inCells, std::optional<int> inKeyCell)
        : type(inType)
        , cells(inCells)
        , keyCell(inKeyCell ? *inKeyCell : *inCells.begin())
        , guid(makeGuid())
    {
        computeBiomeData();
    }

    void TerrainBlockClusterBase::computeBiomeData()
    {
        auto&& allCells = Data::get()->getTerrainCells()->getCells();

        // Compute biome
        std::map<GPoint, int> squareScores;
        for (int bId : cells)
            ++squareScores[allCells[bId]->getCenter().toGPoint()];

        // Find the dominant square
        int mostHits = 0;
        GPoint bestSq;
        for (auto&& [sq, hits] : squareScores)
            if (hits > mostHits)
            {
                bestSq = sq;
                mostHits = hits;
            }

        // Get temp and humidity if able
        auto biomeDomain = Generation::Data::get()->getDomainAtSquare(bestSq, EDomainType::Biome);
        if (biomeDomain)
        {
            // Assume average center <-> center distance = 50 triangles
            auto&& refPos = allCells[keyCell]->getCenter() * 0.02f;

            ETemperature t = biomeDomain->getData<EDomainType::Biome>()->temperature;
            auto prevT = getOffsetedEnum(t, -1);
            temperatureRange[0] = prevT ? PTemperature[*prevT] : 0.0f;
            temperatureRange[1] = PTemperature[t];

            EHumidity h = biomeDomain->getData<EDomainType::Biome>()->humidity;
            auto prevH = getOffsetedEnum(h, -1);
            humidityRange[0] = prevH ? PHumidity[*prevH] : 0.0f;
            humidityRange[1] = PHumidity[h];
        }
    }

    struct Edge2D
    {
        GVector2D v1;
        GVector2D v2;

        bool operator==(const Edge2D& other) const
        {
            // (v1, v2) == (v2, v1)
            return (v1 == other.v1 && v2 == other.v2) || (v1 == other.v2 && v2 == other.v1);
        }

        std::vector<GVector2D> getSplitPoints() const
        {
            return splitSegment<GVector2D>(Segment2D{ v1, v2 }, FFirstLastPolicy::Both, false);
        }
    };

    struct Edge2DHash
    {
        std::size_t operator()(Generation::Edge2D const& edge) const noexcept
        {
            // Associative hash, equal for (v1, v2) and (v2, v1)
            return qHash(qHash(edge.v1) + qHash(edge.v2));
        }
    };

    void TerrainBlockClusterBase::computeBorderPoints()
    {
        QHash<int, std::vector<std::array<int, 2>>> outerEdges;
        auto&& diagram = Data::get()->getTerrainCells();

        // For each of my cells
        for (int cellIdx : cells)
        {
            auto&& cell = diagram->getCellAt(cellIdx).getPolygon();
            auto&& cellPts = cell.getPts();

            // For each neighbor of that cell
            auto&& neighborsMap = diagram->getCellNeighborsAt(cellIdx);
            for (auto nit = neighborsMap.keyBegin(); nit != neighborsMap.keyEnd(); ++nit)
            {
                int neighbor = *nit;

                if (cells.contains(neighbor))
                    continue;

                // Find shared edges with my source cell
                auto ncpts = diagram->getCellAt(neighbor).getPolygon().getCPts();

                // Precalculate containment
                std::vector<int> idxMap;
                idxMap.reserve(ncpts.getSize());

                for (auto&& np : ncpts)
                    idxMap << indexOf(cellPts, np);

                // Check neighboring vertex pairs
                for (int i = 0; i < ncpts.getSize(); ++i)
                {
                    int i2 = ncpts.findIdx(i, 1);

                    if (idxMap[i] >= 0 && idxMap[i2] >= 0)
                    {
                        int lP = cellPts[idxMap[i]] < cellPts[idxMap[i2]] ? idxMap[i] : idxMap[i2];
                        int uP = cellPts[idxMap[i]] < cellPts[idxMap[i2]] ? idxMap[i2] : idxMap[i];

                        outerEdges[cellIdx].push_back({ lP, uP });
                    }
                }
            }
        }

        std::vector<std::array<GVector2D, 2>> outerVertices;
        for (auto eit = outerEdges.keyValueBegin(); eit != outerEdges.keyValueEnd(); ++eit)
        {
            auto&& [edgeBlockId, edges] = *eit;
            auto&& cellPts = diagram->getCellAt(edgeBlockId).getPolygon().getPts();

            for (auto&& edge : edges)
            {
                // Compute border points for this edge
                auto&& [aIdx, bIdx] = edge;
                auto&& a = cellPts[aIdx];
                auto&& b = cellPts[bIdx];

                std::unordered_set<GVector2D> edgeBorderPoints = splitSegment<GVector2D, std::unordered_set>(Segment2D{ a, b }, FFirstLastPolicy::Both, false);
                if (!contains(outerVertices, std::array{ a,b }) && !contains(outerVertices, std::array{ b,a })) //extra security
                    outerVertices.push_back({ a,b });

                // Assign border points to nearby cells 
                // (one bp may be assigned to multiple cells)
                auto& clusterBorderPoints = borderPoints[keyCell];
                for (auto&& bp : edgeBorderPoints)
                    clusterBorderPoints.push_back({ bp });
            }
        }
    }

    void TerrainBlockClusterBase::generate()
    {
        OmniProfile("Base geometry: " + toQString(std::string(magic_enum::enum_name(type))), true);

        computeBorderPoints();
        initialize();
        section = generateMesh();
    }

    QSharedPointer<BatchedSection<ClusterMeshBatchParams>> TerrainBlockClusterBase::generateMesh()
    {
        Q_ASSERT(false);
        return {};
    }

    void TerrainBlockClusterBase::clear()
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        borderPoints.clear();
        faceQuadTree.clear();
        vertexQuadTree.clear();
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockClusterBase::addCells(const std::unordered_set<int>& cellsToAdd, bool updateMeta /*= true*/)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        cells += cellsToAdd;
        if (updateMeta)
        {
            emit Editable::aboutToBeModified(metaCluster);
            metaCluster->cells += cellsToAdd;
            emit Editable::modified(metaCluster);
        }

        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockClusterBase::removeCells(const std::unordered_set<int>& cellsToRemove, bool updateMeta /*= true*/)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        for (auto&& cell : cellsToRemove)
        {
            cells.erase(cell);
            if (updateMeta)
            {
                emit Editable::aboutToBeModified(metaCluster);
                metaCluster->cells.erase(cell);
                emit Editable::modified(metaCluster);
            }
        }

        if (!cells.empty() && cellsToRemove.contains(keyCell))
            keyCell = *cells.begin();
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockClusterBase::setCells(const std::unordered_set<int>& newCells, bool updateMeta /*= true*/)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        if (updateMeta)
        {
            emit Editable::aboutToBeModified(metaCluster);
            for (auto&& cell : cells)
                metaCluster->cells.erase(cell);
            metaCluster->cells.insert(newCells.begin(), newCells.end());
            emit Editable::modified(metaCluster);
        }

        cells = newCells;
        if (!cells.empty() && !cells.contains(keyCell))
            keyCell = *cells.begin();
        emit Editable::modified(sharedFromThis());
    }

    noise::model::Plane& getGlobalNoise()
    {
        static noise::module::RidgedMulti noiseSource;
        static noise::model::Plane noiseModel;
        static bool isInit = false;

        static std::mutex guard;
        std::scoped_lock lock(guard);
        if (!isInit)
        {
            noiseSource.SetSeed(std::rand());
            noiseModel.SetModule(noiseSource);
            isInit = true;
        }

        return noiseModel;
    }

    void TerrainBlockClusterBase::computeTexSlots()
    {
        emit Editable::aboutToBeModified(sharedFromThis());

        auto&& [terrainChunks, chunkBlocksMap, blockChunkMap] = Data::get()->getTerrainChunkData();
        auto&& chunk = terrainChunks[blockChunkMap[keyCell]];

        terrainTexPackSlot = indexOf(chunk->getTerrainTextureIds(), metaCluster->getTerrainTexPack());
        Q_ASSERT(terrainTexPackSlot >= 0);

        if (const quint32 btp = metaCluster->getBiomeTexPack(); btp != quint32(-1))
        {
            biomeTexPackSlot = indexOf(chunk->getBiomeTextureIds(), btp);
            Q_ASSERT(biomeTexPackSlot >= 0);
        }
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockClusterBase::computeNormals()
    {
        // Reset normals;
        auto vertices = section->getVertices();
        tbb::parallel_for(0, int(vertices.size()), [&](int i)
            {
                vertices[i].normal = {};
            });

        // Triangles
        auto&& vertexBuffer = section->mainBuffer->vertices;
        auto&& triangles = section->getIndices();
        tbb::parallel_for(0, int(triangles.size() / 3), [&](int ti)
            {
                IndexType i = ti * 3;

                auto&& mp1 = vertexBuffer[triangles[i]];
                auto&& mp2 = vertexBuffer[triangles[i + 1]];
                auto&& mp3 = vertexBuffer[triangles[i + 2]];

                QVector3D faceNormal = computeFaceNormal({ mp1.position, mp2.position, mp3.position });

                mp1.normal += faceNormal;
                mp2.normal += faceNormal;
                mp3.normal += faceNormal;
            });

        auto&& borderPoints = Data::get()->getTerrainBorderPoints();
        tbb::parallel_for(0, int(vertices.size()), [&](int i)
            {
                auto&& mp = vertices[i];

                auto borderDataIt = borderPoints.constFind(mp.position);
                bool gbpOK = (borderDataIt != borderPoints.constEnd());

                if (!gbpOK)
                    mp.normal.normalize();
                else
                    mp.normal = borderDataIt->normal;

                if (mp.normal.y() < 0.0f)
                    mp.biomeTexWeights = 0;
            });
    }

    void TerrainBlockClusterBase::applySmoothing(TerrainMeshVertex* tmv, const std::vector<WeightedBorderPoint>& weightedBPs, float smoothingStrength)
    {
#if SMOOTH_PROFILE
        OmniProfile("Smoothing application");
#endif
        // Weight sum for normalization
        float weightSum = std::accumulate(weightedBPs.begin(), weightedBPs.end(), 0.0f, [&](float val, auto& next) { return val + next.weight; });
        if (weightSum == 0.0f)
            weightSum = 1.0f;

        // Universal weighted getter
        auto getWeightedAverage = [&](auto&& getVal) 
        { 
            return std::accumulate(weightedBPs.begin(), weightedBPs.end(), 0.0f, [getVal](float val, auto& next) { return val + getVal(next) * next.weight; }) / weightSum;
        };

        // Compute all values
        float blendedHeight = std::lerp(tmv->position.y(), getWeightedAverage([](auto& next) { return next.bpInfo.pos.y(); }), smoothingStrength);
        float blendedTemperature = std::lerp(tmv->temperature, getWeightedAverage([&](auto& next) { return next.bpInfo.temperature; }), smoothingStrength);
        float blendedHumidity = std::lerp(tmv->humidity, getWeightedAverage([&](auto& next) { return next.bpInfo.humidity; }), smoothingStrength);
		std::vector<float> blendedTerrainWeights(4, 0.f);
		std::vector<float> blendedBiomeWeights(4, 0.f);
		std::vector<float> blendedPackParams(4, 0.f);
        for (int j = 0; j < 4; ++j)
        {
            blendedTerrainWeights[j] += std::lerp(getTexWeight(tmv->terrainTexWeights, j), getWeightedAverage([&](auto& next) { return getTexWeight(next.bpInfo.terrainTexWeights, j); }), smoothingStrength);
            blendedBiomeWeights[j] += std::lerp(getTexWeight(tmv->biomeTexWeights, j), getWeightedAverage([&](auto& next) { return getTexWeight(next.bpInfo.biomeTexWeights, j); }), smoothingStrength);
            blendedPackParams[j] += std::lerp(getTexWeight(tmv->packParams, j), getWeightedAverage([&](auto& next) { return getTexWeight(next.bpInfo.packParams, j); }), smoothingStrength);
        }

        // Assign to vertex
        float baseY = tmv->position.y();
        float bestY = weightedBPs[0].bpInfo.pos.y();
        tmv->position.setY(std::clamp(blendedHeight, std::min(baseY, bestY), std::max(baseY, bestY))); // Must not create local extrema
        tmv->terrainTexWeights = compileTexWeights(blendedTerrainWeights);
        tmv->biomeTexWeights = compileTexWeights(blendedBiomeWeights);
        tmv->packParams = compilePackParams(blendedPackParams);
        tmv->temperature = blendedTemperature;
        tmv->humidity = blendedHumidity;
    }

    void TerrainBlockClusterBase::smoothMesh()
    {
#if SMOOTH_PROFILE
        OmnigenProfilerSegment seg0("all smoothing", true);
#endif
        //std::vector<DMultiLineColoredMarker::LineData> lines;

        // Edge case: The one and only cluster in the world.
        if (borderPoints.isEmpty())
            return;

        Q_ASSERT(section);
        const float snapToPointDist = 2.0f;

        auto&& bpTree = getBPQuadTree();
        auto&& BPs = *borderPoints.constFind(keyCell);

        {
#if SMOOTH_PROFILE
            OmnigenProfilerSegment seg3("Vertex processing");
#endif
            // Process each vertex
			for(auto&& vertex : section->getVertices())
			{
                // Find Border Points in smoothing range
                auto affectingBPs = bpTree.map_all_nearest(vertex.position.x(), vertex.position.z(), smoothingParams.smoothingRadius);
                if (affectingBPs.empty())
                    continue;

                // Snapping
                if (affectingBPs.begin()->first == 0.0f)
                {
                    applySmoothing(&vertex, { { BPs[affectingBPs.begin()->second->data], 1.0f } }, 1.0f);
                    continue;
                }

                // Compute weights
                float maxWeight = 0.0f;
				std::vector<WeightedBorderPoint> weightedBPs;
                {
#if SMOOTH_PROFILE
                    OmnigenProfilerSegment seg4("BP weighting");
#endif
					// Consider up to 2 BPs
					auto endIt = ++affectingBPs.begin();
					if (affectingBPs.size() > 1)
						++endIt;

					for (auto it = affectingBPs.begin(); it != endIt; ++it)
					{
                        auto&& [dist, bpNode] = *it;
						auto&& bpInfo = BPs[bpNode->data];

						float weight = 1.0f - dist / smoothingParams.smoothingRadius;

                        if (smoothingMultiplierMap.contains(vertex.position))
                            weight *= 1.0f - smoothingMultiplierMap[vertex.position];

                        maxWeight = std::max(maxWeight, weight);
                        weightedBPs <<= WeightedBorderPoint{ bpInfo, weight };
                        //lines.push_back({ std::vector{ vertex.pos, QVector3D(vertex.pos.x(), bpInfo.pos.y(), vertex.pos.z()), bpInfo.pos }, QVector4D(strength, strength, strength, 1) });
					}

                    // "Normalize" weights so best BP has weight = 1
                    if (maxWeight > 0)
                        for (auto&& smoothingData : weightedBPs)
                            smoothingData.weight /= maxWeight;
                }

				applySmoothing(&vertex, weightedBPs, maxWeight);
			}
        }

        //spawn<DMultiLineColoredMarker>(lines);
    }

    // This technique ensures the interpolation curves join in the middle like a linear function
    float halfSmoothstep(float t)
    {
        static Interpolation::Technique01<EInterpolation01::Smoothstep> smoothstep;
        float x = t * 0.5f + 0.5f;
        return 2.0f * smoothstep(x) - 1.0f;
    }

    ClusterMeshBatchParams TerrainBlockClusterBase::makeBatchParams(std::optional<QVector4D> color) const
    {
        // In practice 1 geometry per meta cluster
        ClusterMeshBatchParams params;
        params.color = color ? *color : ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(type);
        params.renderMode = ERenderType::Wireframe;
        params.primitiveType = GL_TRIANGLES;
        params.metaClusterGuid = metaCluster->getGuid(); 
        return params;
    }

    Polygon2D TerrainBlockClusterBase::calculatePolygon(bool forceCW /*= false*/) const
    {
        auto polygon = Utils::makeBoundingPolygon(cells).front();
        if (forceCW && !polygon.isCW())
            polygon.reverseOrder();

        return polygon;
    }

    std::vector<QVector3D> TerrainBlockClusterBase::raycastDataFrom2D(const GVector2D& in2DPt, const ComparePointPred& Pred)
    {
        std::vector<const tml::qtree<float, IndexType>::node_type*> cellNodes;
        const bool treeMatch = getFaceQuadTree().search(in2DPt.x, in2DPt.z, gMaxTriangleSideLength, cellNodes);
        auto&& triangles = section->getIndices();
        auto&& vertexBuffer = section->mainBuffer->vertices;

        std::vector<QVector3D> results;
        for (auto&& cellHit : cellNodes)
        {
            Q_ASSERT(cellHit->data + 2 < triangles.size());

            gte::Triangle3<float> triangle{
                QtoV3(vertexBuffer[triangles[cellHit->data + 0]].position),
                QtoV3(vertexBuffer[triangles[cellHit->data + 1]].position),
                QtoV3(vertexBuffer[triangles[cellHit->data + 2]].position)
            };

            // Enlarge the triangle slightly to catch points directly on the edge 
            // TODO: Make it work without false positives 
            auto center = (triangle.v[0] + triangle.v[1] + triangle.v[2]) / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                auto dir = VtoQ3(triangle.v[i] - center);
                triangle.v[i] = center + QtoV3(dir * 1.1);
            }

            gte::FIQuery<float, gte::Segment3<float>, gte::Triangle3<float>> query;
            const auto resultQ = query(gte::Segment3<float>{ QtoV3(QVector3D(in2DPt.x, WORLD_LOWEST_Y, in2DPt.z)),
                QtoV3(QVector3D(in2DPt.x, WORLD_HIGHEST_Y, in2DPt.z)) }, triangle);

            if (resultQ.intersect)
                results <<= VtoQ3(resultQ.point);
        }

        std::ranges::sort(results, Pred);
        return results;
    }

    // WARNING: THIS MIRRORS raycastDataFrom2D !!! 
    std::vector<MeshQueryData> TerrainBlockClusterBase::raycastDataFrom2DAdv(const GVector2D& in2DPt, const ComparePointPred& Pred /*= PointByHeightPred()*/)
    {
        std::vector<const tml::qtree<float, IndexType>::node_type*> cellNodes;
        const bool treeMatch = getFaceQuadTree().search(in2DPt.x, in2DPt.z, gMaxTriangleSideLength, cellNodes);
        auto&& triangles = section->getIndices();
        auto&& vertexBuffer = section->mainBuffer->vertices;

        std::vector<MeshQueryData> results;
        for (auto&& cellHit : cellNodes)
        {
            gte::Triangle3<float> triangle{
                QtoV3(vertexBuffer[triangles[cellHit->data + 0]].position),
                QtoV3(vertexBuffer[triangles[cellHit->data + 1]].position),
                QtoV3(vertexBuffer[triangles[cellHit->data + 2]].position)
            };

            auto center = (triangle.v[0] + triangle.v[1] + triangle.v[2]) / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                auto dir = VtoQ3(triangle.v[i] - center);
                triangle.v[i] = center + QtoV3(dir * 1.01);
            }

            gte::FIQuery<float, gte::Segment3<float>, gte::Triangle3<float>> query;
            const auto resultQ = query(gte::Segment3<float>{ QtoV3(QVector3D(in2DPt.x, WORLD_LOWEST_Y, in2DPt.z)),
                QtoV3(QVector3D(in2DPt.x, WORLD_HIGHEST_Y, in2DPt.z)) }, triangle);

            if (resultQ.intersect)
                results <<= MeshQueryData{
                    VtoQ3(resultQ.point),
                    sharedFromThis(),
                    cellHit->data
            };
        }

        std::ranges::sort(results, [&](const MeshQueryData& A, const MeshQueryData& B) {return Pred(A.pos, B.pos); });
        return results;
    }

    const TerrainBlockClusterBase::VertexQuadTree& TerrainBlockClusterBase::getVertexQuadTree()
    {
        static std::mutex treeGuard;

        if (std::scoped_lock lock(treeGuard); !vertexQuadTree)
            calculateVertexQuadTree();

        return *vertexQuadTree;
    }

    void TerrainBlockClusterBase::calculateFacePointQuadTree()
    {
        auto&& terrainCells = Data::get()->getTerrainCells()->getCells();

        BoundingBox bounds;
        for (int cellIdx : cells)
        {
            auto&& cell = terrainCells[cellIdx];
            for (auto&& p : cell)
                bounds.expandToContain(p);
        }

        faceQuadTree = QSharedPointer<FaceQuadTree>::create(bounds.nbl.x(), bounds.nbl.z() + bounds.sizes.z(), bounds.nbl.x() + bounds.sizes.x(), bounds.nbl.z());

        // Triangles
        auto&& triangles = section->getIndices();
        auto&& vertexBuffer = section->mainBuffer->vertices;
        for (size_t i = 0; i < triangles.size(); i += 3)
        {
            auto&& mp1 = vertexBuffer[triangles[i]];
            auto&& mp2 = vertexBuffer[triangles[i + 1]];
            auto&& mp3 = vertexBuffer[triangles[i + 2]];

            const float ptX = (mp1.position.x() + mp2.position.x() + mp3.position.x()) / 3.0f;
            const float ptZ = (mp1.position.z() + mp2.position.z() + mp3.position.z()) / 3.0f;

            faceQuadTree->add_node(ptX, ptZ, static_cast<IndexType>(i));
        }
    }

    auto TerrainBlockClusterBase::getFaceQuadTree() -> const FaceQuadTree&
    {
        static std::mutex treeGuard;

        if (std::scoped_lock lock(treeGuard); !faceQuadTree)
            calculateFacePointQuadTree();

        return *faceQuadTree;
    }

	auto TerrainBlockClusterBase::getBPQuadTree() -> const BPQuadTree&
	{
		static std::mutex treeGuard;

		if (std::scoped_lock lock(treeGuard); !faceQuadTree)
			calculateBPQuadTree();

		return *bpQuadTree;
	}

	QVector4D TerrainBlockClusterBase::getDebugColor()
    {
        return QVector4D(0.5, 0.5, 0.5, 0.5);
    }

    void TerrainBlockClusterBase::calculateVertexQuadTree()
    {
#if SMOOTH_PROFILE
        OmniProfile("vertex qtree");
#endif
        auto&& terrainCells = Data::get()->getTerrainCells()->getCells();

        //TODO: Potentially factor out as it is a duplicate calculation for the face qTree
        BoundingBox bounds;
        for (const int cellIdx : cells)
        {
            auto&& cell = terrainCells[cellIdx];
            for (auto&& p : cell)
                bounds.expandToContain(p);
        }

        vertexQuadTree = QSharedPointer<VertexQuadTree>::create(bounds.nbl.x(), bounds.nbl.z() + bounds.sizes.z(), bounds.nbl.x() + bounds.sizes.x(), bounds.nbl.z());

        auto vertices = section->getVertices();
        for (IndexType i = 0; i < vertices.size(); i++)
        {
            const auto& v = vertices[i].position;
            vertexQuadTree->add_node(v.x(), v.z(), i);
        }
    }

	void TerrainBlockClusterBase::calculateBPQuadTree()
	{
		//TODO: Potentially factor out as it is a duplicate calculation for the face qTree
		auto&& terrainCells = Data::get()->getTerrainCells()->getCells();
		BoundingBox bounds;
		for (const int cellIdx : cells)
		{
			auto&& cell = terrainCells[cellIdx];
			for (auto&& p : cell)
				bounds.expandToContain(p);
		}

		bpQuadTree = QSharedPointer<VertexQuadTree>::create(bounds.nbl.x(), bounds.nbl.z() + bounds.sizes.z(), bounds.nbl.x() + bounds.sizes.x(), bounds.nbl.z());

        auto&& BPs = borderPoints[keyCell];
		for (IndexType i = 0; i < BPs.size(); i++)
		{
            auto&& bp = BPs[i];
            bpQuadTree->add_node(bp.pos.x(), bp.pos.z(), i);
		}
	}

	std::unordered_set<int> ClusterDataBase::computeCandidates(const std::unordered_set<int>& metaCluster, const std::unordered_set<int>& alreadyAssigned)
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& [lithoMap, lithoClusters] = Data::get()->getLithomap();
        auto&& clusterTraits = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetClusterTraits>(type);

        auto checkCell = [&](int id)
        {
            if (cells.size() >= clusterTraits.maxSize)
                return false;

            // Cannot exceed meta cluster
            if (!metaCluster.contains(id))
                return false;

            // Cannot reuse cell
            if (alreadyAssigned.contains(id))
                return false;

            // Cannot have duplicate cell
            if (cells.contains(id))
                return false;

            // Cannot cross lithoCluster bounds
            if (lithoId != getCellLithoId(id))
                return false;

            // Cannot exceed max height difference
            if (qAbs(height - getCellHeight(id)) > clusterTraits.maxHeightDiff)
                return false;

            // Cannot have a differing domain set
            if (domains != getCellDomains(id))
                return false;

            return true;
        };

        std::unordered_set<int> candidates;
        for (int id : cells)
        {
            auto&& neighborsMap = diagram->getCellNeighborsAt(id);
            for (auto nit = neighborsMap.keyBegin(); nit != neighborsMap.keyEnd(); ++nit)
                if (checkCell(*nit))
                    candidates << *nit;
        }

        return candidates;
    }

    ETerrainBlock ClusterDataBase::getType(int id)
    {
        auto&& blockTypeMap = Data::get()->getBlockTypeMap();
        return blockTypeMap[id];
    }

    float ClusterDataBase::getCellHeight(int id)
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& dem = Data::get()->getDEM();
        return dem->heightData.sample(diagram->getCenters()[id]);
    }

    std::unordered_set<qint64> ClusterDataBase::getCellDomains(int id)
    {
        std::unordered_set<qint64> result;
        auto&& diagram = Data::get()->getTerrainCells();

        GPoint sq = diagram->getCenters()[id].toGPoint();
        auto domains = Data::get()->getDomainsAtSquare(sq);
        for (auto&& [type, domain] : domains)
            result << domain->getGuid();

        return result;
    }

    qint64 ClusterDataBase::getCellLithoId(int id)
    {
        auto&& [lithomap, lithoClusters] = Data::get()->getLithomap();
        return lithoClusters[lithomap[id]]->getType();
    }

    std::unordered_set<int> ClusterDataBase::customGrow(const std::unordered_set<int>& candidates)
    {
        return customGrowFilterIslands(candidates, &cells);
    };

    TerrainBlockMetaClusterBase::TerrainBlockMetaClusterBase(ETerrainBlock inType, const std::unordered_set<int>& inCells)
        : type(inType)
        , cells(inCells)
        , guid(makeGuid())
    {
    }


    void TerrainBlockMetaClusterBase::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        // This should be replaced with Terrain Wetness Index (TWI) queries
        auto&& thresholdData = lithoCluster->getThresholdData();
        float rockIdx = 0;
        for (auto it = thresholdData.begin(); it != thresholdData.end(); ++it)
        {
            auto&& [minIh, params] = *it;
            if (averageIHLevel >= minIh)
                rockIdx = params.texSlot;
        }

        packParams = quint32(rockIdx * texSlotConverter) /* terrain slot */ + (255 << 8) /* vegetation = max */;
    }

    std::unordered_set<int> TerrainBlockMetaClusterBase::selectNonClusterCells() const
    {
        auto nonClusterCells = cells;

        for (auto&& cluster : clusters)
            for (auto&& cell : cluster->cells)
                nonClusterCells.erase(cell);

        return nonClusterCells;
    }

    std::unordered_set<int> TerrainBlockMetaClusterBase::selectClusterCells() const
    {
        std::unordered_set<int> clusterCells;

        for (auto&& cluster : clusters)
            clusterCells += cluster->cells;

        return clusterCells;
    }

    std::vector<std::unordered_set<int>> TerrainBlockMetaClusterBase::selectCellsPerCluster() const
    {
        std::vector<std::unordered_set<int>> clusterCells;

        for (auto&& cluster : clusters)
            clusterCells << cluster->cells;

        return clusterCells;
    }

    Polygon2D TerrainBlockMetaClusterBase::calculatePolygon(bool forceCW /*= false*/) const
    {
        if (!forceCW)
            return Utils::makeBoundingPolygon(cells).front();

        auto polygon = Utils::makeBoundingPolygon(cells).front();
        if (!polygon.isCW())
            polygon.reverseOrder();

        return polygon;
    }

    void TerrainBlockMetaClusterBase::spawnClusters()
    {
        static const float maxDiff = 300.0f;

        std::unordered_set<int> alreadyAssigned = selectClusterCells();

        auto&& diagram = Data::get()->getTerrainCells();
        auto&& dem = Data::get()->getDEM();

        for (int id : cells)
        {
            if (alreadyAssigned.contains(id)) // block already assigned
                continue;

            // New cluster!
            auto clusterData = ETerrainBlockConstexpr::UseIn<EAC::CreateClusterData>(type, this, id);
            alreadyAssigned << id;

            while (true)
            {
                std::unordered_set<int> candidates = clusterData->computeCandidates(cells, alreadyAssigned);
                std::unordered_set<int> added = clusterData->customGrow(candidates);

                if (added.empty())
                {
                    emit Editable::aboutToBeModified(sharedFromThis());
                    clusters << ETerrainBlockConstexpr::UseIn<EAC::CreateTerrainCluster>(type, *clusterData);
                    emit Editable::modified(sharedFromThis());
                    break;
                }

                alreadyAssigned += added;
            }
        }

        Q_ASSERT(alreadyAssigned.size() == cells.size());

        for (auto&& cluster : clusters)
        {
            emit Editable::aboutToBeModified(cluster);
            cluster->metaCluster = sharedFromThis();
            emit Editable::modified(cluster);
        }
    }

    void TerrainBlockMetaClusterBase::addCells(const std::unordered_set<int>& inCells)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        cells.insert(inCells.begin(), inCells.end());
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockMetaClusterBase::removeCells(const std::unordered_set<int>& inCells)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        for (auto&& cell : inCells)
            cells.erase(cell);
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockMetaClusterBase::addCluster(const QSharedPointer<TerrainBlockClusterBase>& cluster)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        clusters << cluster;

        emit Editable::aboutToBeModified(cluster);
        cluster->metaCluster = sharedFromThis();
        emit Editable::modified(cluster);

        cells.insert(cluster->cells.begin(), cluster->cells.end());
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockMetaClusterBase::removeCluster(const QSharedPointer<TerrainBlockClusterBase>& cluster)
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        clusters.erase(std::find(clusters.begin(), clusters.end(), cluster));

        emit Editable::aboutToBeModified(cluster);
        cluster->metaCluster = nullptr;
        emit Editable::modified(cluster);

        for (auto&& cell : cluster->cells)
            cells.erase(cell);
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockMetaClusterBase::setClusters(const std::vector<QSharedPointer<TerrainBlockClusterBase>>& newClusters)
    {
        emit Editable::aboutToBeModified(sharedFromThis());

        for (auto&& cluster : clusters)
        {
            emit Editable::aboutToBeModified(cluster);
            cluster->metaCluster = nullptr;
            for (auto&& cell : cluster->cells)
                cells.erase(cell);
            emit Editable::modified(cluster);
        }
        clusters.clear();
        cells.clear();

        for (auto&& cluster : newClusters)
            addCluster(cluster);

        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockMetaClusterBase::initialize()
    {
        emit Editable::aboutToBeModified(sharedFromThis());
        computeBaseMaterial();
        emit Editable::modified(sharedFromThis());
    }

    void TerrainBlockMetaClusterBase::spawnBigClusters()
    {
        std::unordered_set<int> alreadyAssigned;
        alreadyAssigned.reserve(cells.size());

        auto&& diagram = Data::get()->getTerrainCells();

        std::unordered_set<int> neighborsCandidatesForSpawn;
        neighborsCandidatesForSpawn.reserve(cells.size());

        neighborsCandidatesForSpawn += *cells.begin();

        while (true)
        {
            const int currId = *neighborsCandidatesForSpawn.begin();
            alreadyAssigned += currId;
            auto clusterData = ETerrainBlockConstexpr::UseIn<::EAC::CreateClusterData>(type, this, currId);

            // cluster grow
            while (true)
            {
                std::unordered_set<int> candidates = clusterData->computeCandidates(cells, alreadyAssigned);
                std::unordered_set<int> added = clusterData->customGrow(candidates);
                if (added.empty())
                {
                    emit Editable::aboutToBeModified(sharedFromThis());
                    clusters << ETerrainBlockConstexpr::UseIn<::EAC::CreateTerrainCluster>(type, *clusterData);
                    emit Editable::modified(sharedFromThis());
                    break;
                }
                alreadyAssigned += added;
            }

            // calculate candidates for next spawn (neighbors of current cluster)
            neighborsCandidatesForSpawn.clear();
            for (int id : alreadyAssigned) // clusterData->cells
            {
                const auto& currCell = diagram->getCellAt(id);
                const auto& neighbors = currCell.getNeighbors();
                for (auto it = neighbors.keyBegin(); it != neighbors.keyEnd(); ++it)
                {
                    const int neighbor = *it;
                    if (cells.contains(neighbor) && !alreadyAssigned.contains(neighbor))
                        neighborsCandidatesForSpawn += neighbor;
                }
            }

            if (neighborsCandidatesForSpawn.empty())
                break;
        }

        Q_ASSERT(alreadyAssigned.size() == cells.size());

        for (auto&& cluster : clusters)
        {
            emit Editable::aboutToBeModified(cluster);
            cluster->metaCluster = sharedFromThis();
            emit Editable::modified(cluster);
        }
    }


    void TerrainBlockMetaClusterBase::computeBaseMaterial()
    {
        auto&& diagram = Data::get()->getTerrainCells();
        auto&& [lithomap, lithoClusters] = Data::get()->getLithomap();
        auto&& dem = Data::get()->getDEM();

        // Collect data from all cells
        std::map<int, int> largestLithoCluster;
        std::map<QSharedPointer<DDomain>, int> dominantBiome;
        float avgIhLvl = 0.0f;

        Q_ASSERT(!cells.empty());

        for (auto&& cell : cells)
        {
            largestLithoCluster[lithomap[cell]] += 1;

            GVector2D refPoint = diagram->getCells()[cell]->getCenter();
            GPoint sq = refPoint.toGPoint();
            auto biomeDomain = Data::get()->getDomainAtSquare(sq, EDomainType::Biome);
            dominantBiome[biomeDomain] += 1;

            float ihLvl = dem->levelData.sample(refPoint);
            avgIhLvl += ihLvl;
        }

        // Choose values for whole metacluster
        int bestLithoCount = 0;
        int bestLithoId;

        int bestBiomeCount = 0;
        QSharedPointer<DDomain> bestBiome;

        avgIhLvl /= float(cells.size());

        for (auto [lithoClusterId, count] : largestLithoCluster)
            if (count > bestLithoCount)
            {
                bestLithoCount = count;
                bestLithoId = lithoClusterId;
            }

        for (auto&& [domainPtr, count] : dominantBiome)
            if (count > bestBiomeCount)
            {
                bestBiomeCount = count;
                bestBiome = domainPtr;
            }

        auto&& lithoCluster = lithoClusters[bestLithoId];
        qint64 lithoType = lithoCluster->getType();
        terrainTexPack = Data::get()->getTerrainTexturePacks()[lithoType];

        if (bestBiome)
        {
            auto&& coverAssets = Omnigen::get()->getAssetsSection()->getAssets<EAsset::SoilMaterial>();
            auto&& coverAssetsMap = Data::get()->getBiomeTexturePacks();
            std::vector<qint64> validSoils;

            for (auto cit = coverAssetsMap.keyValueBegin(); cit != coverAssetsMap.keyValueEnd(); ++cit)
            {
                auto&& [id, arrayIdx] = *cit;
                auto&& asset = coverAssets[id];

                // Invalid rock material
                if (!asset->getAllowedRockMaterials().contains(lithoType))
                    continue;

                auto&& biomeData = bestBiome->getData<EDomainType::Biome>();

                // Invalid temperature
                if (auto&& [minTemp, maxTemp] = asset->getTemperatureRange(); biomeData->temperature < minTemp || biomeData->temperature > maxTemp)
                    continue;

                // Invalid humidity
                if (auto&& [minHum, maxHum] = asset->getHumidityRange(); biomeData->humidity < minHum || biomeData->humidity > maxHum)
                    continue;

                // All good
                validSoils << id;
            }

            if (!validSoils.empty())
            {
                // TODO: Keep all options, choose during Chunk creation?
                std::uniform_int_distribution<int> soilIdxDist(0, validSoils.size() - 1);
                biomeTexPack = coverAssetsMap.value(validSoils[soilIdxDist(gRandomEngine)]);
            }
        }

        computePackParams(lithoCluster, bestBiome, avgIhLvl);
    }

    void TerrainBlockMetaClusterBase::generate()
    {
        for (auto&& cluster : clusters)
            cluster->generate();
    }
}

std::unordered_set<int> customGrowFilterIslands(const std::unordered_set<int>& candidates, std::unordered_set<int>* clusterCells)
{
    auto&& diagram = Generation::Data::get()->getTerrainCells();

    std::unordered_set<int> neighbors;
    neighbors.reserve(clusterCells->size());

    // collect all outer neighbors, that are not in candidates
    for (int id : *clusterCells)
    {
        const auto& neighborsMap = diagram->getCellNeighborsAt(id);
        for (auto nit = neighborsMap.keyBegin(); nit != neighborsMap.keyEnd(); ++nit)
        {
            const int neighbor = *nit;
            if (clusterCells->contains(neighbor) || candidates.contains(neighbor))
                continue;
            neighbors << neighbor;
        }
    }

    // find if some neighbor is potential "island"
    // and is surrounded by current cluster and candidates only
    // Also collect set of suspects - for more complex islands, where
    // island can consist of several cells.
    // So, suspect is cell, which has only neighbors from cluster cells, candidates, or neighbors of cluster.
    std::unordered_set<int> suspects;
    suspects.reserve(neighbors.size());
    for (int neighbor : neighbors)
    {
        const auto& neighborsMap = diagram->getCellNeighborsAt(neighbor);
        bool isIsland = true;
        bool isSuspect = true;
        for (auto nit = neighborsMap.keyBegin(); nit != neighborsMap.keyEnd(); ++nit)
        {
            const int currNid = *nit;
            if (clusterCells->contains(currNid) || candidates.contains(currNid))
                continue;
            isIsland = false;
            if (neighbors.contains(currNid))
                continue;

            isSuspect = false;
            break;
        }

        if (isIsland) // find simple island
            return std::unordered_set<int>();

        if (isSuspect)
            suspects << neighbor;
    }

    // Filter suspects and find if there is complex island - set of cells with links only to suspect set of cluster cells / candidates
    for (auto iter = suspects.begin(); iter != suspects.end(); ++iter)
    {
        const int currSuspect = *iter;
        const auto& neighborsMap = diagram->getCellNeighborsAt(currSuspect);
        bool isJustified = false;
        for (auto nit = neighborsMap.keyBegin(); nit != neighborsMap.keyEnd(); ++nit)
        {
            const int currNid = *nit;
            if (!clusterCells->contains(currNid) && !candidates.contains(currNid) && !suspects.contains(currNid))
            {
                isJustified = true;
                break;
            }
        }
        if (isJustified)
        {
            suspects -= currSuspect;
            if (suspects.empty())
                break;
            // start next iteration from begin
            iter = suspects.begin();
        }
    }

    if (!suspects.empty())
    {
        // find complex island
        return std::unordered_set<int>();
    }

    // If there is no "islands", simply return all cells
    (*clusterCells) += candidates;
    return candidates;
}

std::array<QVector3D, 2> getMinMaxHeightCellPts(const Polygon2D& cell)
{
    auto&& dem = Generation::Data::get()->getDEM();

    std::array<QVector3D, 2> result =
    {
        QVector3D(0, std::numeric_limits<float>::max(), 0),
        QVector3D(0, std::numeric_limits<float>::min(), 0)
    };

    for (auto&& p : cell.getPts())
    {
        float h = dem->heightData.sample(p);

        if (h < result[0].y())
            result[0] = { p.x, h, p.z };

        if (h > result[1].y())
            result[1] = { p.x, h, p.z };
    }

    return result;
}

void omniSave(const Generation::TerrainBlockClusterBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.type;
    omniBin << object.cells;
    omniBin << object.keyCell;
    omniBin << object.borderPoints;
    omniBin << object.guid;
    omniBin << object.temperatureRange;
    omniBin << object.humidityRange;
}

void omniLoad(Generation::TerrainBlockClusterBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> const_cast<Generation::ETerrainBlock&>(object.type);
    omniBin >> object.cells;
    omniBin >> object.keyCell;
    omniBin >> object.borderPoints;
    omniBin >> object.guid;
    omniBin >> object.temperatureRange;
    omniBin >> object.humidityRange;
}
