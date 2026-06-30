#include "stdafx.h"
#include "StageGeneration_TerrainClassification.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/OmnigenGeneration.h"

#include "Scene/Generation/Stages/Landmasses/StageGeneration_Landmasses.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"
#include "Editor/StageTools/TerrainClassification/BlockTypeMarker.h"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <execution>

#define DEBUG_BORDER_POINTS (1 & !NDEBUG)

#if DEBUG_BORDER_POINTS
#include "Utils/Colors.h"
#endif

namespace Generation
{
    void StageGen<EGenerationStage::TerrainClassification>::initialize()
    {
        if (Data::get()->getBlockTypeMap().empty())
        {
            const auto& diagram = *Data::get()->getTerrainCells();
            std::vector<ETerrainBlock> blockTypeMap(diagram.getCells().size(), ETerrainBlock::Last);

            Data::get()->setBlockTypeMap(blockTypeMap);
        }
    }

    // Assigns terrain meta clusters and clusters to voronoi cells.
    // Each cluster (a small group of Voronoi cells) represents one terrain feature.
    // Multiple clusters may belong to a single Meta Cluster. 
    // Meta Clusters provide common params to all clusters contained by them.
    // 
    // First, each Voronoi cell is assigned a TerrainBlock type (Slope, Flatland, Beach, etc)
    // Then neighboring blocks form big groups of cells (super meta clusters)
    // Super meta clusters are divided into meta clusters
    // Meta clusters spawn clusters
    bool StageGen<EGenerationStage::TerrainClassification>::autoGen()
    {
        selectBlocks();
        return true;
    }

    void StageGen<EGenerationStage::TerrainClassification>::clear()
    {
        Data::get()->setBlockTypeMap({});
        clearAllBatches<BlockTypeBatchParams>();
    }

    bool StageGen<EGenerationStage::TerrainClassification>::validate()
    {
        std::atomic_bool valid = true;

        auto&& cells = Data::get()->getTerrainCells()->getCells();
        auto&& blockTypeMap = Data::get()->getBlockTypeMap();

        tbb::parallel_for(0, int(blockTypeMap.size()), [&](int i)
            {
                if (blockTypeMap[i] == ETerrainBlock::Last)
                {
                    spawn<DLineMarker>(cells[i].getVoronoiCenter(), 10000, Colors::red);
                    valid = false;
                }
            });

        if (!valid.load())
        {
            Data::get()->initializeQueuedMarkers();
            OmniLog(ELoggingLevel::Error) <<= "One or more cells do not have Type assigned!";
        }

        return valid.load();
    }

    void StageGen<EGenerationStage::TerrainClassification>::finalize()
    {
    }

    int StageGen<EGenerationStage::TerrainClassification>::compareNeighbhorsAlongRidge(int lookupDepth, const std::vector<Segment2D>& ridgeSegments, int index, int height, bool succesor)
    {
        if (ridgeSegments.empty()) return 0;
        int localMaxCounter = 0;
        static const float comparisonFactor = 0.98f;

        const auto& diagram = *Data::get()->getTerrainCells();
        const auto& dem = *Data::get()->getDEM();

        int next = findNeighborAlongRidge(ridgeSegments, index, succesor);
        if (height * comparisonFactor > dem.heightData.sample(diagram.getCellAt(next)->getCenter())) 
            localMaxCounter++;

        if (lookupDepth > 1) 
            return localMaxCounter + compareNeighbhorsAlongRidge(lookupDepth - 1, ridgeSegments, next, height, succesor);

        return localMaxCounter;
    }

    int StageGen<EGenerationStage::TerrainClassification>::findNeighborAlongRidge(const std::vector<Segment2D>& ridgeSegments, int cellIndex, bool succesor)
    {
        const auto& diagram = *Data::get()->getTerrainCells();
        const auto& dem = *Data::get()->getDEM();

        auto cellCenter = diagram.getCenters()[cellIndex];
        float minDist = std::numeric_limits<float>::max();
        uint32_t segmentId = 0;

        //Find ridge segment closest to cell center
        for (int segmentIdlookup = 0; auto & segment : ridgeSegments)
        {
            // distance to segment |ab|
            auto [t, pointDst] = distance(segment, cellCenter, false);

            if (pointDst < minDist)
            {
                minDist = pointDst;
                segmentId = segmentIdlookup;
            }
            segmentIdlookup++;
        }

        //Find neighboring cell oriented parallel to the ridge
        auto&& midCell = diagram.getCellAt(cellIndex);
        auto&& neighbors = midCell.getNeighbors();

        float successorAngle = std::numeric_limits<float>::min();
        float predAngle = std::numeric_limits<float>::max();

        int ridgeNeighborId = 0;

        using keyItType = QMap<int, std::array<int, 2>>::key_iterator;

        auto processNext = [&](float alignmentWithRidgeAngle, keyItType neighborIt)
        {
            if (successorAngle < alignmentWithRidgeAngle)
            {
                successorAngle = std::max(successorAngle, alignmentWithRidgeAngle);
                ridgeNeighborId = *neighborIt;
            }
        };

        auto processPrev = [&](float alignmentWithRidgeAngle, keyItType neighborIt)
        {
            if (predAngle > alignmentWithRidgeAngle)
            {
                predAngle = std::min(predAngle, alignmentWithRidgeAngle);
                ridgeNeighborId = *neighborIt;
            }
        };

        std::function<void(float, keyItType)> compareNeighbor;

        if (succesor) compareNeighbor = processNext;
        else compareNeighbor = processPrev;


        for (auto neighborIt = neighbors.keyBegin(); neighborIt != neighbors.keyEnd(); ++neighborIt)
        {
            auto ridgeDir = ridgeSegments[segmentId].second - ridgeSegments[segmentId].first;

            auto neighborToCurrentDir = diagram.getCenters()[*neighborIt] - cellCenter;
            auto alignmentWithRidgeAngle = GVector2D::dotProduct(ridgeDir, neighborToCurrentDir) / (ridgeDir.length() * neighborToCurrentDir.length());

            compareNeighbor(alignmentWithRidgeAngle, neighborIt);
        }

        //if (lookupDepth > 1) return findNeighborAlongRidge(lookupDepth -1, ridgeSegments, ridgeNeighborId, succesor);
        return ridgeNeighborId;
    }

    std::vector<Generation::BlockChanceData> StageGen<EGenerationStage::TerrainClassification>::computeBlockChanceData()
    {
        const auto& diagram = *Data::get()->getTerrainCells();
        const auto& dem = *Data::get()->getDEM();

        std::vector<BlockChanceData> results(diagram.getCells().size());

        tbb::parallel_for(0, int(results.size()), [&](int i)
            {
                auto&& cell = *diagram.getCellAt(i);
                auto cellCenter = cell.getCenter();

                BlockChanceData data;

                // Heights
                CellElevationData ced = dem.getCellElevationData(cell);
                data.centerH = ced.height;
                data.minH = ced.minH;
                data.maxH = ced.maxH;
                data.steepness = ced.steepness;

                // Domains
                GPoint sq = cellCenter.toGPoint();
                for (auto&& [type, domain] : Data::get()->getDomainsAtSquare(sq))
                {
                    switch (type)
                    {
                    case EDomainType::Terrain: data.terrainDomain = domain; break;
                    case EDomainType::Water: data.waterDomain = domain; break;
                    case EDomainType::Biome: data.biomeDomain = domain; break;
                    }
                }

                auto checkPeak = [&]()
                {
                    auto&& ihlevels = Generation::Data::get()->getIsohypseMarkersByLevel();
                    const float maxPeakDist = 600;

                    if (ihlevels.empty()) return 0;

                    for (auto&& ih : ihlevels.front())
                    {
                        for (auto& point : ih->getCircularPoints())
                        {
                            if (GVector2D(point.x(), point.z()).dist(cellCenter) < maxPeakDist)
                                return 1;
                        }
                    }

                    return 0;
                };

                auto checkShore = [&]() -> std::tuple<bool /*isWithinShore*/, bool /*isWaterSideOfShore*/>
                {
                    auto&& qtree = Data::get()->getMarkerQuadTree<DShorelineMarker>();
                    auto nodes = qtree.find_all_nearest(cellCenter.x, cellCenter.z, 3000);
                    bool isBeach = false;

                    float minD = std::numeric_limits<float>::max();
                    LineMarkerPoint closestPoint;

                    for (auto&& node : nodes)
                    {
                        GVector2D p(node->x, node->y);
                        float noise = getGlobalNoiseValue(p.x, p.z, ENoiseUsage::BeachWidth);

                        // TODO: Parametrize: Beach size?
                        float lookupDistance = 1000.f;
                        if (noise < 0.f)
                            lookupDistance += noise * 700.f;
                        else
                            lookupDistance += noise * 1500.f;

                        if (float d = distance(cellCenter, p); (d < minD) && (d < lookupDistance))
                        {
                            isBeach = true;
                            minD = d;
                            closestPoint = node->data;
                        }
                    }

                    if (!closestPoint)
                    {
                        // Find any closest shore point
                        auto* node = qtree.find_nearest(cellCenter.x, cellCenter.z, getMaxGridCoord());
                        if (node)
                            closestPoint = node->data;
                    }

                    int side = getLineSide(closestPoint, cellCenter);
                    return { isBeach, side < 0 };
                };

                data.isWithinPeakDist = checkPeak();

                std::tie(data.isWithinShoreDist, data.isWaterSideOfShore) = checkShore();

                results[i] = std::move(data);
            });

        const auto& ridges = Generation::Data::get()->getMarkers<DRidgeMarker>();
        const auto& blockTree = Data::get()->getBlockQuadTree();
        std::unordered_set<int> cellsUnderRidges;
        cellsUnderRidges.reserve(ridges.size() * 100);
        const float maxR = Data::get()->getLargestVoronoiCellRadius() * 0.25f;

        const auto iterateRidgeFunc = [&cellsUnderRidges, &blockTree, maxR](const QSharedPointer<DRidgeMarker>& ridgeMarker)
        {
            const auto& ridgePoints = ridgeMarker->getControlPoints();
            for (const auto& pt: ridgePoints)
            {
                const auto nodes = blockTree->find_all_nearest(pt.x(), pt.z(), maxR);
                for (auto* node : nodes)
                    cellsUnderRidges << (int)node->data;
            }
        };

        for (auto&& ridgeMarker: ridges)
        {
            ridgeMarker->forEachChild(iterateRidgeFunc, ridgeMarker);
        }

        for (int cellIdx: cellsUnderRidges)
            results[cellIdx].isUnderRidge = true;

        return results;
    }

    void StageGen<EGenerationStage::TerrainClassification>::selectBlocks()
    {
        OmniProfile("Block selection");

        const auto& dem = *Data::get()->getDEM();
        const auto& diagram = *Data::get()->getTerrainCells();
        auto&& centers = diagram.getCenters();
        auto&& cells = diagram.getCells();

        std::vector<int> indices(cells.size());
        for (int i = 0; i < cells.size(); ++i)
            indices[i] = i;

        std::shuffle(indices.begin(), indices.end(), gRandomEngine);

        auto blockChanceData = computeBlockChanceData();

        auto&& [lithomap, lithoClusters] = Data::get()->getLithomap();
        tbb::parallel_for(0, int(lithomap.size()), [&](int i)
            {
                blockChanceData[i].lithoType = lithoClusters[lithomap[i]]->getType();
            });

        auto blockTypeMap = Data::get()->getBlockTypeMap(); // editable copy!
        auto selectionPass = [&](bool firstPass)
        {
#if DEBUG_BLOCK_SELECTION
            for (int i : indices)
            {
#else
            tbb::parallel_for(0, int(indices.size()), [&](quint32 idxOfIdx)
                {
                    int i = indices[idxOfIdx];
#endif
                    // Skip already set blocks.
                    if (blockTypeMap[i] != ETerrainBlock::Last)
#if DEBUG_BLOCK_SELECTION
                        continue;
#else
                        return;
#endif

                    std::vector<float> chances(static_cast<int>(ETerrainBlock::Last));
                    float chancesSum = 0.0f;

                    // Compute chances - consider only must-haves
                    for (int c = 0; c < chances.size(); ++c)
                    {
                        chances[c] = ETerrainBlockConstexpr::UseIn<EAC::ChooseBlockType>(ETerrainBlock(c), blockChanceData[i]);
                        if (firstPass && (chances[c] < 1.0f))
                            chances[c] = 0.0f;
                        else
                            chancesSum += chances[c];
                    }

                    if (chancesSum == 0.0f)
                    {
                        Q_ASSERT(firstPass);
#if DEBUG_BLOCK_SELECTION
                        continue;
#else
                        return;
#endif
                    }

                    float targetValue = randomChance();

                    for (int c = 0; c < chances.size(); ++c)
                    {
                        // Normalize
                        chances[c] /= chancesSum;

                        targetValue -= chances[c];

                        if (targetValue < 0)
                        {
                            blockTypeMap[i] = ETerrainBlock(c);
                            break;
                        }
                    }

                    Q_ASSERT(blockTypeMap[i] != ETerrainBlock::Last);
                }
#if !DEBUG_BLOCK_SELECTION
            );
#endif
        };

        selectionPass(true);
        selectionPass(false);

        auto&& cellUpdate = QSharedPointer<Voronoi::CellUpdate>::create();

        auto&& previousBlockTypeMap = Data::get()->getBlockTypeMap();
        for (IndexType i = 0; i < blockTypeMap.size(); i++)
            if (previousBlockTypeMap.size() < i || previousBlockTypeMap[i] != blockTypeMap[i])
                cellUpdate->cellCenters << cells[i].getVoronoiCenter();

        emit Editable::aboutToBeModified(cellUpdate);
        Data::get()->setBlockTypeMap(blockTypeMap);
        emit Editable::modified(cellUpdate);
    }
}
