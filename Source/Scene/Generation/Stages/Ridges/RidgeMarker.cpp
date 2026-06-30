#include "stdafx.h"
#include "RidgeMarker.h"
#include "Omnigen.h"
#include "StageGeneration_Ridges.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/OmnigenProgressDialog.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "../Landmasses/ShorelineMarker.h"
#include "../Landmasses/LandmassBoundMarker.h"
#include "../Layout/StageGeneration_Layout.h"
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Editor/StageTools/StageTools.h" 

#include "Editor/StageTools/Ridges/RidgesSelection.h"
#include "Utils/CoreUtils.h"
#include "Utils/QuadTreeLite.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <functional>

const float RIDGE_TIER_HEIGHT_MULT = 0.6f;

#define DEBUG_AREA_PARTITIONING 0
#define DEBUG_MARGIN 0
#define DEBUG_RIDGELINE_CREATION 0

DRidgeMarker::DRidgeMarker(const std::vector<QVector3D>& inControlPoints, const QSharedPointer<DRidgeMarker>& inParent, const QVector4D& inColor)
    : DLineMarker(inControlPoints, inColor, false, 0.0f)
    , segmentWidth(distance(inControlPoints[0],inControlPoints[1]))
{
    parent = inParent;
    selectionColor = color - QVector4D(0.33f, 0.33f, 0.33f, 0.0f);
    makeName();
    auto&& domain = Generation::Data::get()->getDomainAtSquare(GVector2D(inControlPoints[0]).toGPoint(), EDomainType::Terrain);
    auto&& domainData = domain->getData<EDomainType::Terrain>();
    auto&& slopeFactorData = domainData->landformInstanceParams->slopeFactorRange;
    slopeVariation = { slopeFactorData.getRandomValue(), slopeFactorData.getRandomValue() };

    for (auto&& pt : inControlPoints)
        squares += ((GVector2D)pt).toGPoint();
}


void DRidgeMarker::draw()
{
    setHovered(Design::RidgeSelection::isRidgeHovered(sharedFromThis()));

    DLineMarker::draw();
}

void DRidgeMarker::joinRidgeAsSubridge(const QSharedPointer<DRidgeMarker>& ridge)
{
    auto&& controlPoints = getControlPoints();
    auto&& subPoints = ridge->getControlPoints();

    for (int i = 0; i < controlPoints.size(); ++i)
    {
        if (vEq(controlPoints[i], ridge->getControlPoints()[0]))
        {
            ridge->setSourcePointIdx(i);
            addChild(ridge);
            break;
        }
    }

    Q_ASSERT(ridge->sourcePointIdx != -1);
}

void DRidgeMarker::createRidgeMarkerTree(SubRidge* ridge, const QSharedPointer<DRidgeMarker>& parent)
{
    std::vector<QVector3D> inControlPoints;

    for (auto&& pt : ridge->squarePath)
    {
        inControlPoints.push_back(pt);
    }
    if (inControlPoints.size()>2)
    {
        auto marker = spawn<DRidgeMarker>(inControlPoints, parent);
        
        if (ridge->tablelandType)
            marker->setTablelandType(*ridge->tablelandType);

        if (parent) parent->joinRidgeAsSubridge(marker);

        for(auto&& subRidge : ridge->subRidges)
        {
            if(subRidge.squarePath.size() > 2) createRidgeMarkerTree(&subRidge, marker);
        }
    }
}

void DRidgeMarker::subtractSquareMargin(QSet<GPoint>* squares, const QSet<GPoint>& marginSource)
{
    QSet<GPoint> margin;

    for (auto&& sq : *squares)
        for (auto&& srcSq : marginSource)
            if (sq.isNeighbor(srcSq, true))
                margin += sq;

    *squares -= margin;
}

float DRidgeMarker::randomSign(bool allowZero)
{
    static const int signOptions[] = { -1, 1 };
    static std::discrete_distribution<int> signDist{ 1,1 };

    static const int signWithZeroOptions[] = { -1, 0, 1 };
    static std::discrete_distribution<int> signWithZeroDist{ 1,1,1 };

    return allowZero ? signWithZeroOptions[signWithZeroDist(Generation::gRandomEngine)] : signOptions[signDist(Generation::gRandomEngine)];
}

std::vector<RidgePeakData> DRidgeMarker::getPeakData()
{
    auto&& ridgePts = getControlPoints();
    std::vector<RidgePeakData> peakData;

    GPoint sq = GVector2D(ridgePts.front()).toGPoint();
    auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
    auto domainData = domain->getData<EDomainType::Terrain>();
    auto&& peakDistanceData = domainData->landformInstanceParams->peakDistance;
    int peaks = domainData->landformInstanceParams->mainPeakCount.getRandomValue();

    std::unordered_set<int> peakPoints;

    // Tablelands should only have a single peak due to their specific nature
    bool isTablelands = false;
    if (domainData->landform == ELandform::Tablelands)
    {
        peaks = 1;
        isTablelands = true;
    }

    int margin = std::min(int((ridgePts.size() / 5)), 15);
    std::uniform_int_distribution<> distr(0 + margin, ridgePts.size() - 1 - margin);

    // Main Ridge peaks - due to how peak height is assigned later on, all peaks will have fairly similar height (allowing for long main ridges)
    for (int i = 0; i < peaks; ++i)
    {
        int randomRoot = distr(Generation::gRandomEngine);
        if (peakPoints.contains(randomRoot))
            continue;

        peakData.push_back({ findTier(), getGuid(), getGuid(), ridgePts[randomRoot] });
        peakPoints.emplace(randomRoot);
    }

    // Skip peaks for subridges for tablelands
    if (isTablelands)
        return peakData;

    std::vector<QSharedPointer<DRidgeMarker>> children = getChildren();
    while (!children.empty())
    {
        std::vector<QSharedPointer<DRidgeMarker>> nextChildren;
        for (auto&& child : children)
        {
            auto&& childPts = child->getControlPoints();
            if (childPts.size() > 5)
            {
                // Subridge peaks
                // TODO: if longer subridges, or more frequent peaks are desired, add multiple peaks per subridge here (currently it is considered impossible)
                // Still would suggest changing how assignHeightToRidge() is calculated to achieve multiple peaks, as changing it here with drastically increase peak graph, and affect generation times
                int subridgeMargin = std::min(15, int(childPts.size() / 4));
                std::uniform_int_distribution<> distr(0 + subridgeMargin, childPts.size() - 1 - subridgeMargin);
                int randomRoot = distr(Generation::gRandomEngine);
                QVector3D potentialPeak = childPts[randomRoot];

                float minDistance = peakDistanceData.getRandomValue();
                bool distanceCheck = true;

                // Check minimal distance from other peaks
                for (auto&& peak : peakData)
                    if (peak.peakPoint.distanceToPoint(potentialPeak) < minDistance)
                    {
                        distanceCheck = false;
                        break;
                    }

                if(distanceCheck)
                    peakData.push_back({ child->findTier(), child->getGuid(), getGuid(), potentialPeak });
            }

            if(auto&& nextSubridges = child->getChildren(); !nextSubridges.empty())
                nextChildren.insert(nextChildren.end(), std::make_move_iterator(nextSubridges.begin()), std::make_move_iterator(nextSubridges.end()));
        }

        if (!nextChildren.empty())
        {
            children.clear();
            children = std::move(nextChildren);
        }
        else
            break;
    }

    return peakData;
}

void DRidgeMarker::makeName()
{
    static int nameCounter = 0;
    name = "Ridge " + QString::number(++nameCounter);
}

void DRidgeMarker::setParent(const QSharedPointer<DRidgeMarker>& newParent)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    parent = newParent;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::addChild(const QSharedPointer<DRidgeMarker>& childToAdd)
{
    auto where_to_add = std::upper_bound(children.begin(), children.end(), childToAdd, [](auto&& A, auto&& B) { return A->sourcePointIdx < B->sourcePointIdx; });
    emit Editable::aboutToBeModified(sharedFromThis());
    children.insert(where_to_add, childToAdd);
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setName(const QString& newName)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    name = newName;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setSourcePointIdx(int newSourcePointidx) 
{
    emit Editable::aboutToBeModified(sharedFromThis());
    sourcePointIdx = newSourcePointidx;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setSegmentWidth(float newSegmentWidth)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    segmentWidth = newSegmentWidth;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setHeights(const std::vector<float>& newHeights) 
{
    emit Editable::aboutToBeModified(sharedFromThis());
    ridgelineHeight = newHeights;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setTablelandType(ETableLand newTablelandType) 
{
    emit Editable::aboutToBeModified(sharedFromThis());
    tablelandType = newTablelandType;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setLeftSlopeFactor(float newLeftSlopeFactor)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    slopeVariation.first = newLeftSlopeFactor;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setRightSlopeFactor(float newRightSlopeFactor) 
{
    emit Editable::aboutToBeModified(sharedFromThis());
    slopeVariation.second = newRightSlopeFactor;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::moveRidgePoints(const std::vector<QVector3D>& newVerts, int vertsAdded /*= 0*/)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    movePoints(newVerts, vertsAdded);
    squares.clear();
    for (auto&& pt : getControlPoints())
        squares += ((GVector2D)pt).toGPoint();
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setSquares(const QSet<GPoint>& newSquares)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    squares = newSquares;
    emit Editable::modified(sharedFromThis());
}

void DRidgeMarker::setMarkerColor(const QVector4D& newColor)
{
    color = newColor;
}

int DRidgeMarker::findTier()
{
    int tier = 0;

    auto parentRidge = parent;
    while (parentRidge) 
    {
        tier++;
        parentRidge = parentRidge.lock()->parent;
    }

    return tier;
}

QSharedPointer<DRidgeMarker> DRidgeMarker::findRootParent()
{
    auto rootParent = sharedFromThis();

    while (rootParent->parent)
    {
        rootParent = rootParent->parent;
    }

    return rootParent;
}

ETableLand DRidgeMarker::getRandomTablelandType(ETableLand domainType)
{
    float factor = (1.0f / float(magic_enum::enum_count<ETableLand>() - 1));
    float chance = factor * (float(domainType));
    hybrid_int_distribution<int> tablelandsDist(0, magic_enum::enum_count<ETableLand>() - 1, 0.2f, chance);

    ETableLand tablelandType = static_cast<ETableLand>(tablelandsDist(Generation::gRandomEngine));
    return tablelandType;
}

void DRidgeMarker::showAs3D()
{
    auto cPts = getControlPoints();
    Q_ASSERT(cPts.size() == ridgelineHeight.size());
    tbb::parallel_for(0, int(cPts.size()), [&](int i)
        {
            cPts[i].setY(ridgelineHeight[i]);
        });

    movePoints(cPts);
}

void DRidgeMarker::showAs2D()
{
    auto cPts = getControlPoints();
    tbb::parallel_for(0, int(cPts.size()), [&](int i)
        {
            cPts[i].setY(60.0f);
        });

    movePoints(cPts);
}

bool DRidgeMarker::generateAll()
{
    OmniProfile("Ridges");

    auto existingRidges = Generation::Data::get()->getMarkers<DRidgeMarker>();
    auto landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();

    std::vector<std::pair<QSharedPointer<DLandmassMarker>, QMap<QSharedPointer<DDomain>, QSet<GPoint>>>> partitionedLandmasses;

    for (auto&& landmass : landmasses)
    {
        if (std::any_of(existingRidges.begin(), existingRidges.end(), [&](auto& r) { return landmass->isInside(*r->getControlPoints().begin()); }))
            continue;

        QMap<QSharedPointer<DDomain>, QSet<GPoint>> partitionedLandmass;
        for (auto&& sq : landmass->getSquares())
        {
            auto domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain);
            partitionedLandmass[domain] += sq;
        }

        partitionedLandmasses.push_back({landmass, partitionedLandmass});
    }

    if (partitionedLandmasses.empty())
        return true;

    std::vector<RidgeRoot> ridgeRoots;

    const int subdivision = 3;
    const float cellSize = GRID_SEGMENT_WIDTH / std::pow(2, subdivision);

    auto processRidge = [&](QSet<GPoint>* set, int areaSize, int maxLevel, tbb::spin_mutex* guard, const std::vector<std::pair<int, std::vector<GVector2D>>>& otherSegments)
    {
        RidgeGridData ridgeGrid;
        auto initialSet = set;

        // Pick a random seed square
        int sqIdx = std::uniform_int_distribution<int>(0, set->size() - 1)(Generation::gRandomEngine);
        auto sqIt = set->begin();
        std::advance(sqIt, sqIdx);

        // Expand it randomly to create a subset
        ridgeGrid.points = Generation::Utils::generateRandomContinuousSubset(*set, set->size(), *sqIt);
        Q_ASSERT(!ridgeGrid.points.isEmpty());
        ridgeGrid.perimeter = std::get<std::vector<Segment2D>>(computePerimeterForSquares(ridgeGrid.points));

        RidgeRoot newRoot{ maxLevel, nullptr };
        newRoot.area.reserve(ridgeGrid.points.size() * std::pow(2, 2 * subdivision));

        for (auto& point : ridgeGrid.points)
        {
            for (float x = 0.0f; x < GRID_SEGMENT_WIDTH; x += GRID_SEGMENT_WIDTH / std::pow(2, subdivision))
            {
                for (float z = 0.0f; z < GRID_SEGMENT_WIDTH; z += GRID_SEGMENT_WIDTH / std::pow(2, subdivision))
                {
                    GVector2D squarePt = { point.x * GRID_SEGMENT_WIDTH + x, point.z * GRID_SEGMENT_WIDTH + z };

                    if ((x == 0 || z == 0) && std::any_of(ridgeGrid.perimeter.begin(), ridgeGrid.perimeter.end(), [squarePt](auto& line) { return line.dist(squarePt) < 1.f; }))
                        continue;

                    QSharedPointer< Generation::SquigSquare> SquarePtr = QSharedPointer< Generation::SquigSquare>(new Generation::SquigSquare{ squarePt, cellSize, {} });
                    newRoot.area.push_back(SquarePtr);
#if DEBUG_AREA_PARTITIONING
                    float sideLength = SquarePtr->size;
                    std::vector<GVector2D> debugSqr = { SquarePtr->coords, GVector2D(SquarePtr->coords.x + sideLength,SquarePtr->coords.z),  GVector2D(SquarePtr->coords.x + sideLength,SquarePtr->coords.z + sideLength),   GVector2D(SquarePtr->coords.x,SquarePtr->coords.z + sideLength) };
                    Generation::Data::get()->createMarker<DLineMarker>(debugSqr, QVector4D(1, 1, 1, 1), true, 10);
#endif
                }
            }
        }
        std::uniform_int_distribution idDistribution(0, static_cast<int>(newRoot.area.size() - 1));

        GVector2D BBoxCenter = calcCenter(&newRoot.area);
        GVector2D currPos = newRoot.area[idDistribution(Generation::gRandomEngine)]->coords;
        GVector2D direction = { 1,0 };

        // Prevent dir = null
        std::uniform_real_distribution<float> offsetDist(-1.f, 1.f);
        while (true)
        {
            direction = (BBoxCenter + GVector2D(offsetDist(Generation::gRandomEngine), offsetDist(Generation::gRandomEngine)) - currPos).normalized();
            if (!direction.isNull())
                break;
        }

        newRoot.squarePath.push_back(currPos);
        newRoot.visitedSquares.insert(currPos.toGPoint());
        QSet<GPoint> claimedSquares = newRoot.visitedSquares;

        auto domain = Generation::Data::get()->getDomainAtSquare(currPos.toGPoint(), EDomainType::Terrain);
        auto domainData = domain->getData<EDomainType::Terrain>();
        auto [iterations, subridgesPerIterationsMain, subridgesPerIterationsSub, spreadFactor] = ridgeParamsToValues(domainData->ridgeGenParams);
        auto scalingData = domainData->landformInstanceParams->scaling;

        // Main Ridge size
        int mainRidgeSize = domainData->landformInstanceParams->mainRidgeSize.getRandomValue();
        float scalingFactor = ((float(areaSize) / scalingData.second) / ((float(areaSize) / scalingData.second) + 1.0)) * scalingData.first;
        mainRidgeSize *= std::max(1.0f, 0.5f + scalingFactor);

        //Sub Ridge Size
        int subRidgeSize = domainData->landformInstanceParams->subRidgeSize.getRandomValue();
        subRidgeSize *= std::max(1.0f, 0.5f + scalingFactor);

        if (domainData->landform == ELandform::Tablelands)
        {
            auto tablelandType = getRandomTablelandType(domainData->tableland);
            newRoot.tablelandType = tablelandType;
            auto landformVariation = domainData->landformVariation;
            auto tablelandsRidgeDensity = PTablelandTypes[landformVariation][tablelandType].ridgeDensityRatio;
            mainRidgeSize -= (mainRidgeSize * tablelandsRidgeDensity);
            mainRidgeSize = std::max(mainRidgeSize, 4);
        }

        float moveLength = domainData->landformInstanceParams->segmentLength;
        float oneIterationLength = moveLength * 3;
        float maxHeightDelta = domainData->maxHeight - domainData->minHeight;
        float ridgelineAngle = domainData->ridgeGenParams.ridgelineAngle;
        float maxContinuousLineLength = std::tanf(qDegreesToRadians(90.0f - ridgelineAngle)) * maxHeightDelta;
        int maxIterations = std::floor(maxContinuousLineLength / oneIterationLength);
        int finalIterations = std::min(maxIterations, int((iterations * maxLevel) * std::max(1.0f, scalingFactor)));

        if (newRoot.tablelandType == ETableLand::Pinnacle && mainRidgeSize < 10)
        {
            mainRidgeSize = 4;
            moveLength = 25;
            finalIterations = 4;
        }

        RidgeSegmentParams rootParams = {
            .subridgeId = -1,
            .subridgeParentId = -1,
            .parentBranchTier = -1,
            .branchTier = 0,
            .createSubridgePerIterations = subridgesPerIterationsMain,
            .createSubridgePerIterationsSub = subridgesPerIterationsSub,
            .createSubridgeOnIteration = subridgesPerIterationsMain,
            .iteration = 0,
            .mainRidgeSteps = mainRidgeSize,
            .maxIterations = finalIterations,
            .startingStep = 0,
            .steps = 0,
            .maxSteps = subRidgeSize,
            .maxSquares = areaSize,
            .pos = currPos,
            .parentDir = GVector2D::rotateDegrees(direction, DRidgeMarker::randomSign() * 90.0f),
            .baseDir = direction,
            .dir = direction,
            .spreadFactor = spreadFactor,
            .moveLength = moveLength,
            .otherSegmentDistFactor = 1.0f + (domainData->landformOpenness * 0.1f),
            .tablelandType = newRoot.tablelandType,
            .otherSegments = otherSegments
        };

        std::map<int, subRidgeParam_t> subRidgeParams;
        std::unordered_map<int, std::vector<Segment2D>> segments;
        ridgeMainPass(&rootParams, ridgeGrid, &newRoot, &subRidgeParams, &segments, &claimedSquares);

        std::vector<Segment2D> mainSegments;
        for (auto it = newRoot.squarePath.begin() + 1; it != newRoot.squarePath.end(); it++)
            mainSegments.emplace_back(*(it - 1), *it);

        segments.emplace(0, std::move(mainSegments));

#if DEBUG_RIDGELINE_CREATION
        for (auto&& segTier : segments)
            for (auto&& ele : segTier.second)
                spawn<DLineMarker>(std::vector<GVector2D>({ ele.first, ele.second }), QVector4D(0, 0, 1, 1), false, 700.0f);
#endif

        // Skip ridgelines that failed to achieve at least 50% of desired main ridge length, or minimal size
        if (newRoot.squarePath.size() < 3 || newRoot.squarePath.size() < rootParams.mainRidgeSteps * 0.5)
        {
            set = initialSet;
            return;
        }

        if (newRoot.maxTreeLevel > 0)
            processSubRidges(&rootParams, &subRidgeParams, 0, &segments, &claimedSquares);

#if DEBUG_RIDGELINE_CREATION
        for (auto&& seg : segments)
            for (auto&& ele : seg.second)
                spawn<DLineMarker>(std::vector<GVector2D>({ ele.first, ele.second }), QVector4D(0, 1, 0, 1), false, 600.0f);
#endif

        std::function<void(SubRidge&)> removeVisitedSquare;
        removeVisitedSquare = [&](SubRidge& ridge)
        {
            *set -= ridge.visitedSquares;

            for (auto&& subRidge : ridge.subRidges) removeVisitedSquare(subRidge);
        };

        removeVisitedSquare(newRoot);

        if (guard)
        {
            std::scoped_lock lock(*guard);
            ridgeRoots << newRoot;
        }
        else
        {
            ridgeRoots << newRoot;
        }
    };

    auto genProgress = QOmnigenProgressDialog("Generating ridges...", partitionedLandmasses.size(), [&](QOmnigenProgressDialogBase* progressDialog)
        {
            tbb::spin_mutex pushGuard;

            tbb::parallel_for(tbb::blocked_range<int>(0, partitionedLandmasses.size()), [&](tbb::blocked_range<int>& r)
                {
                    for (int i = r.begin(); i != r.end(); ++i)
                    {
                        if (progressDialog->wasCanceled)
                            return;

                        auto&& [landmass, partitionedLandmass] = partitionedLandmasses[i];

                        if (!partitionedLandmass.empty())
                        {
                            for (auto it = partitionedLandmass.keyValueBegin(); it != partitionedLandmass.keyValueEnd(); ++it)
                            {
                                auto&& [domain, set] = *it;
                                auto&& data = domain->getData<EDomainType::Terrain>();
                                auto&& landform = data->landform;
                                auto&& landformParams = *data->landformInstanceParams;
                                float openness = data->landformOpenness * 0.1f;

                                if (landformParams.ridgeMargin > 0)
                                    applyMarginToSet(&set, domain, landformParams.ridgeMargin);

                                Q_ASSERT_X(!set.isEmpty(), "landform restriction", "illegal hills / mountains domain shape");

                                auto domainData = domain->getData<EDomainType::Terrain>();
                                auto ridgeDensityPerSquare = landformParams.ridgeDensityPerSquare;
                                int branchLevel = landformParams.ridgeMaxTreeLevel.getRandomValue();

                                int squares = set.size();
                                int squaresToCover = int(ridgeDensityPerSquare.getRandomValue() * squares);
                                if (landform == ELandform::Tablelands)
                                {
                                    auto landformVariation = domain->getData<EDomainType::Terrain>()->landformVariation;
                                    squaresToCover *= PTablelandTypes[landformVariation][domain->getData<EDomainType::Terrain>()->tableland].ridgeDensityRatio;
                                }
                                else if (landform == ELandform::Mountains || landform == ELandform::Hills)
                                    squaresToCover = domain->getSquares().size() * std::min(1.0f, (1.2f - openness));

                                squaresToCover = std::max(1, squaresToCover);
                                domainData->desiredRidgeCoverage +=  float(squaresToCover) / float(squares);
                                if (landform == ELandform::Tablelands)
                                {
                                    auto landformVariation = domain->getData<EDomainType::Terrain>()->landformVariation;
                                    domainData->desiredRidgeCoverage = std::max(0.05f, domainData->desiredRidgeCoverage - PTablelandTypes[landformVariation][domain->getData<EDomainType::Terrain>()->tableland].ridgeDensityRatio);
                                }
                                Q_ASSERT(domainData->desiredRidgeCoverage <= 1.0f || domainData->desiredRidgeCoverage >= 0.0f);

                                std::vector<std::pair<int, std::vector<GVector2D>>> otherLandmassSegments;
                                int ridgelinesAdded = 0;
                                int failedTries = 0;

                                // Create subsets for ridges
                                while (true)
                                {
                                    int initialSetSize = set.size();
                                    if (squares - set.size() >= squaresToCover || set.size() <= 0)
                                        break;

                                    float ridgeSizeFactor = landformParams.ridgeAverageSize.getRandomValue();
                                    int areaSize = static_cast<int>(std::round(ridgeSizeFactor * squaresToCover));
                                    areaSize = std::max(areaSize, 1);

                                    processRidge(&set, areaSize, branchLevel, &pushGuard, otherLandmassSegments);

                                    // No difference in set size means the generation process failed, indicating too little space is left for valid generation
                                    if (initialSetSize == set.size())
                                    {
                                        if (squares - set.size() >= std::ceil(squaresToCover * 0.75) || failedTries > 2)
                                            break;
                                        else
                                            failedTries++;
                                    }

                                    if(ridgeRoots.size() - 1 == ridgelinesAdded && ridgeRoots.back().squarePath.size() > 2)
                                    {
                                        ridgelinesAdded++;
                                        int level = 0;
                                        otherLandmassSegments.emplace_back(level, ridgeRoots.back().squarePath);

                                        std::function<void(const std::vector<SubRidge>&, int)> getPointsFromSubridges = [&](const std::vector<SubRidge>& ridges, int lvl)
                                        {
                                            ++lvl;
                                            for (auto&& subRidge : ridges)
                                            {
                                                if (subRidge.squarePath.size() <= 2)
                                                    continue;

                                                otherLandmassSegments.emplace_back(lvl, subRidge.squarePath);
                                                if (!subRidge.subRidges.empty())
                                                    getPointsFromSubridges(subRidge.subRidges, lvl);
                                            }
                                        };

                                        getPointsFromSubridges(ridgeRoots.back().subRidges, level);
                                    }

                                }
                            }
                        }
                        else
                        {
                            // Possibly better solution could be found
                            auto landmassPolygon = PolygonUtils::inflatePolygon(landmass->getMainPolygon(), -10).front();

                            int idx1 = std::uniform_int_distribution<int>(0, landmassPolygon.size() - 1)(Generation::gRandomEngine);
                            int idx2 = asCircular(landmassPolygon).findIdx(idx1, landmassPolygon.size() * 0.5f);
                            auto path = PolygonUtils::findPathInsidePolygon(landmassPolygon[idx1], landmassPolygon[idx2], landmassPolygon, landmass->getCutPolygons());
                            if (path.size() == 2)
                                path.insert(path.begin() + 1, (path[0] + path[1]) * 0.5f);

                            auto&& domain = Generation::Data::get()->getDomainAtSquare(((GVector2D)landmassPolygon.front()).toGPoint(), EDomainType::Terrain);
                            auto&& landform = domain->getData<EDomainType::Terrain>()->landform;
                            auto&& landformParams = *domain->getData<EDomainType::Terrain>()->landformInstanceParams;
                            int branchLevel = landformParams.ridgeMaxTreeLevel.getRandomValue();

                            RidgeRoot newRoot{ branchLevel, nullptr };
                            newRoot.squarePath = std::vector<GVector2D>(path.begin(), path.end());

                            {
                                std::scoped_lock lock(pushGuard);
                                ridgeRoots << newRoot;
                            }
                        }
                    }

                    progressDialog->addProgress(r.size());
                });
        });

    if (!genProgress.executeProgressDialog())
        return false;

    for (auto&& root : ridgeRoots)
        createRidgeMarkerTree(&root);

    return true;
}

void DRidgeMarker::ridgeMainPass(RidgeSegmentParams* ridgeParams, const RidgeGridData& ridgeGrid, SubRidge* ridgeRoot, std::map<int, subRidgeParam_t>* subRidgeParams, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares)
{
    auto domain = Generation::Data::get()->getDomainAtSquare(ridgeParams->pos.toGPoint(), EDomainType::Terrain);
    auto domainData = domain->getData<EDomainType::Terrain>();

    if (ridgeParams->branchTier != 0 && ridgeParams->iteration >= ridgeParams->maxIterations)
        return;

    if (std::any_of(ridgeGrid.perimeter.begin(), ridgeGrid.perimeter.end(), [ridgeParams](auto&& seg) { return seg.dist(ridgeParams->pos) < ridgeParams->minDistanceFromGrid; }))
        return;

#if DEBUG_RIDGELINE_CREATION
    QVector3D iterationStartMarker(ridgeParams->pos.x, 600.0f, ridgeParams->pos.z);
    spawn<DLineMarker>(iterationStartMarker, 150.0f, QVector4D(1,0,0,1));
#endif

    // !!! if number of segments ever changes, make sure to change `oneIterationLength` in `processRidge` !!!
    //insert 2 random segments and 1 that's always forward
    if (!insertSegment(ridgeParams, ridgeGrid, ridgeRoot, segments, claimedSquares))
        return;
    if (!insertSegment(ridgeParams, ridgeGrid, ridgeRoot, segments, claimedSquares))
        return;
    if (!insertSegment(ridgeParams, ridgeGrid, ridgeRoot, segments, claimedSquares, true))
        return;

#if DEBUG_RIDGELINE_CREATION
    QVector3D iterationMarker(ridgeParams->pos.x, 150.0f, ridgeParams->pos.z);
    spawn<DLineMarker>(iterationMarker, 150.0f);
#endif

    ridgeParams->steps++;

    if (!ridgeGrid.points.contains(ridgeParams->pos.toGPoint()))
        return;

    // If max square count is reached, do not allow expansion into new squares
    if (claimedSquares->size() >= ridgeParams->maxSquares && !claimedSquares->contains(ridgeParams->pos.toGPoint()))
        return;

    auto subridgeSteps = ridgeParams->steps - ridgeParams->startingStep;

    int maxSteps = ridgeParams->branchTier == 0 ? ridgeParams->mainRidgeSteps : ridgeParams->maxSteps;
    if (subridgeSteps >= maxSteps)
        return;

    ridgeParams->iteration++;

    // Branch check
    // Tier 0 has higher subridge chance to allow for more even subridge distribution
    if ((ridgeParams->branchTier == 0 && ridgeParams->iteration % (std::max(ridgeParams->createSubridgePerIterations / 2, 3)) == 0) 
        || ridgeParams->iteration >= ridgeParams->createSubridgeOnIteration)
    {
        ridgeParams->createSubridgeOnIteration = ridgeParams->createSubridgePerIterations + ridgeParams->iteration + 1;

        std::vector<RidgeSegmentParams> branches;
        branches.emplace_back(createBranchSegment(*ridgeParams));

        std::uniform_real_distribution<> doubleBranchDist(0.0f, 1.0f);

        // The chance for a double branch is lower with each branch tier
        if (doubleBranchDist(Generation::gRandomEngine) > 0.1f / std::min(1.0f / float(ridgeParams->branchTier + 1), 0.95f))
        {
            auto opposedAngle = GVector2D::rotateDegrees(branches.front().baseDir, 180.0f);
            branches.emplace_back(createBranchSegment(*ridgeParams, opposedAngle));
        }

        ridgeMainPass(ridgeParams, ridgeGrid, ridgeRoot, subRidgeParams, segments, claimedSquares);

        if (ridgeParams->branchTier < ridgeRoot->maxTreeLevel - 1)
        {
            for(auto&& branch : branches)
            {
                ridgeRoot->subRidges.emplace_back(ridgeRoot->maxTreeLevel, ridgeRoot);

                branch.subridgeId = (*subRidgeParams)[branch.branchTier].size();
                int subId = ridgeRoot->subRidges.size() - 1;
                (*subRidgeParams)[branch.branchTier].push_back(std::forward_as_tuple(branch, ridgeGrid, ridgeRoot, subId));
            }
        }
    }
    else
        return ridgeMainPass(ridgeParams, ridgeGrid, ridgeRoot, subRidgeParams, segments, claimedSquares);
}

bool DRidgeMarker::insertSegment(RidgeSegmentParams* ridgeParams, const RidgeGridData& ridgeGrid, SubRidge* ridgeRoot, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares, bool straight)
{
    auto domain = Generation::Data::get()->getDomainAtSquare(ridgeParams->pos.toGPoint(), EDomainType::Terrain);
    auto domainData = domain->getData<EDomainType::Terrain>();
    std::uniform_real_distribution<> angleDist(20.0f, 30.0f);
    std::uniform_real_distribution<> angleCorrectionDist(5.f, 15.f);

    float sign = DRidgeMarker::randomSign(true);

    // We want to bend the ridge shape. If sign equals 0 then we move forward
    auto oldDir = ridgeParams->dir;
    if (!straight && std::strong_ordering::equal != fCmp(sign, 0.f))
    {
        // Control the behavior of subridges in order to avoid loop like shapes
        float nextAngle = angleDist(Generation::gRandomEngine);
        GVector2D targetDir = GVector2D::rotateDegrees(ridgeParams->dir, sign*nextAngle);
        auto angle = ridgeParams->dir.angle(targetDir);
        auto angleToBaseDir = targetDir.angle(ridgeParams->baseDir);
        auto previousAngle = oldDir.angle(ridgeParams->baseDir);
        auto maxAngle = 30.0f;
        float angleCorrection = 0;

        // Check if the angle to base direction is within limits
        if(angleToBaseDir > maxAngle || angleToBaseDir < 360.0f - maxAngle)
        {
            // If both new direction, and previous one are out of limits, correct the previous one to trend towards base direction
            if (previousAngle > maxAngle && previousAngle < 360.0f - maxAngle)
            {
                angleCorrection = angleCorrectionDist(Generation::gRandomEngine);
                angle = previousAngle < 180.0f ? previousAngle - angleCorrection : previousAngle + angleCorrection;
                targetDir = GVector2D::rotateDegrees(ridgeParams->dir, angle);
            }
            // If new direction is out of bounds, but old was good, correct the new direction to trend towards base direction
            else
            {
                angleCorrection = angleCorrectionDist(Generation::gRandomEngine);
                angle = angle < 180.0f ? angle - angleCorrection : angle + angleCorrection;
                targetDir = GVector2D::rotateDegrees(ridgeParams->dir, angle);
            }
        }

        ridgeParams->dir = targetDir;
    }

    auto posCandidate = ridgeParams->pos + ridgeParams->dir * ridgeParams->moveLength;

    if (!ridgeGrid.points.contains(posCandidate.toGPoint()))
        return false;

    // If max square count is reached, do not allow expansion into new squares
    if (claimedSquares->size() >= ridgeParams->maxSquares && !claimedSquares->contains(posCandidate.toGPoint()))
        return false;

    // Check new segment, and try correcting it once
    Segment2D potentialSegment(ridgeParams->pos, posCandidate);
    if (auto conflictingSegment = checkNewSegment(potentialSegment, *ridgeParams, *ridgeRoot, *segments); conflictingSegment)
    {
        std::map<float /*distance*/, float /*angle*/> distanceAngleMap;

        sign = (fCmp(sign, 0.f) != std::strong_ordering::equal) ? sign : 1.0;
        float angleCandidate = (-sign) * 50;
        GVector2D targetDir = GVector2D::rotateDegrees(oldDir, angleCandidate);
        posCandidate = ridgeParams->pos + targetDir * ridgeParams->moveLength;
        auto subridgeSteps = ridgeParams->steps - ridgeParams->startingStep;
        float distanceThresholdByLenght = std::min(subridgeSteps * ridgeParams->moveLength * 0.5f, ridgeParams->minDistanceFromRidges);
        float baseDirDeviationLimit = 60.0f;

        while (true)
        {
            angleCandidate -= 10 * (-sign);
            targetDir = GVector2D::rotateDegrees(oldDir, angleCandidate);
            posCandidate = ridgeParams->pos + targetDir * ridgeParams->moveLength;

            if (angleCandidate != std::clamp(angleCandidate, -40.0f, 40.0f))
                break;

            // If new direction is within deviation margin, save it to the map
            if (targetDir.angle(ridgeParams->baseDir) > baseDirDeviationLimit && targetDir.angle(ridgeParams->baseDir) < 360.0f - baseDirDeviationLimit)
                continue;

            distanceAngleMap.emplace(distance(posCandidate, conflictingSegment->second), angleCandidate);
        }

        if (distanceAngleMap.empty())
            return false;

        //Check if the furthest candidate fulfills the weaker distance limit
        if(distanceAngleMap.rbegin()->first < distanceThresholdByLenght)
        {
#if DEBUG_RIDGELINE_CREATION
            QVector3D failedCorrectionMarker(posCandidate.x, 0.0f, posCandidate.z);
            spawn<DLineMarker>(failedCorrectionMarker, 1000, QVector4D(0, 0, 0, 1));
#endif
            return false;
        }

        // Pick furthest point for correction
        targetDir = GVector2D::rotateDegrees(oldDir, distanceAngleMap.rbegin()->second);
        ridgeParams->dir = targetDir;
        posCandidate = ridgeParams->pos + ridgeParams->dir * ridgeParams->moveLength;
        potentialSegment = {ridgeParams->pos, posCandidate};

        // If the segment fails after correction, stop generation
        if (auto finalCheck = checkNewSegment(potentialSegment, *ridgeParams, *ridgeRoot, *segments); finalCheck)
            return false;
    }

#if DEBUG_RIDGELINE_CREATION
    QVector3D segmentMarker(posCandidate.x, 0.0f, posCandidate.z);
    spawn<DLineMarker>(segmentMarker, 150.0f, QVector4D(0.5, 0.5, 1, 1));
#endif

    ridgeParams->pos = posCandidate;
    ridgeRoot->squarePath.push_back(ridgeParams->pos);
    ridgeRoot->visitedSquares.insert(ridgeParams->pos.toGPoint());
    claimedSquares->insert(ridgeParams->pos.toGPoint());
    ridgeParams->steps++;
    return true;
}

std::optional<Segment2D> DRidgeMarker::checkNewSegment(const Segment2D& newSegment, const RidgeSegmentParams& ridgeParams, const SubRidge& ridgeRoot, const std::unordered_map<int, std::vector<Segment2D>>& segments)
{
    // Skip checks for main ridges
    if (ridgeRoot.parent)
    {
        for (auto&& segmentTier : segments)
        {
            auto subridgeSteps = ridgeParams.steps - ridgeParams.startingStep;

            // Branches of same tier (where the current branch is not yet saved)
            if (ridgeParams.branchTier == segmentTier.first)
            {
                float distanceThresholdByLenght = std::min(subridgeSteps * ridgeParams.moveLength * 0.5f, ridgeParams.minDistanceFromRidges);

                for (auto&& segment : segmentTier.second)
                {
                    if (distance(newSegment.second, segment.second) < distanceThresholdByLenght)
                    {
#if DEBUG_RIDGELINE_CREATION
                        QVector3D pinkMarker(newSegment.second.x, 1000.0f, newSegment.second.z);
                        spawn<DLineMarker>(pinkMarker, 1000.0f, QVector4D(1, 0, 1, 1));
#endif
                        return segment;
                    }

                    if (newSegment.intersects(segment, false))
                    {
#if DEBUG_RIDGELINE_CREATION
                        QVector3D redMarker(newSegment.second.x, 1000.0f, newSegment.second.z);
                        spawn<DLineMarker>(redMarker, 1000.0f, QVector4D(1, 0, 0, 1));
#endif
                        return segment;
                    }
                }
            }

            // Parent tier segments
            if (ridgeParams.branchTier - 1 == segmentTier.first)
            {
                float distanceThresholdByLenght = std::min(subridgeSteps * ridgeParams.moveLength * 0.5f, ridgeParams.minDistanceFromRidges);

                for (auto&& segment : segmentTier.second)
                {
                    if (distance(newSegment.second, segment.second) < distanceThresholdByLenght)
                    {
#if DEBUG_RIDGELINE_CREATION
                        QVector3D greenMarker(newSegment.second.x, 1000.0f, newSegment.second.z);
                        spawn<DLineMarker>(greenMarker, 1000.0f, QVector4D(0, 1, 0, 1));
#endif
                        return segment;
                    }

                    if (newSegment.intersects(segment, false))
                    {
#if DEBUG_RIDGELINE_CREATION
                        QVector3D redMarker(newSegment.second.x, 1000.0f, newSegment.second.z);
                        spawn<DLineMarker>(redMarker, 1000.0f, QVector4D(1, 0, 0, 1));
#endif
                        return segment;
                    }
                }
            }

            // Checks against higher tier branches (where 0 is the highest)
            if (ridgeParams.branchTier - 2 >= segmentTier.first)
                for (auto&& segment : segmentTier.second)
                {
                    if (distance(newSegment.second, segment.second) < ridgeParams.minDistanceMultiplierFromHigherRidge * (ridgeParams.branchTier - segmentTier.first))
                    {
#if DEBUG_RIDGELINE_CREATION
                        QVector3D blueMarker(newSegment.second.x, 1000.0f, newSegment.second.z);
                        spawn<DLineMarker>(blueMarker, 1200.0f, QVector4D(0, 0, 1, 1));
#endif
                        return segment;
                    }
                }
        }
    }

    // Check against other segments of landmass
    for (auto&& [ridgeTier, controlPoints] : ridgeParams.otherSegments)
    {
        for (auto&& pt : controlPoints)
        {
            // Here is an assumption that ridges of vastly different tiers will have different height - it is incorrect, but - 
            // to properly predict it, height would need to be assigned during ridgeline generation, not after
            auto standardDistance = ridgeParams.minDistanceFromOtherSegments + (std::abs(ridgeParams.branchTier - ridgeTier) * 2000.0f);
            if (distance(newSegment.second, pt) < standardDistance * ridgeParams.otherSegmentDistFactor)
            {
#if DEBUG_RIDGELINE_CREATION
                QVector3D whiteMarker(newSegment.second.x, 1000.0f, newSegment.second.z);
                spawn<DLineMarker>(whiteMarker, 1000.0f, QVector4D(1, 1, 1, 1));
#endif
                return Segment2D(pt, pt);
            }
        }
    }

    return {};
}

RidgeSegmentParams DRidgeMarker::createBranchSegment(const RidgeSegmentParams& ridgeParams, const GVector2D& fixedAngle)
{
    RidgeSegmentParams branchSegmentParams(ridgeParams);
    GVector2D newBranchAngle;

    if (fixedAngle.isNull())
        newBranchAngle = GVector2D::rotateDegrees(branchSegmentParams.dir, DRidgeMarker::randomSign() * 90.0f);
    else
        newBranchAngle = fixedAngle;

    if (ridgeParams.branchTier == 0)
        branchSegmentParams.iteration = 0;

    branchSegmentParams.subridgeParentId = branchSegmentParams.subridgeId;
    branchSegmentParams.parentBranchTier = branchSegmentParams.branchTier;
    branchSegmentParams.dir = newBranchAngle;
    branchSegmentParams.baseDir = newBranchAngle;
    branchSegmentParams.parentDir = ridgeParams.dir;
    branchSegmentParams.startingStep = ridgeParams.steps;
    branchSegmentParams.branchTier++;
    branchSegmentParams.createSubridgeOnIteration = branchSegmentParams.iteration + ridgeParams.createSubridgePerIterationsSub;
    branchSegmentParams.createSubridgePerIterations = ridgeParams.createSubridgePerIterationsSub;

    return branchSegmentParams;
}

void DRidgeMarker::branchPass(RidgeSegmentParams* ridgeParams, const RidgeGridData& ridgeGrid, SubRidge* ridgeRoot, RidgeSegmentParams* rootRidgeParams, std::map<int, subRidgeParam_t>* subRidgeParams, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares)
{
    ridgeRoot->squarePath.push_back(ridgeParams->pos);
    ridgeRoot->visitedSquares.insert(ridgeParams->pos.toGPoint());
    ridgeRoot->tablelandType = ridgeParams->tablelandType;
    claimedSquares->insert(ridgeParams->pos.toGPoint());

    const auto& ridgeParamsParent = ridgeParams->subridgeParentId == -1 ? *rootRidgeParams : std::get<RidgeSegmentParams>((*subRidgeParams)[ridgeParams->parentBranchTier][ridgeParams->subridgeParentId]);
    auto maxStepsFactor = ridgeParams->spreadFactor;

    auto sideFromParent = std::sgn(GVector2D::dotProduct(ridgeParams->parentDir.rotatedRight90(), ridgeParams->baseDir));
    auto lenghtRatioFromParent =(float(ridgeParams->startingStep) - float(ridgeParamsParent.startingStep)) / (float(ridgeParamsParent.steps) - float(ridgeParamsParent.startingStep));
    auto ridgeOffsetFactor = (ridgeParams->spreadFactor - 0.5f) * 2.0f;

    float outOfRidgeCenterAngleOffset = 0.0f;

    // Offset branch angle depending on position on parent ridge
    // Offset in both directions for branches originating from main ridge, and outwards for rest
    if (ridgeParamsParent.branchTier == 0)
        outOfRidgeCenterAngleOffset = ((lenghtRatioFromParent * 100.0f) - 50.0f) * sideFromParent;
    else
        outOfRidgeCenterAngleOffset = lenghtRatioFromParent * sideFromParent * 50.0f;

    ridgeParams->maxSteps = ridgeParams->branchTier <= 1 ? ridgeParams->maxSteps : ridgeParams->maxSteps * maxStepsFactor;
    ridgeParams->dir = GVector2D::rotateDegrees(ridgeParams->dir, outOfRidgeCenterAngleOffset);
    ridgeParams->baseDir = GVector2D::rotateDegrees(ridgeParams->baseDir, outOfRidgeCenterAngleOffset);
    ridgeParams->parentDir = GVector2D::rotateDegrees(ridgeParams->parentDir, outOfRidgeCenterAngleOffset);

    ridgeMainPass(ridgeParams, ridgeGrid, ridgeRoot, subRidgeParams, segments, claimedSquares);

    if (ridgeRoot->squarePath.size() > 4)
    {
        for (auto it = ridgeRoot->squarePath.begin() + 1; it != ridgeRoot->squarePath.end(); it++)
        {
            if (segments->contains(ridgeParams->branchTier))
                (*segments)[ridgeParams->branchTier].emplace_back(*(it - 1), *it);
            else
                segments->emplace(ridgeParams->branchTier, std::vector<Segment2D>({ Segment2D(*(it - 1), *it) }));
        }
    }
    else
    {
        ridgeRoot->squarePath.clear();
    }
}

void DRidgeMarker::processSubRidges(RidgeSegmentParams* rootRidgeParams, std::map<int, subRidgeParam_t>* subRidgeParams, int level, std::unordered_map<int, std::vector<Segment2D>>* segments, QSet<GPoint>* claimedSquares)
{
    for(size_t i = 0; i < (*subRidgeParams)[level].size(); i++)
    {
        auto&& [ridgeParams, ridgeGrid, ridgeRoot, id] = (*subRidgeParams)[level][i];
        branchPass(&ridgeParams, ridgeGrid, &ridgeRoot->subRidges[id], rootRidgeParams, subRidgeParams, segments, claimedSquares);
    }

    if (subRidgeParams->contains(level + 1))
        processSubRidges(rootRidgeParams, subRidgeParams, level + 1, segments, claimedSquares);
}

void DRidgeMarker::applyMarginToSet(QSet<GPoint>* set, const QSharedPointer<DDomain>& domain, int marginSize)
{
    QSet<GPoint> marginSet;

    auto toMarginSet = [&](const GPoint& sq)
    {
        if (Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Water))
            return;

        if (set->contains(sq))
            marginSet.insert(sq);
        else
            for (auto&& setSq : *set)
                if (sq.isNeighbor(setSq, true) && !Generation::Data::get()->getDomainAtSquare(setSq, EDomainType::Water))
                    marginSet.insert(setSq);
    };

    auto&& heightBounds = Generation::Data::get()->getDomainHeightBounds();
    if (!heightBounds.contains(domain->getGuid()))
        return;

    auto&& domainHeightBounds = heightBounds.at(domain->getGuid());
    if (domainHeightBounds.contains(Generation::HeightBoundOrigin::Domain))
        for (auto&& [otherDomainGuid, data] : domainHeightBounds.at(Generation::HeightBoundOrigin::Domain))
        {
            auto&& otherDomain = *Generation::Data::get()->findDomainByGuid(otherDomainGuid);

            if (domain->getData<EDomainType::Terrain>()->maxHeight > otherDomain->getData<EDomainType::Terrain>()->maxHeight)
            {
                for(auto&& [ignore, segments] : data)
                    for (auto&& segment : segments)
                    {
                        toMarginSet(GVector2D(segment.first).toGPoint());
                        toMarginSet(GVector2D(segment.second).toGPoint());
                    }
            }
        }

    *set -= marginSet;
    for (int i = 0; i < marginSize - 1; ++i)
    {
        for (auto it = marginSet.begin(); it != marginSet.end(); it++)
            toMarginSet(*it);

        *set -= marginSet;
    }

#if DEBUG_MARGIN
    for (auto&& gp : marginSet)
        Polygon2D({
            {gp.x * GRID_SEGMENT_WIDTH, gp.z * GRID_SEGMENT_WIDTH},
            {(gp.x + 1) * GRID_SEGMENT_WIDTH, gp.z * GRID_SEGMENT_WIDTH },
            {(gp.x + 1) * GRID_SEGMENT_WIDTH, (gp.z + 1) * GRID_SEGMENT_WIDTH },
            {gp.x * GRID_SEGMENT_WIDTH, (gp.z + 1) * GRID_SEGMENT_WIDTH } }).debugPlot({ 0,0,1,1 }, 1000);
#endif // DEBUG_MARGIN
}

GVector2D DRidgeMarker::calcCenter(std::vector<QSharedPointer<Generation::SquigSquare>>* area)
{
    GVector2D BBoxTopLeft, BBoxBotRight;
    BBoxTopLeft.x = std::numeric_limits<float>::max();
    BBoxTopLeft.z = std::numeric_limits<float>::min();
    BBoxBotRight.x = std::numeric_limits<float>::min();
    BBoxBotRight.z = std::numeric_limits<float>::max();

    for (auto& sq : *area)
    {
        if (BBoxTopLeft.x > sq->coords.x)BBoxTopLeft.x = sq->coords.x;
        if (BBoxTopLeft.z < sq->coords.z)BBoxTopLeft.z = sq->coords.z;
        if (BBoxBotRight.x < sq->coords.x)BBoxBotRight.x = sq->coords.x;
        if (BBoxBotRight.z > sq->coords.z)BBoxBotRight.z = sq->coords.z;
    }

    return GVector2D((BBoxTopLeft.x + BBoxBotRight.x) / 2.0f, (BBoxTopLeft.z + BBoxBotRight.z) / 2.0f);
}

std::tuple<int, int, int, float> DRidgeMarker::ridgeParamsToValues(const RidgeGenParams& params)
{
    static std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

    int iterations = std::lerp(PRidgeCharacter.size[params.size].first, PRidgeCharacter.size[params.size].second, distribution(Generation::gRandomEngine));
    int subridgePerIterationMain = std::lerp(PRidgeCharacter.complexityMain[params.complexityMain].first, PRidgeCharacter.complexityMain[params.complexityMain].second, distribution(Generation::gRandomEngine));
    int subridgePerIterationSub = std::lerp(PRidgeCharacter.complexitySub[params.complexitySub].first, PRidgeCharacter.complexitySub[params.complexitySub].second, distribution(Generation::gRandomEngine));
    float spreadFactor = std::lerp(PRidgeCharacter.spread[params.spread].first, PRidgeCharacter.spread[params.spread].second, distribution(Generation::gRandomEngine));

    return { iterations, subridgePerIterationMain, subridgePerIterationSub, spreadFactor };
}