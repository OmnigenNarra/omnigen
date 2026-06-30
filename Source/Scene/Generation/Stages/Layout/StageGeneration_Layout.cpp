#include "stdafx.h"
#include "StageGeneration_Layout.h"
#include "../../OmnigenGeneration.h"
#include "Omnigen.h"
#include "Editor/StageTools/Layout/LayoutSelection.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/StageTools/StageTools.h"

namespace Generation
{
    void StageGen<EGenerationStage::Layout>::clear()
    {
        Data::get()->clearDomains();
    }

    bool StageGen<EGenerationStage::Layout>::validate()
    {
        if (!validateContinuity())
            return false;

        if (!validateSquares())
            return false;

        if (!validateSeasides())
            return false;

        if (!validateNoHoles())
            return false;

        if (!validateLithology())
            return false;

        return true;
    }

    void StageGen<EGenerationStage::Layout>::finalize()
    {
        auto&& minHeightMap = computeDomainMinimumHeight();

        for (auto&& [handle, domain] : Data::get()->getAllDomains<EDomainType::Terrain>())
        {
            auto&& domainData = domain->getData<EDomainType::Terrain>();
            if (!domainData->landformInstanceParams)
                domainData->landformInstanceParams = PLandformTypes[domainData->landformVariation];

            // Slope Angle
            auto&& ridgelineAngleData = domainData->landformInstanceParams->ridgelineSlopeAngle.range;
            domainData->ridgeGenParams.slopeAngle = domainData->landformInstanceParams->IsohypseDropAngle.getRandomValue();

            // Ridgeline Angle
            float maxRidgelineAngle = domainData->ridgeGenParams.slopeAngle < ridgelineAngleData.second ? domainData->ridgeGenParams.slopeAngle : ridgelineAngleData.second;
            float minRidgelineAngle = ridgelineAngleData.first < maxRidgelineAngle ? ridgelineAngleData.first : maxRidgelineAngle;
            domainData->ridgeGenParams.ridgelineAngle = std::uniform_real_distribution<float>(minRidgelineAngle, maxRidgelineAngle)(Generation::gRandomEngine);

            domainData->minHeight = minHeightMap[domain->getGuid()];
        }
    }

    bool StageGen<EGenerationStage::Layout>::validateContinuity()
    {
        QSet<GPoint> worldSquares = Data::get()->getAllSquares<EDomainType::Terrain>() + Data::get()->getAllSquares<EDomainType::Water>();

        if (auto squareSets = Omnigen::get()->partitionSquares(worldSquares); squareSets.size() > 1)
        {
            int minSquares = std::numeric_limits<int>::max();
            QSharedPointer<DDomain> firstBadDomain;

            for (auto&& sqSet : squareSets)
            {
                if (sqSet.size() < minSquares)
                {
                    minSquares = sqSet.size();
                    firstBadDomain = Data::get()->getDomainAtSquare(*sqSet.begin(), EDomainType::Terrain);
                }
            }

            OmniLog(ELoggingLevel::Error) <<= "World is not continuous! Fill in the holes with Terrain or Water domains!";

            Design::LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Domain>({ firstBadDomain->getHandle() });

            return false;
        }

        return true;
    }

    bool StageGen<EGenerationStage::Layout>::validateSquares()
    {
        QSet<GPoint> badSquares;

        for (int x = 0; x < GRID_SEGMENT_COUNT; ++x)
        {
            for (int z = 0; z < GRID_SEGMENT_COUNT; ++z)
            {
                if (Data::get()->getDomainSquares()[x][z])
                {
                    bool ok = false;

                    // ok if has Terrain
                    GPoint sq = { x, z };
                    if (Data::get()->getDomainAtSquare(sq, EDomainType::Terrain))
                        ok = true;

                    // ok if has Water...
                    if (!ok)
                        if (auto domain = Data::get()->getDomainAtSquare(sq, EDomainType::Water))
                        {
                            // ...but only acceptable neighbors are other Water squares
                            ok = true;
                            for (int ix = x - 1; ix <= x + 1; ++ix)
                                for (int iz = z - 1; iz <= z + 1; ++iz)
                                    if (GPoint isq = { ix, iz }; sq != isq)
                                    {
                                        auto&& domainsAtNeighbor = Data::get()->getDomainsAtSquare(isq);
                                        ok &= (domainsAtNeighbor.empty() || domainsAtNeighbor.contains(EDomainType::Water));
                                    }
                        }

                    if (!ok)
                        badSquares.insert({ x, z });
                }
            }
        }

        if (!badSquares.isEmpty())
        {
            OmniLog(ELoggingLevel::Error) << "Found " << badSquares.size() <<= " squares without Terrain domain! Generation aborted.";
            OmniLog(ELoggingLevel::Info) <<= "Please remove those squares or assign them to a terrain domain.";

            Design::LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Grid>(badSquares);

            return false;
        }

        return true;
    }

    bool StageGen<EGenerationStage::Layout>::validateSeasides()
    {
        QSet<GPoint> badSeasideSquares; // Seaside squares that have more than one empty square in the neighbourhood
        auto&& domainSquares = Data::get()->getDomainSquares();

        for (int x = 0; x < GRID_SEGMENT_COUNT; x++)
        {
            for (int z = 0; z < GRID_SEGMENT_COUNT; z++)
            {
                if (domainSquares[x][z] && Data::get()->getDomainAtSquare({ x, z }, EDomainType::Terrain) && Data::get()->getDomainAtSquare({ x, z }, EDomainType::Water))
                {
                    int nulls = 0;
                    if (x - 1 < 0 || !domainSquares[x - 1][z])
                        nulls++;
                    if (z - 1 < 0 || !domainSquares[x][z - 1])
                        nulls++;
                    if (x + 1 >= GRID_SEGMENT_COUNT || !domainSquares[x + 1][z])
                        nulls++;
                    if (z + 1 >= GRID_SEGMENT_COUNT || !domainSquares[x][z + 1])
                        nulls++;
                    if (nulls > 2)
                        badSeasideSquares.insert({ x, z });
                }
            }
        }

        if (!badSeasideSquares.isEmpty())
        {
            OmniLog(ELoggingLevel::Error) << "Found " << badSeasideSquares.size() <<= " seaside squares with too many empty neighbours! Generation aborted.";
            OmniLog(ELoggingLevel::Info) <<= "Please add proper neighbourhood.";

            Design::LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Grid>(badSeasideSquares);

            return false;
        }
        return true;
    }

    bool StageGen<EGenerationStage::Layout>::validateNoHoles()
    {
        auto&& domainSquares = Data::get()->getDomainSquares();

        QSet<GPoint> badSquares;
        int nextHoleId = 1;

        QMap<int, int> holes;

        // [x, z] -> holeId lookup table
        auto holeMap = filled_array<GRID_SEGMENT_COUNT>(filled_array<GRID_SEGMENT_COUNT>(INT_MAX));

        // Can't have holes on the borders
        for (int i = 0; i < GRID_SEGMENT_COUNT; ++i)
        {
            if (!domainSquares[0][i])
                holeMap[0][i] = 0;
            if (!domainSquares[i][0])
                holeMap[i][0] = 0;
            if (!domainSquares[GRID_SEGMENT_COUNT - 1][i])
                holeMap[GRID_SEGMENT_COUNT - 1][i] = 0;
            if (!domainSquares[i][GRID_SEGMENT_COUNT - 1])
                holeMap[i][GRID_SEGMENT_COUNT - 1] = 0;
        }

        std::function<int(int)> getRootHole = [&](int id)
        {
            if (holes[id] == id)
                return id;

            int root = getRootHole(holes[id]);
            return root;
        };

        for (int x = 1; x < GRID_SEGMENT_COUNT; x++)
            for (int z = 1; z < GRID_SEGMENT_COUNT; z++)
            {
                // Skip filled squares
                if (domainSquares[x][z])
                    continue;

                if (domainSquares[x - 1][z] && domainSquares[x][z - 1])
                {
                    // Has valid square neighbors at x-1 and z-1, new hole candidate
                    holeMap[x][z] = nextHoleId;
                    holes[nextHoleId] = nextHoleId; // independent hole
                    ++nextHoleId;
                }
                else // No valid neighbors processed so far
                {
                    // Non-edge
                    if (holeMap[x][z] != 0)
                    {
                        // Propagate hole or empty area
                        holeMap[x][z] = getRootHole(std::min(holeMap[x - 1][z], holeMap[x][z - 1]));

                        // Connect holes
                        if (!domainSquares[x - 1][z] && !domainSquares[x][z - 1] 
                            && (holeMap[x - 1][z] != holeMap[x][z - 1]))
                        {
                            holes[getRootHole(std::max(holeMap[x - 1][z], holeMap[x][z - 1]))] = holeMap[x][z];
                        }
                    }
                    // Edge
                    else
                    {
                        // Invalidate hole candidates
                        if (!domainSquares[x - 1][z])
                            holes[holeMap[x - 1][z]] = 0;
                        if (!domainSquares[x][z - 1])
                            holes[holeMap[x][z - 1]] = 0;
                    }
                }
        }

        for (int x = 1; x < GRID_SEGMENT_COUNT; x++)
            for (int z = 1; z < GRID_SEGMENT_COUNT; z++)
                if (!domainSquares[x][z] && holes[holeMap[x][z]] != 0)
                    badSquares.insert({ x, z });


        if (!badSquares.isEmpty())
        {
            OmniLog(ELoggingLevel::Error) << "Found " << badSquares.size() <<= " null squares that form holes in domains";
            OmniLog(ELoggingLevel::Info) <<= "Please assign them to a proper domain.";

            Design::LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Grid>(badSquares);

            return false;
        }

        return true;
    }

    bool StageGen<EGenerationStage::Layout>::validateLithology()
    {
        auto&& lithoAssets = Omnigen::get()->getAssetsSection()->getAssetsIds<EAsset::RockMaterial>();

        if(lithoAssets.empty())
        {
            OmniLog(ELoggingLevel::Error) <<= "Create at least one Rock Material Asset!";
            QMessageBox(QMessageBox::Icon::Critical,
                QString::fromStdString("Error"),
                QString::fromStdString("Create at least one Rock Material Asset!"), QMessageBox::StandardButton::Ok).exec();

            return false;
        }
        

        return true;
    }

    std::unordered_map<qint64, float> StageGen<EGenerationStage::Layout>::computeDomainMinimumHeight()
    {
        auto&& heightBounds = Generation::Data::get()->getDomainHeightBounds();

        std::unordered_map<qint64, float> localMinPerDomain;

        for (auto&& [ignore, domain] : Generation::Data::get()->getAllDomains())
        {
            auto id = domain->getGuid();
            auto maxHeight = domain->getData<EDomainType::Terrain>()->maxHeight;
            auto estimatedLocalMin = maxHeight * 0.3f;

            if (heightBounds.contains(id))
            {
                int minHeightBound = std::numeric_limits<int>::max();
                for (auto&& [_, boundsPerType] : heightBounds.at(id))
                    for (auto&& [_, bounds] : boundsPerType)
                        minHeightBound = std::min(minHeightBound, bounds.begin()->first);
                localMinPerDomain[id] = maxHeight >= minHeightBound ? minHeightBound : estimatedLocalMin;
            }
            else
                localMinPerDomain[id] = estimatedLocalMin;
        }

        return localMinPerDomain;
    }

    namespace Utils
    {
        QSet<GPoint> generateRandomContinuousSubset(const QSet<GPoint>& source, int subsetSize, const GPoint& seed)
        {
            QSet<GPoint> result;
            QSet<GPoint> perimeter;

            auto updatePerimeter = [&](const GPoint& newSquare)
            {
                if (GPoint sq = { newSquare.x - 1, newSquare.z }; source.contains(sq) && !result.contains(sq))
                    perimeter += sq;

                if (GPoint sq = { newSquare.x + 1, newSquare.z }; source.contains(sq) && !result.contains(sq))
                    perimeter += sq;

                if (GPoint sq = { newSquare.x, newSquare.z - 1 }; source.contains(sq) && !result.contains(sq))
                    perimeter += sq;

                if (GPoint sq = { newSquare.x, newSquare.z + 1 }; source.contains(sq) && !result.contains(sq))
                    perimeter += sq;

                perimeter.remove(newSquare);
            };

            // Init
            result += seed;
            updatePerimeter(seed);

            while (!perimeter.isEmpty() && result.size() < subsetSize)
            {
                int targetIdx = std::uniform_int_distribution<int>(0, int(perimeter.size()) - 1)(Generation::gRandomEngine);
                auto targetIt = perimeter.begin();
                std::advance(targetIt, targetIdx);

                result += *targetIt;
                updatePerimeter(*targetIt);
            }

            return result;
        }

        std::vector<Segment2D> computeSharedPerimeter(QSharedPointer<DDomain> D1, QSharedPointer<DDomain> D2)
        {
            std::vector<Segment2D> result;

            for (auto&& e1 : D1->getPerimeter())
            {
                for (auto& e2 : D2->getPerimeter())
                {
                    // Edge unit vector
                    auto unitVector = GVector2D((fCmp(e2.first.x, e2.second.x) != 0) * GRID_SEGMENT_WIDTH, (fCmp(e2.first.z, e2.second.z) != 0) * GRID_SEGMENT_WIDTH);

                    // Check each segment
                    for (auto head = e2.first; (head.x <= e2.second.x) && (head.z <= e2.second.z); head = head + unitVector)
                    {
                        if (head == e2.second)
                            break;

                        auto shorter = Segment2D{ head, head + unitVector };
                        auto& longer = e1;

                        if ((shorter.first.x >= longer.first.x) && (shorter.first.z >= longer.first.z) && (shorter.second.x <= longer.second.x) && (shorter.second.z <= longer.second.z))
                            result << shorter;
                    }
                }
            }

            return result;
        }

        std::vector<QSet<GPoint>> findSeasideAreas(const QSet<GPoint>& ignoreSquares /*= {}*/)
        {
            QSet<GPoint> terrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
            QSet<GPoint> waterSquares = Generation::Data::get()->getAllSquares<EDomainType::Water>();

            QSet<GPoint> seasideSquares = container_and(terrainSquares, waterSquares) - ignoreSquares;
            return Omnigen::get()->partitionSquares(seasideSquares);
        }

        std::vector<QSet<GPoint>> findLandAreas()
        {
            QSet<GPoint> terrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
            QSet<GPoint> waterSquares = Generation::Data::get()->getAllSquares<EDomainType::Water>();

            QSet<GPoint> landSquares = terrainSquares - waterSquares;
            return Omnigen::get()->partitionSquares(landSquares);
        }
    }
}