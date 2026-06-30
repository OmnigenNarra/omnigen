#pragma once
#include "stdafx.h"
#include "StageGeneration_Lithomap.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/Interpolation.h"

#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"

#include "Omnigen.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"

#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>

#include "Scene/Generation/Common/Markers/BatchMarker.h"
#include "LithomapMarker.h"

namespace Generation
{
    // Generate a Voronoi Diagram to cover all world in 2D. It will be the foundation of further steps.
    void StageGen<EGenerationStage::Lithomap>::initialize()
    {
        if (!Data::get()->getTerrainCells())
        {
            generateTerrainCells();
            initLithoMap();
            visualizeLithomap();
        }
    }

    // Assign a lithological type to each cell.
    // These cells may form clusters with shared data.
    bool StageGen<EGenerationStage::Lithomap>::autoGen()
    {
        generateLithoMap();
        return true;
    }

    void StageGen<EGenerationStage::Lithomap>::clear()
    {
        cellToLithoType.clear();
        colorByLithoType.clear();
        Data::get()->setLithomap({}, {});
        Data::get()->setTerrainCells({});
        Data::get()->clearBlockQuadTree();
        clearAllBatches<LithomapBatchParams>();
    }

    void StageGen<EGenerationStage::Lithomap>::finalize()
    {
        finalizeLithoMap();
        Generation::Data::get()->computeTextureArrays();
    }

    void StageGen<EGenerationStage::Lithomap>::setCellLithoType(int cell, qint64 rockId)
    {
        auto&& cellUpdate = QSharedPointer<Voronoi::CellUpdate>::create(Data::get()->getTerrainCells()->getCellAt(cell).getVoronoiCenter());

        emit Editable::aboutToBeModified(cellUpdate);
        cellToLithoType[cell] = rockId;
        updateCellColor(cell);
        emit Editable::modified(cellUpdate);
    }

    void StageGen<EGenerationStage::Lithomap>::visualizeLithomap()
    {
        // Setup colors
        auto&& lithoAssetsIds = Omnigen::get()->getAssetsSection()->getAssetsIds<EAsset::RockMaterial>();
        Q_ASSERT(!lithoAssetsIds.empty());

        colorByLithoType.clear();
        for (auto&& typeId : lithoAssetsIds)
            colorByLithoType.emplace(typeId, QVector4D(randomChance(), randomChance(), randomChance(), 0.2f));

        auto&& dem = Generation::Data::get()->getDEM();
        auto&& diagram = Data::get()->getTerrainCells();
        for (int cellId = 0; cellId < cellToLithoType.size(); ++cellId)
        {
            auto&& cell = diagram->getCellAt(cellId);

            GeometryData<CellVertex> geometry;
			auto& vertices = geometry.vertices;
			auto& triangles = geometry.indices;

            auto&& pts = cell->getPts();
            vertices.resize(pts.size());
            for (int v = 0; v < vertices.size(); ++v)
            {
                auto& p = vertices[v];
                auto&& src = pts[v];
                p.position = { src.x, dem->heightData.sample(src) + 200, src.z };
                p.normal = dem->heightData.sampleNormal(src);
                p.cellId = cellId;
            }

			triangles = std::get<1>(constrainedTriangulation2D(std::vector<GVector2D>(vertices.begin(), vertices.end())));

			// Fix winding order
			for (int i = 0; i < triangles.size(); i += 3)
			{
				IndexType& i0 = triangles[i];
				IndexType i1 = triangles[i + 1];
				IndexType& i2 = triangles[i + 2];

				if (GVector2D::crossProduct(vertices[i0].position, vertices[i1].position, vertices[i2].position) > 0.f)
					std::swap(i0, i2);
			}

            auto section = spawnBatched(std::move(geometry), LithomapBatchParams());
            section->cellIdx = cellId;
        }

        updateCellColors();

        // Update triangles map
        auto&& painter = gLithomapMarker->painter;

        auto&& [batches, batchesGuard] = gLithomapMarker->getBatches();
        auto&& batch = batches.begin()->second;

        painter.trianglesToCells.clear();
        painter.trianglesToCells.resize(batch.geometry->indices.size() / 3);

        for (auto&& [guid, section] : batch.sections)
        {
            IndexType trisBegin = section->getIndexBufferOffset() / 3;
            IndexType trisEnd = trisBegin + (section->getIndexBufferSize() / 3);
            for (IndexType ti = trisBegin; ti < trisEnd; ++ti)
                painter.trianglesToCells[ti] = section->cellIdx;
        }
    }

    void StageGen<EGenerationStage::Lithomap>::updateCellColor(int cell)
    {
        gLithomapMarker->painter.cellColors[cell] = colorByLithoType[cellToLithoType[cell]];
        gLithomapMarker->painter.bNeedsBufferUpdate = true;
    }

    void StageGen<EGenerationStage::Lithomap>::updateCellDataFromLithomap()
    {
        auto&& [lithomap, lithoClusters] = Data::get()->getLithomap();
        Q_ASSERT(!lithomap.empty());

		// Update cellToLithoType
		cellToLithoType.resize(lithomap.size());
		for (int i = 0; i < lithomap.size(); ++i)
			Generation::StageGen<EGenerationStage::Lithomap>::cellToLithoType[i] = lithoClusters[lithomap[i]]->getType();
    }

    void StageGen<EGenerationStage::Lithomap>::updateCellColors()
    {
        gLithomapMarker->painter.cellColors.resize(cellToLithoType.size());
		for (int i = 0; i < cellToLithoType.size(); ++i)
			updateCellColor(i);
    }

    // Randomized triangular grid for terrain block seeds scattering
    std::vector<GVector2D> randomGrid(const BoundingBox& bb, float density, float snap, int freedom)
    {
        std::uniform_int_distribution<int> distribution(-freedom, freedom);

        float sqrt = std::numbers::sqrt3;
        float incr = density * sqrt / 2;

        int shift = 0;
        auto centers = std::vector<GVector2D>();
        for (float x = bb.nbl.x() + incr; x <= bb.nbl.x() + bb.sizes.x() - incr; x += incr) {
            for (float z = bb.nbl.z() + density * (1 + 0.5f * shift); z <= bb.nbl.z() + bb.sizes.z() - density; z += density) {
                float xx = distribution(Generation::gRandomEngine) * snap;
                float zz = distribution(Generation::gRandomEngine) * snap;
                centers << GVector2D(x + xx, z + zz);
            }
            shift = 1 - shift;
        }
        return centers;
    }

    template<typename LineMarkerType>
    float scaleChanceForMarker(const GVector2D & p, float maxD)
    {
        auto&& qtree = Data::get()->getMarkerQuadTree<LineMarkerType>();
        auto* node = qtree.find_nearest(p.x, p.z, maxD);
        if (!node)
            return 0.0f;

        static Interpolation::Technique01<EInterpolation01::Smoothstep> interp;
        return 1.0f - interp(std::clamp(distance(GVector2D(node->x, node->y), p) / maxD, 0.0f, 1.0f));
    };

    void StageGen<EGenerationStage::Lithomap>::generateTerrainCells()
    {
        auto&& dem = Data::get()->getDEM();

        static const float blockDensity = 1000.0f;
        static const float alignment = 300.0f;

        OmniProfile("Terrain cells");

        std::vector<GVector2D> centers;

        // Block seeds scattering
        auto grid = randomGrid(dem->heightData.getBoundingBox(), blockDensity / 2, alignment, blockDensity / alignment / 8);
        centers.reserve(grid.size());

        {
            OmniProfile("Voronoi seeds");
            std::mutex centersGuard;
            tbb::parallel_for(0, int(grid.size()), [&](int i)
                {
                    auto&& point = grid[i];

                    float r = std::clamp(randomChance() - scaleChanceForMarker<DRidgeMarker>(point, 1000), 0.0f, 1.0f);

                    if (r * r * 1.5f < std::max(dem->heightData.sampleGradient(point).length(), 0.05f))
                    {
                        // Critical section
                        std::scoped_lock lock(centersGuard);
                        centers << point;
                    }
                });
            centers.shrink_to_fit();
        }

        // Cells generation
        auto diagram = QSharedPointer<Voronoi::BoxDiagram>::create(centers, dem->heightData.getBoundingBox());
        Data::get()->setTerrainCells(diagram);
        emit Editable::created(QSharedPointer<Voronoi::CellUpdate>::create(centers));

        float maxR = 0;
        for (auto&& cell : diagram->getCells())
        {
            auto rad = cell.getPolygon().getRadius();
            if (rad > maxR)
                maxR = rad;
        }
        Data::get()->setLargestVoronoiCellRadius(maxR);
    }

    void StageGen<EGenerationStage::Lithomap>::reshapeTerrainCellsDiagram()
    {
        auto&& dem = Data::get()->getDEM();
        auto&& newBoundingBox = dem->heightData.getBoundingBox();

        auto&& oldDiagram = Data::get()->getTerrainCells();
        auto&& oldBoundingBox = oldDiagram->getPerimeterBB();
        auto&& oldCenters = Data::get()->getTerrainCells()->getCenters();

        if (newBoundingBox.nbl == oldBoundingBox.nbl)
            return;

        static const float blockDensity = 1000.0f;
        static const float alignment = 300.0f;

        std::vector<GVector2D> newCenters;
        std::vector<GVector2D> modifiedCenters;
        std::vector<GVector2D> removedCenters;
        std::vector<GVector2D> addedCenters;

        std::mutex centersGuard;

        newCenters.reserve(oldCenters.size());
        for (auto&& center : oldCenters)
        {
            if (newBoundingBox.contains(center))
                newCenters << center;
            else
                removedCenters << center;
        }

        // Block seeds scattering
        auto grid = randomGrid(newBoundingBox, blockDensity / 2, alignment, blockDensity / alignment / 8);
        tbb::parallel_for(0, int(grid.size()), [&](int i)
            {
                auto&& point = grid[i];

                if (oldBoundingBox.contains(point))
                    return;

                float r = std::clamp(randomChance() - scaleChanceForMarker<DRidgeMarker>(point, 1000), 0.0f, 1.0f);

                if (r * r * 1.5f < std::max(dem->heightData.sampleGradient(point).length(), 0.05f))
                {
                    // Critical section
                    std::scoped_lock lock(centersGuard);
                    newCenters << point;
                    addedCenters << point;
                }
            });
        newCenters.shrink_to_fit();

        // Cells generation
        auto newDiagram = QSharedPointer<Voronoi::BoxDiagram>::create(newCenters, newBoundingBox);

        for (auto&& cell : newDiagram->getCells())
        {
            auto&& center = cell.getVoronoiCenter();
            auto&& cellPolygon = cell.getPolygon();

            if (oldBoundingBox.dist(center) < GRID_SEGMENT_WIDTH || newBoundingBox.dist(center) < GRID_SEGMENT_WIDTH)
                if (auto idx = oldDiagram->getCellIndexFromCenter(center); idx >= 0)
                {
                    auto&& oldPolygon = oldDiagram->getCellAt(idx).getPolygon();

                    if (cellPolygon.getPts().size() != oldPolygon.getPts().size() || container_and(cellPolygon.getPts(), oldPolygon.getPts()).size() != cellPolygon.getPts().size())
                        modifiedCenters << center;
                }
        }

        for (auto&& center : removedCenters)
            oldDiagram->getCellFromCenter(center)->debugPlot(QVector4D(1, 0, 0, 1), 20300);

        for (auto&& center : addedCenters)
            newDiagram->getCellFromCenter(center)->debugPlot(QVector4D(0, 1, 0, 1), 20400);

        for (auto&& center : modifiedCenters)
            newDiagram->getCellFromCenter(center)->debugPlot(QVector4D(0, 0, 1, 1), 20500);

        emit Editable::aboutToBeDeleted(QSharedPointer<Voronoi::CellUpdate>::create(removedCenters));
        emit Editable::aboutToBeModified(QSharedPointer<Voronoi::CellUpdate>::create(modifiedCenters));

        Data::get()->setTerrainCells(newDiagram);

        emit Editable::created(QSharedPointer<Voronoi::CellUpdate>::create(addedCenters));
        emit Editable::modified(QSharedPointer<Voronoi::CellUpdate>::create(modifiedCenters));

        float maxR = 0;
        for (auto&& cell : newDiagram->getCells())
        {
            auto rad = cell.getPolygon().getRadius();
            if (rad > maxR)
                maxR = rad;
        }
        Data::get()->setLargestVoronoiCellRadius(maxR);
    }

    int Utils::findCell(const GVector2D& p)
    {
        auto&& blockTree = Data::get()->getBlockQuadTree();
        auto&& cells = Data::get()->getTerrainCells()->getCells();

        float r = 200.f;
        float maxR = Data::get()->getLargestVoronoiCellRadius();
        while (true)
        {
            auto nodes = blockTree->find_all_nearest(p.x, p.z, r);
            for (auto* node : nodes)
                if (cells[node->data]->contains(p))
                    return node->data;

            if (r > maxR)
                break;

            r *= 2.0f;
        }

        Q_ASSERT(Data::get()->getDomainAtSquare(p.toGPoint(), EDomainType::Terrain).isNull());
        return -1;
    }

    struct VertexNode
    {
        int vertexIndex = 0;
        bool isTraversed = false;
        std::unordered_map<int, bool> neighbors; // index -> isTraversed

        VertexNode(int index): vertexIndex(index)
        {
            neighbors.reserve(2);
        }

        int getNext() const
        {
            for (const auto& [index, isTraversed]: neighbors)
                if (!isTraversed)
                    return index;
            return -1;
        }

        bool isCommonPoint() const
        {
            return neighbors.size() > 2;
        }
    };

    static void traverseGraph(std::vector<VertexNode>& vertexGraph, int startIndex, std::vector<std::vector<int>>& cycles, std::vector<int>& currentPath, bool isMainPass = true)
    {
        int currIndex = startIndex;

        std::vector<int> path;
        path.reserve(vertexGraph.size() / 2);

        while (true)
        {
            auto& currNode = vertexGraph[currIndex];

            if (currNode.getNext() == -1)
            {
                currentPath << std::move(path);
                cycles <<= std::move(currentPath);
                currentPath.clear();
                return;
            }

            if (currNode.isCommonPoint() && (currIndex != startIndex || isMainPass))
            {
                currentPath << std::move(path);
                traverseGraph(vertexGraph, currIndex, cycles, currentPath, false);
                traverseGraph(vertexGraph, currIndex, cycles, currentPath, false);
                return;
            }

            path << currIndex;
            const int nextIndex = currNode.getNext();
            currNode.neighbors[nextIndex] = true;
            vertexGraph[nextIndex].neighbors[currIndex] = true;
            if (currNode.getNext() == -1)
                currNode.isTraversed = true;
            currIndex = nextIndex;

            if (currIndex == startIndex)
            {
                cycles <<= std::move(path);
                return;
            }
        }
    }

    static Polygon2D convertIndicesToPolygon(const std::vector<GVector2D>& vertices, const std::vector<int>& indices)
    {
        std::vector<GVector2D> pts;
        pts.reserve(indices.size());
        for (int index: indices)
            pts << vertices[index];
        return Polygon2D(std::move(pts));
    }

    static std::vector<int> merge2Cycles(const std::vector<GVector2D>& vertices, const std::vector<int>& cycle1, const std::vector<int>& cycle2)
    {
        if (cycle1.empty())
            return cycle2;
        if (cycle2.empty())
            return cycle1;

        const Polygon2D polygon1 = convertIndicesToPolygon(vertices, cycle1);
        const Polygon2D polygon2 = convertIndicesToPolygon(vertices, cycle2);
        const GVector2D testPoint1 = polygon1[cycle1.size() / 2];
        const GVector2D testPoint2 = polygon2[cycle2.size() / 2];

        std::vector<int> result;
        result.reserve(cycle1.size() + cycle2.size() - 1);

        const bool isOnePolygonInsideOther = polygon1.contains(testPoint2) || polygon2.contains(testPoint1);
        const bool isSimilarDirection = (polygon1.isCW() == polygon2.isCW());

        std::vector<int> tempVec;
        if ((isOnePolygonInsideOther && isSimilarDirection) || (!isOnePolygonInsideOther && !isSimilarDirection))
        {
            tempVec = cycle2;
            std::reverse(++tempVec.begin(), tempVec.end());
        }
        else
        {
            std::copy(cycle2.begin(), cycle2.end(), std::back_inserter(tempVec));
        }

        std::copy(cycle1.begin(),  cycle1.end(),  std::back_inserter(result));
        std::copy(tempVec.begin(), tempVec.end(), std::back_inserter(result));

        return result;
    }

    static std::vector<std::vector<int>> mergeCycles(const std::vector<GVector2D>& vertices, const std::vector<VertexNode>& vertexGraph, const std::vector<std::vector<int>>& cycles)
    {
        std::vector<std::vector<int>> result = cycles;

        auto commonNodes = vertexGraph | std::views::filter([](const VertexNode& node) -> bool { return node.neighbors.size() > 2; });

        for (auto nodeIter = commonNodes.begin(); nodeIter != commonNodes.end(); ++nodeIter)
        {
            const int commonIndex = nodeIter->vertexIndex;
            std::vector<int> foundCycles;
            foundCycles.reserve(2);
            for (int i = 0; i < result.size(); ++i)
            {
                auto& cycle = result[i];
                const auto indexIter = std::find(cycle.begin(), cycle.end(), commonIndex);
                if (indexIter == cycle.end())
                    continue;
                foundCycles << i;
                if (*indexIter != *cycle.begin()) // put common index in start of cycle, to prepare for merge
                {
                    std::vector<int> newCycle;
                    std::copy(indexIter, cycle.end(), std::back_inserter(newCycle));
                    std::copy(cycle.begin(), indexIter, std::back_inserter(newCycle));
                    cycle = std::move(newCycle);
                }
            }
            if (foundCycles.size() < 2)
                continue;
            auto mergedCycle = merge2Cycles(vertices, result[foundCycles[0]], result[foundCycles[1]]);
            std::sort(foundCycles.begin(), foundCycles.end(), std::greater<int>()); // to prevent offset of indices in vector after erasing
            result.erase(result.begin() + foundCycles[0]);
            result.erase(result.begin() + foundCycles[1]);
            result << mergedCycle;
        }

        return result;
    }

    std::vector<Polygon2D> Utils::makeBoundingPolygon(const std::unordered_set<int>& cells)
    {
        if (cells.empty())
        {
            Q_ASSERT(false);
            return std::vector<Polygon2D>();
        }

        std::vector<GVector2D> allVertices;
        std::unordered_map<GVector2D, int> verticesMap;
        std::unordered_set<triangulation::Edge> edgesSet;
        std::vector<VertexNode> vertexGraph;

        allVertices.reserve(cells.size() * 10);
        verticesMap.reserve(cells.size() * 10);
        edgesSet.reserve(cells.size() * 10);
        vertexGraph.reserve(cells.size() * 10);

        auto&& diagram = Data::get()->getTerrainCells();

        const auto registerPoint = [&allVertices, &verticesMap, &vertexGraph](const GVector2D& pt) -> int
        {
            int resIndex = 0;
            const auto iter = verticesMap.find(pt);
            if (iter == verticesMap.end())
            {
                resIndex = allVertices.size();
                allVertices << pt;
                verticesMap[pt] = resIndex;
                vertexGraph.emplace_back(resIndex);
            }
            else
                resIndex = iter->second;

            return resIndex;
        };

        // Fill vertices and edges information of all cells
        for (int cellIdx : cells)
        {
            const auto& pts = diagram->getCellAt(cellIdx).getPolygon().getPts();
            for (int i = 0; i < pts.size(); ++i)
            {
                const GVector2D& curr = pts[i];
                const GVector2D& next = pts[i == pts.size() - 1 ? 0 : i + 1];
                int currIndex = registerPoint(curr);
                int nextIndex = registerPoint(next);

                triangulation::Edge edge{ currIndex, nextIndex };
                const auto iter = edgesSet.find(edge);
                // remove inner edges
                if (iter != edgesSet.end())
                    edgesSet.erase(iter);
                else
                    edgesSet.insert(edge);
            }
        }

        // Create graph from border edges
        for (const triangulation::Edge& edge: edgesSet)
        {
            // add connections between vertices
            vertexGraph[edge.start].neighbors[edge.end] = false;
            vertexGraph[edge.end].neighbors[edge.start] = false;
        }

        // Find common points in graph - cycles with such points should be merged
        auto commonNodes = vertexGraph | std::views::filter([](const VertexNode& node) -> bool { return node.neighbors.size() > 2; });

        std::vector<Polygon2D> polygons;

        std::vector<std::vector<int>> cycles;

        for (int i = 0; i < vertexGraph.size(); ++i)
        {
            const VertexNode& node = vertexGraph[i];
            if (node.isTraversed || node.neighbors.empty())
                continue;

            std::vector<int> currentPath;
            traverseGraph(vertexGraph, i, cycles, currentPath);
        }

        // Merge cycles with common points
        cycles = mergeCycles(allVertices, vertexGraph, cycles);

        for (const auto& cycle: cycles)
        {
            polygons <<= convertIndicesToPolygon(allVertices, cycle);
        }

        std::sort(polygons.begin(), polygons.end(), [](const Polygon2D& a, const Polygon2D& b){ return a.getArea() > b.getArea(); });

        return polygons;
    }

    std::vector<std::unordered_set<int>> Utils::clusterCells(std::unordered_set<int>&& selectedCells, const std::optional<int>& maxSize /*= std::nullopt*/)
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();

        std::vector<std::unordered_set<int>> cellClusters;

        while (true)
        {
            if (selectedCells.empty())
                break;

            std::unordered_set<int> cellCluster{ *selectedCells.begin() };
            std::unordered_set<int> cellsToConsider{ *selectedCells.begin() };
            selectedCells -= *selectedCells.begin();

            while (true)
            {
                if (cellsToConsider.empty())
                    break;

                if (maxSize && cellCluster.size() >= maxSize.value())
                    break;

                auto nextCell = *cellsToConsider.begin();

                cellCluster.insert(nextCell);
                cellsToConsider.erase(nextCell);
                selectedCells.erase(nextCell);

                for (auto it = cells[nextCell].getNeighbors().begin(); it != cells[nextCell].getNeighbors().end(); it++)
                    if (!cellCluster.contains(it.key()) && selectedCells.contains(it.key()))
                        cellsToConsider.insert(it.key());
            }

            cellClusters.push_back(cellCluster);
        }

        return cellClusters;
    }

    void StageGen<EGenerationStage::Lithomap>::initLithoMap()
    {
        OmniProfile("Lithomap");

        auto&& assetMgr = Omnigen::get()->getAssetsSection();
        auto&& lithoAssetsIds = assetMgr->getAssetsIds<EAsset::RockMaterial>();
        assetMgr->forceLoadAssets(EAsset::RockMaterial, lithoAssetsIds);
        auto lithoAssets = assetMgr->getAssets<EAsset::RockMaterial>();

        // Select softest rock type as bedrock (for full preliminary coverage)
        std::vector<qint64> softestRocksIds;
        std::multimap<float, qint64> rockHardnessMap;
        for (auto&& [id, rockAsset] : lithoAssets)
            rockHardnessMap.emplace(rockAsset->getHardness(), id);

        Q_ASSERT(!rockHardnessMap.empty());

        auto&& softestRocks = rockHardnessMap.equal_range(rockHardnessMap.begin()->first);
        for (auto rockTypeIterator = softestRocks.first; rockTypeIterator != softestRocks.second; ++rockTypeIterator)
            softestRocksIds.emplace_back(rockTypeIterator->second);

        qint64 bedrockId = randomPick<qint64, std::mt19937>(softestRocksIds, Generation::gRandomEngine);

        cellToLithoType = std::vector(Data::get()->getTerrainCells()->getCells().size(), bedrockId);
    }

    void StageGen<EGenerationStage::Lithomap>::generateLithoMap()
    {
        OmniProfile("Lithomap");

        const int maxSizeFactor = 5;

        auto&& diagram = Data::get()->getTerrainCells();
        auto&& assetMgr = Omnigen::get()->getAssetsSection();
        auto&& lithoAssetsIds = assetMgr->getAssetsIds<EAsset::RockMaterial>();
        assetMgr->forceLoadAssets(EAsset::RockMaterial, lithoAssetsIds);
        auto lithoAssets = assetMgr->getAssets<EAsset::RockMaterial>();

        auto&& cells = diagram->getCells();

        // Select softest rock type as bedrock (for full preliminary coverage)
        std::vector<qint64> softestRocksIds;
        std::multimap<float, qint64> rockHardnessMap;
        for (auto&& [id, rockAsset] : lithoAssets)
            rockHardnessMap.emplace(rockAsset->getHardness(), id);

        Q_ASSERT(!rockHardnessMap.empty());

        auto&& softestRocks = rockHardnessMap.equal_range(rockHardnessMap.begin()->first);
        for (auto rockTypeIterator = softestRocks.first; rockTypeIterator != softestRocks.second; ++rockTypeIterator)
            softestRocksIds.emplace_back(rockTypeIterator->second);

        qint64 bedrockId = randomPick<qint64, std::mt19937>(softestRocksIds, Generation::gRandomEngine);

        cellToLithoType = std::vector(diagram->getCells().size(), bedrockId);

        for (auto&& [hardness, typeId] : rockHardnessMap)
        {
            if (typeId == bedrockId)
                continue;

            auto&& minSize = lithoAssets[typeId]->getMinSize();
            int seedCount = std::max(1, int(std::round((cellToLithoType.size() / minSize) / (maxSizeFactor * 4))));
            std::unordered_set<int> seedCellIndices;

            // Purely random seeds, may overlap with same type
            std::uniform_int_distribution<int> dist(0, cellToLithoType.size() - 1);
            while(seedCellIndices.size() < seedCount)
                seedCellIndices.emplace(dist(Generation::gRandomEngine));

            for (auto&& seedIdx : seedCellIndices)
            {
                std::unordered_set<int> allPatchCellsIndices({ seedIdx });
                std::unordered_set<int> edgeCellsIndices = allPatchCellsIndices;

                while(true)
                {
                    if (auto newPoints = expandLithoPatch(edgeCellsIndices, allPatchCellsIndices, cells); !newPoints.empty())
                    {
                        edgeCellsIndices = newPoints;
                        allPatchCellsIndices.insert(std::make_move_iterator(newPoints.begin()), std::make_move_iterator(newPoints.end()));
                    }
                    else
                        break;

                    if (allPatchCellsIndices.size() > minSize)
                    {
                        double chance = double(allPatchCellsIndices.size() - minSize) / double(maxSizeFactor * minSize);
                        if(hybrid_int_distribution<int>(0, 1, std::min(chance, 1.0), 0.0)(Generation::gRandomEngine))
                            break;
                    }
                }

                for (auto&& cellId : allPatchCellsIndices)
                    cellToLithoType[cellId] = typeId;
            }
        }

        visualizeLithomap();
    }

    void StageGen<EGenerationStage::Lithomap>::finalizeLithoMap()
    {
        auto&& diagram = Data::get()->getTerrainCells();

        // Final clusters
        std::unordered_set<int> assignedIndices;
        std::vector<int> cellToCluster(cellToLithoType.size());
        std::vector<QSharedPointer<LithoCluster>> lithoClusters;

        auto&& [oldLithoMap, oldLithoClusters] = Data::get()->getLithomap();

        for (auto&& oldLithoCluster : oldLithoClusters)
        {
            std::unordered_set<int> unchangedClusterCells;

            for (auto&& cell : oldLithoCluster->getCells())
                if (cellToLithoType[cell] == oldLithoCluster->getType())
                    unchangedClusterCells += cell;

            assignedIndices += unchangedClusterCells;
            for (auto&& cluster : Utils::clusterCells(std::move(unchangedClusterCells)))
            {
                for (int cell : cluster)
                    cellToCluster[cell] = lithoClusters.size();

                lithoClusters <<= QSharedPointer<LithoCluster>::create(oldLithoCluster->getType(), cluster, oldLithoCluster->getThresholdData());
            }
        }
        
        for (int i = 0; i < cellToLithoType.size(); ++i)
        {
            // Basically size-capped meta clusters
            static const auto typeGeter = [](const qint64& type) { return type; };
            auto metaCluster = Utils::createMetaCluster(cellToLithoType, typeGeter, *diagram, i, &assignedIndices, 20);
            if (!metaCluster.isEmpty())
            {
                for (int cell : metaCluster)
                    cellToCluster[cell] = lithoClusters.size();

                lithoClusters <<= QSharedPointer<LithoCluster>::create(cellToLithoType[i], std::unordered_set<int>(metaCluster.begin(), metaCluster.end()));
            }
        }

        Data::get()->setLithomap(cellToCluster, lithoClusters);
    }

    std::unordered_set<int> StageGen<EGenerationStage::Lithomap>::expandLithoPatch(const std::unordered_set<int>& edgeCellsIndices, const std::unordered_set<int>& allPatchCellsIndices, const std::vector<Voronoi::GVoronoiCell>& allCells)
    {
        std::unordered_set<int> newPoints; 
        for (auto&& cell : edgeCellsIndices)
        {
            auto&& pointNeighbors = allCells[cell].getPointNeighbors();
            auto&& neighbors = allCells[cell].getNeighbors();

            for (auto&& it = pointNeighbors.keyBegin(); it != pointNeighbors.keyEnd(); ++it)
                if (!allPatchCellsIndices.contains(*it))
                    newPoints.emplace(*it);

            for (auto&& it = neighbors.begin(); it != neighbors.end(); ++it)
                if (!allPatchCellsIndices.contains(it.key()))
                    newPoints.emplace(it.key());
        }

        return newPoints;
    }

    void StageGen<EGenerationStage::Lithomap>::growCluster(qint64 lithoTypeId, QSet<int>* cluster, int targetSize, const Voronoi::Diagram& diagram, QSet<int>* assignedIndices, std::vector<qint64>* data)
    {
        // Make hull
        QSet<int> hull;

        auto hullCheck = [&](int idx) { return !cluster->contains(idx); };
        for (int i : *cluster)
            diagram.expandCellularCluster(&hull, i, hullCheck);

        // Grow randomly
        while (cluster->size() < targetSize)
        {
            int id = *hull.begin();

            cluster->insert(id);
            (*assignedIndices) << id;
            (*data)[id] = lithoTypeId;

            hull.remove(id);
            diagram.expandCellularCluster(&hull, id, hullCheck);
        }
    }
}