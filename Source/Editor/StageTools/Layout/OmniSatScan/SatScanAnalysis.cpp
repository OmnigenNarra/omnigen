#include "stdafx.h"
#include "SatScanAnalysis.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Constants.h"

#include "Scene/Generation/Stages/ContourLines/StageGeneration_ContourLines.h"

#include <ranges>
#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdal_alg.h>

SatScanAnalysis::SatScanAnalysis(bool copyFromWorld, float demHeightFactor, const QSet<GPoint>& availableSquares) 
    : fHeightMultiplier(demHeightFactor)
    , bCopyFromWorld(copyFromWorld)
{
    GDALDataset* dataset, * ridgeDataset, * finalDataset;
    GDALAllRegister();

    std::string input = "Output/Earth Engine/DEMRaster.tif";
    std::string ridgeOutput = "Output/Earth Engine/RidgeRaster.tif";
    std::string output = "Output/Earth Engine/FinalRaster.tif";

    GDALDriver* driverTiff;
    driverTiff = GetGDALDriverManager()->GetDriverByName("GTiff");

    dataset = static_cast<GDALDataset*>(GDALOpen(input.c_str(), GA_ReadOnly));
    dataset->GetRasterBand(1)->ComputeStatistics(false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    nCols = dataset->GetRasterBand(1)->GetXSize();
    nRows = dataset->GetRasterBand(1)->GetYSize();

    gdalDEM.resize(nCols * nRows);
    std::vector<float> datasetOutputBuffer(nCols * nRows), datasetFinalBuffer(nCols * nRows);

    ridgeDataset = driverTiff->CreateCopy(ridgeOutput.c_str(), dataset, false, nullptr, nullptr, nullptr);

    dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, nCols, nRows, gdalDEM.data(), nCols, nRows, GDT_Float32, 0, 0);
    dataset->GetGeoTransform(aTargetDEMGeoTransform);

    int highestPoint = 0;
    // Peak map for future ridgeline generation
    std::multimap<float, int> peakMap;

    // Assign peaks and ridges according to neighboring pixels
    for (int i = 0; i < gdalDEM.size(); i++)
    {
        EGeoForm geoForm = EGeoForm::Other;

        if (highestPoint < gdalDEM[i])
            highestPoint = gdalDEM[i];

        if (gdalDEM[i] > 6)
        {
            if (auto points = findPoints(i, nCols, nRows); points)
            {
                geoForm = geoFormAssignment(i, *points, gdalDEM);
                if (geoForm == EGeoForm::Peak)
                    peakMap.emplace(gdalDEM[i], i);
            }
        }

        datasetOutputBuffer[i] = static_cast<int>(geoForm);
    }

    // Save raster data as file
    ridgeDataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, nCols, nRows, datasetOutputBuffer.data(), nCols, nRows, GDT_Float32, 0, 0);

    // Additional dataset for better height visualization
    finalDataset = driverTiff->CreateCopy(output.c_str(), ridgeDataset, false, nullptr, nullptr, nullptr);

    // Mark ridge lines as decreasingly darker points, starting from highest point as brightest
    for (int i = 0; i < gdalDEM.size(); i++)
    {
        if (datasetOutputBuffer[i] > 0)
            datasetFinalBuffer[i] = 250.0 - ((highestPoint - gdalDEM[i]) * 5.0);
        else
            datasetFinalBuffer[i] = 0;
    }

    for (auto&& kv = peakMap.rbegin(); kv != peakMap.rend(); kv++)
    {
        if (peakCheck((*kv).second, 6))
            continue;

        vSortedPeaks.emplace_back((*kv).second);
    }

    // For each peak try to find its ridgeline
    for(auto&& kv = vSortedPeaks.begin(); kv != vSortedPeaks.end(); kv++)
    {
        // Check if peak space was not already claimed by other mountain
        if (!checkIfUnoccupied(rasterIdxToPos(*kv), 6))
            continue;

        RidgePoint peakPt;
        peakPt.pos = rasterIdxToPos(*kv);
        peakPt.height = gdalDEM[*kv] * fHeightMultiplier;

        vMountains.emplace_back(MountainData(peakPt));
        ridgelineSearch(*kv, 4, 12, 90.0, datasetOutputBuffer, gdalDEM);
    }

    // Save raster data as file
    finalDataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, nCols, nRows, datasetFinalBuffer.data(), nCols, nRows, GDT_Float32, 0, 0);

    GDALClose(dataset);
    GDALClose(ridgeDataset);
    GDALClose(finalDataset);
    GDALDestroyDriverManager();

    // Clean mountain map of invalid ridgelines
    for (int i = 0; i < vMountains.size(); ++i)
    {
        if (vMountains[i].getMountainPts().size() < 2)
            vMountains.erase(vMountains.begin() + i--);
    }

    // It's a rather rare situation, as most plain areas have at least some sort of distinguishable ridgeline
    if (vMountains.empty())
        bNoRidgeline = true;

    computeScaleAndOffset(availableSquares);

    // Purely debug, as Copy From World will work differently
    if (copyFromWorld)
    {
        std::vector<RidgelineDomain> domainRequest(vMountains.size());
        auto squaresPerMountain = getRidgelineArea(availableSquares);
        for (int i = 0; i < vMountains.size(); i++)
        {
            RidgelineDomain rd = { .mountainIndices = {i}, .lf = EHammondLandforms::HighMountains, .squares = squaresPerMountain[i] };
            domainRequest[i] = rd;
        }

        SatScanDomainGeneration::generateDomainsFromResults(domainRequest);

        Generation::Data::get()->setGenerationStage(EGenerationStage::Ridges, true, true);

        for (int i = 0; i < vMountains.size(); i++)
            vMountains[i].drawRidgesInDomain(domainRequest[i].domainId, nCols, nRows, availableSquares);

        Generation::Data::get()->setGenerationStage(EGenerationStage::ContourLines, false, true);

        float lowestHeight = std::numeric_limits<float>::max();
        for (auto&& pt : gdalDEM)
            lowestHeight = pt < lowestHeight ? pt : lowestHeight;

        lowestHeight = lowestHeight <= 3.0f ? 3.0f : lowestHeight;

        float margin = 3;
        int bufferCols = nCols + (margin * 2);
        int bufferRows = nRows + (margin * 2);
        std::vector<float> bufferDem(bufferCols * bufferRows, 0.0f);
        for(int colIdx = 0; colIdx < bufferCols; ++colIdx)
        {
            for (int rowIdx = 0; rowIdx < bufferRows; ++rowIdx)
            {
                int bufferIdx = colIdx + (bufferCols * rowIdx);

                // Set artificial value for margin points of buffer DEM
                if(    colIdx < margin
                    || rowIdx < margin
                    || colIdx >= nCols + margin
                    || rowIdx >= nRows + margin)
                {
                    float height = 2.0;
                    bufferDem[bufferIdx] = height;
                    continue;
                }

                int idx = (colIdx - margin) + (nCols * (rowIdx - margin));
                float height = (gdalDEM[idx] - lowestHeight) * fHeightMultiplier * fHeightAuxiliaryMultiplier;
                bufferDem[bufferIdx] = height;
            }
        }

        GPoint offset = GPoint(fOffsetX / fScaleX  , fOffsetZ / fScaleZ);
        Generation::StageGen<EGenerationStage::ContourLines>::createIsohypsesOutOfDEM(bufferDem, margin, offset, bufferRows, bufferCols, fScaleX, fScaleZ, true);
    }
    // Proper Ridgeline analysis
    else
    {
        auto gdal = SatScanLandforms(fHeightMultiplier);
        auto mainArea = getRasterAreaForRidgeline(availableSquares);
        auto mainlf = gdal.computeLandform(mainArea);

        std::vector<RidgelineDomain> domainRequest = {RidgelineDomain({ .mountainIndices = {0}, .lf = mainlf, .areaPercentage = 1.0f })};

        // Skip analysis for tablelands
        if(!(mainlf >= EHammondLandforms::TablelandsWithHighRelief && mainlf <= EHammondLandforms::TablelandsWithOpenLowRelief))
            analyzeRidgelines();

        auto satScanGen = SatScanDomainGeneration();
        satScanGen.createLandformDomains(&domainRequest, availableSquares, false);
        setDomainParameters(domainRequest.front());
    }
}

std::optional<std::vector<int>> SatScanAnalysis::findPoints(int midPtIdx, int nCols, int nRows, int cellDistance, int circleDetail)
{
    // Check if mid point is in an acceptable distance from left/right borders
    if (auto pt = midPtIdx % nCols; !(pt >= cellDistance && pt <= nCols - (cellDistance + 1)))
        return {};

    // Check if mid point is in an acceptable distance from top/bottom borders
    if (auto pt = midPtIdx / nCols; !(pt >= cellDistance && pt < nRows - cellDistance))
        return {};

    std::vector<int> circlePoints;
    static std::vector<std::pair<int, int>> jumpMap;

    // Compute the jumpMap again for new parameters (jumpMap[0].first is always for angle 0, as such should be equal to cellDistance)
    if(jumpMap.empty() || jumpMap.size() != circleDetail || jumpMap[0].first != cellDistance)
    {
        jumpMap.clear();
        for (int i = 0; i < circleDetail; i++)
        {
            auto angle = ((std::numbers::pi * 2) / circleDetail) * i;
            std::pair<int, int> jump(std::round(cellDistance * std::cosf(angle)), std::round(cellDistance * std::sinf(angle)));
            jumpMap.emplace_back(jump);
        }
    }

    for(auto&& jump : jumpMap)
        circlePoints.emplace_back(midPtIdx + (jump.first * nCols) + jump.second);

    return circlePoints;
}

std::optional<std::vector<int>> SatScanAnalysis::directionalFindPoints(int midPtIdx, int cellDistance, int circleDetail, float allowedAngle, const GVector2D& searchVector, const std::vector<float>& ridgeRaster)
{
    std::vector<int> allowedPts;
    std::vector<int> mergedPts;
    auto midPtPos = rasterIdxToPos(midPtIdx);

    if (auto points = findPoints(midPtIdx, nCols, nRows, cellDistance, circleDetail); points)
    {
        for (auto&& ptIdx : *points)
        {
            GVector2D ptPos = rasterIdxToPos(ptIdx);
            GVector2D ptVec = GVector2D(ptPos - midPtPos).normalized();

            if (auto angle = searchVector.angle(ptVec); angle >= (allowedAngle / 2) && angle <= 360.0 - (allowedAngle / 2))
                continue;
            else
                allowedPts.emplace_back(ptIdx);
        }

        std::vector<int> tempPts;

        // Merge neighboring points
        auto merge = [&]()
        {
            GVector2D mergedPos;

            for(auto&& tpt :tempPts)
                mergedPos += rasterIdxToPos(tpt);

            mergedPos = { std::round(mergedPos.x / tempPts.size()), std::round(mergedPos.z / tempPts.size()) };
            mergedPts.emplace_back((mergedPos.z * nCols) + mergedPos.x);

            tempPts.clear();
        };

        // Check if points in allowed angle are of correct type - if they are neighboring merge them to get proper ridge line direction
        for (int i = 0; i < allowedPts.size(); i++)
        {
            if (auto&& p = ridgeRaster[allowedPts[i]]; p == static_cast<int>(EGeoForm::Peak) || p == static_cast<int>(EGeoForm::Ridge))
                tempPts.emplace_back(allowedPts[i]);
            else if (!tempPts.empty())
                merge();
        }

        if (!tempPts.empty())
            merge();
    }

    return mergedPts;
}

EGeoForm SatScanAnalysis::geoFormAssignment(int midPt, const std::vector<int>& ridgeRaster, const std::vector<float>& demRaster)
{
    auto midPtH = demRaster[midPt];

    std::vector<EElevation> elevPts(ridgeRaster.size());

    int higherCounter = 0;
    int equalCounter = 0;
    int lowerCounter = 0;

    for (int i = 0; i < elevPts.size(); i++)
    {
        if (demRaster[ridgeRaster[i]] > midPtH)
        {
            elevPts[i] = EElevation::Higher;
            higherCounter++;
        }
        else if(demRaster[ridgeRaster[i]] < midPtH)
        {
            elevPts[i] = EElevation::Lower;
            lowerCounter++;
        }
        else
        {
            elevPts[i] = EElevation::Equal;
            equalCounter++;
        }
    }

    // Simple requirements for peaks and ridges, needs to be revised if other forms would be desired
    if (higherCounter > 3 || lowerCounter < 5 || equalCounter > 3)
        return EGeoForm::Other;

    // Peaks are quite straight forward, all neighbors must be lower
    if (lowerCounter == elevPts.size())
        return EGeoForm::Peak;

    return EGeoForm::Ridge;
}

void SatScanAnalysis::ridgelineSearch(int rootPeakIdx, int searchDistance, int circleDetail, float allowedAngle, const std::vector<float>& ridgeRaster, const std::vector<float>& demRaster)
{
    auto&& ridgePts = vMountains.back().getMountainPts();
    std::vector<int> newPoints;
    int rootIdx = ridgePts.size() - 1;

    int sDist = searchDistance;

    searchStart:
    if (auto points = findPoints(rootPeakIdx, nCols, nRows, sDist, circleDetail); points)
    {
        int ridgePtsFound = 0;
        for (auto ptIdx : *points)
        {
            if (auto&& p = ridgeRaster[ptIdx]; p == static_cast<int>(EGeoForm::Ridge) || p == static_cast<int>(EGeoForm::Peak))
                ridgePtsFound++;
        }

        // Too many points found, widen the search
        if (ridgePtsFound == points->size())
        {
            sDist++;
            goto searchStart;
        }

        // Save points that will be the root for further ridge line search
        for (auto ptIdx : *points)
        {
            if(auto && p = ridgeRaster[ptIdx]; p == static_cast<int>(EGeoForm::Ridge) || p == static_cast<int>(EGeoForm::Peak))
            {
                auto ptPos = rasterIdxToPos(ptIdx);

                if (!checkIfUnoccupied(ptPos, searchDistance - 1, rootIdx))
                    continue;

                RidgePoint rpt;
                rpt.parent = rootIdx;
                rpt.height = demRaster[ptIdx] * fHeightMultiplier;
                rpt.pos = ptPos;
                int idx = ridgePts.size();

                vMountains.back().addRidgePoint(rpt);
                vMountains.back().addChildToPoint(idx, rpt.parent);
                newPoints.emplace_back(idx);
            }
        }
    }

    while (true)
    {
        // Preliminary point search, with angle restriction
        std::vector<int> nextPts;
        for (auto&& ptIdx : newPoints)
            if (auto newPts = searchForRidgePoints(ptIdx, 4, 18, 180, ridgeRaster, demRaster); newPts)
                nextPts.insert(nextPts.end(), 
                    std::make_move_iterator((*newPts).begin()), 
                    std::make_move_iterator((*newPts).end()));

        // If no more points are found by the preliminary search, check all saved points once more with wider range and no angle restriction.
        // This is required due to ridge point raster inconsistency (especially true for subridges)
        // and is done after the preliminary search, to avoid possible ridge line shortcuts due to the wider search range
        if (nextPts.empty())
            if (auto newPts = searchForSubridges(6, 28, ridgeRaster, demRaster); newPts)
                nextPts.insert(nextPts.end(),
                    std::make_move_iterator((*newPts).begin()),
                    std::make_move_iterator((*newPts).end()));

        // If both preliminary and auxiliary searches find no new points, finish the ridge line search
        if (nextPts.empty())
            break;

        // Points found during the loop will be search root points in next loop
        newPoints.clear();
        newPoints.insert(newPoints.begin(),
            std::make_move_iterator(nextPts.begin()),
            std::make_move_iterator(nextPts.end()));
    }
}

std::optional<std::vector<int>> SatScanAnalysis::searchForRidgePoints(int parentIdx, int searchDistance, int circleDetail, float allowedAngle, const std::vector<float>& ridgeRaster, const std::vector<float>& demRaster)
{
    auto&& ridgePts = vMountains.back().getMountainPts();
    std::vector<int> pointsFound;
    GVector2D ridgeVec = GVector2D(ridgePts[parentIdx].pos - ridgePts[ridgePts[parentIdx].parent].pos).normalized();

    if (auto points = directionalFindPoints(posToRasterIdx(ridgePts[parentIdx].pos), searchDistance, circleDetail, allowedAngle, ridgeVec, ridgeRaster); points)
    {
        for (auto ptIdx : *points)
        {
            GVector2D ptPos = rasterIdxToPos(ptIdx);
            GVector2D ptVec = GVector2D(ptPos - ridgePts[parentIdx].pos).normalized();

            if (auto angle = ridgeVec.angle(ptVec); angle >= (allowedAngle / 2) && angle <= 360.0 - (allowedAngle / 2))
                continue;

            if (!checkIfUnoccupied(ptPos, 4, parentIdx))
                continue;

            int tempIdx = ptIdx;

            if (auto&& peak = peakCheck(ptIdx, 4); peak)
            {
                vMountains.back().addPeakIdx(ridgePts.size());
                tempIdx = *peak;
            }

            RidgePoint rpt;
            rpt.parent = parentIdx;
            rpt.height = demRaster[tempIdx] * fHeightMultiplier;
            rpt.pos = rasterIdxToPos(ptIdx);
            int idx = ridgePts.size();

            vMountains.back().addRidgePoint(rpt);
            vMountains.back().addChildToPoint(idx, parentIdx);
            pointsFound.emplace_back(idx);
        }
    }

    // If no new points are found, search again more with wider range and narrower angle - this is required due to ridge raster point inconsistency
    if (searchDistance < 7 && pointsFound.empty())
        if (auto fPts = searchForRidgePoints(parentIdx, searchDistance + 1, circleDetail + 0.3 * circleDetail, allowedAngle - allowedAngle * 0.5, ridgeRaster, demRaster); fPts)
            return fPts;

    return pointsFound;
}

std::optional<std::vector<int>> SatScanAnalysis::searchForSubridges(int searchDistance, int circleDetail, const std::vector<float>& ridgeRaster, const std::vector<float>& demRaster)
{
    auto&& ridgePts = vMountains.back().getMountainPts();
    std::vector<int> newPoints;

    for (int i = 0; i < ridgePts.size(); i++)
    {
        if (auto points = findPoints(posToRasterIdx(ridgePts[i].pos), nCols, nRows, searchDistance, circleDetail); points)
        {
            for (auto&& pIdx : *points)
            {
                if (auto&& p = ridgeRaster[pIdx]; p == static_cast<int>(EGeoForm::Ridge) || p == static_cast<int>(EGeoForm::Peak))
                {
                    auto ptPos = rasterIdxToPos(pIdx);

                    if (!checkIfUnoccupied(ptPos, 5, pIdx))
                        continue;

                    RidgePoint rpt;
                    rpt.parent = i;
                    rpt.height = demRaster[pIdx] * fHeightMultiplier;
                    rpt.pos = ptPos;
                    int idx = ridgePts.size();

                    vMountains.back().addRidgePoint(rpt);
                    vMountains.back().addChildToPoint(idx, rpt.parent);
                    newPoints.emplace_back(idx);
                }
            }
        }
    }

    return newPoints;
}

bool SatScanAnalysis::checkIfUnoccupied(GVector2D pos, int allowedDistance, int parentIdx /*= -1*/)
{
    for (int i = 0; i < vMountains.size(); i++)
    {
        auto&& mountainPts = vMountains[i].getMountainPts();
        for (int j = 0; j < mountainPts.size(); j++)
        {
            // New points are always added to last vector of ridgePtMap
            if (i == vMountains.size() - 1 && parentIdx == j)
                continue;

            if (mountainPts[j].pos.dist(pos) < allowedDistance)
                return false;
        }
    }

    return true;
}

std::optional<int> SatScanAnalysis::peakCheck(int idx, int allowedDistance)
{
    if (vSortedPeaks.size() == 0)
        return {};

    auto ptPos = rasterIdxToPos(idx);

    for (int i = 0; i < vSortedPeaks.size(); i++)
    {
        if (ptPos.dist(rasterIdxToPos(vSortedPeaks[i])) < allowedDistance)
            return vSortedPeaks[i];
    }

    return {};
}

void SatScanAnalysis::analyzeRidgelines()
{
    if (bNoRidgeline)
        return;

    // Assign data for each individual mountain
    for (int i = 0; i < vMountains.size(); i++)
    {
        vMountains[i].computeRidgeline();

        // Valley profile
        analyzeMainAndInnerValleyProfiles(&vMountains[i]);
    }

    analyzeOuterValleyAndSlopeProfile();
}

std::vector<QSet<GPoint>> SatScanAnalysis::getRidgelineArea(const QSet<GPoint>& availableSquares)
{
    std::unordered_map<GPoint, bool> claimMap;
    for (auto&& pt : availableSquares)
        claimMap[pt] = false;

    std::vector<QSet<GPoint>> squaresPerMountain(vMountains.size());

    // Assign squares to ridgelines (from biggest ridgeline to smallest)
    for (int i = 0; i < vMountains.size(); i++)
    {
        QSet<GPoint> moutainSquares;
        auto&& pts = vMountains[i].getMountainPts();
        for (auto&& pt : pts)
        {
            GPoint gPt = {static_cast<int>(std::floor((pt.pos.x * fScaleX + fOffsetX) / GRID_SEGMENT_WIDTH)), static_cast<int>(std::floor((pt.pos.z * fScaleZ + fOffsetZ) / GRID_SEGMENT_WIDTH))};
            if (availableSquares.contains(gPt) && claimMap.at(gPt) == false)
            {
                moutainSquares.insert(gPt);
                claimMap.at(gPt) = true;
            }
        }
        squaresPerMountain[i] = moutainSquares;
        vMountains[i].setSquareCountOfRidgeline(moutainSquares.size());
    }

    // Expand each ridgeline area until each returns false on an expansion attempt
    std::vector<bool> areaExpandable(vMountains.size(), true);

    QSet<GPoint> unclaimedSquares;
    for (auto&& squareStatus : claimMap)
        if (squareStatus.second == false)
            unclaimedSquares.insert(squareStatus.first);

    while(true)
    {
        for (int i = 0; i < vMountains.size(); i++)
        {
            if (areaExpandable[i] == false)
                continue;

            int squaresLeft = availableSquares.size();
            if (auto&& newSquares = SatScanDomainGeneration::expandAreaBorder(squaresPerMountain[i], &unclaimedSquares, &squaresLeft); newSquares)
                squaresPerMountain[i].unite(*newSquares);
            else
                areaExpandable[i] = false;
        }

        auto sum = std::accumulate(areaExpandable.begin(), areaExpandable.end(), 0);
        if (sum == 0)
            break;
    }

    return squaresPerMountain;
}

std::vector<int> SatScanAnalysis::getRasterAreaForRidgeline(const QSet<GPoint>& ridgelineArea)
{
    std::vector<int> demPts;
    std::unordered_set<int> verification;

    int squareLength = static_cast<int>(std::floor(GRID_SEGMENT_WIDTH / fScaleX));
    int squareHeigth = static_cast<int>(std::floor(GRID_SEGMENT_WIDTH / fScaleZ));

    // Calculate raster points that are "under" a grid square
    for (auto&& pt : ridgelineArea)
    {
        GVector2D squareStart = { std::floor((pt.x * GRID_SEGMENT_WIDTH - fOffsetX) / fScaleX), std::floor((pt.z * GRID_SEGMENT_WIDTH - fOffsetZ) / fScaleZ) };
        auto rasterIdx = posToRasterIdx(squareStart);
        for (int i = 0; i < squareHeigth; i++)
        {
            for (int j = 0; j < squareLength; j++)
            {
                demPts.emplace_back(rasterIdx + j + (i * nCols));
                verification.emplace(rasterIdx + j + (i * nCols));
            }
        }

        Q_ASSERT(demPts.size() == verification.size());
    }

    return demPts;
}

void SatScanAnalysis::computeScaleAndOffset(const QSet<GPoint>& availableSquares)
{
    // Scale the raster area to available grid area
    GPoint bottomRight = { 0,0 };
    GPoint topLeft{ GRID_SEGMENT_COUNT, GRID_SEGMENT_COUNT };

    for (auto&& square : availableSquares)
    {
        if (square.x <= topLeft.x && square.z <= topLeft.z)
            topLeft = square;

        if (square.x >= bottomRight.x && square.z >= bottomRight.z)
            bottomRight = square;
    }

    fScaleX = ((bottomRight.x - topLeft.x) * GRID_SEGMENT_WIDTH + GRID_SEGMENT_WIDTH) / static_cast<float>(nCols);
    fScaleZ = ((bottomRight.z - topLeft.z) * GRID_SEGMENT_WIDTH + GRID_SEGMENT_WIDTH) / static_cast<float>(nRows);
    fOffsetX = topLeft.x * GRID_SEGMENT_WIDTH;
    fOffsetZ = topLeft.z * GRID_SEGMENT_WIDTH;
}

std::optional<std::vector<QVector3D>> SatScanAnalysis::getDEMShapeBetweenPoints(const GVector2D& firstPoint, const GVector2D& secondPoint)
{
    std::vector<QVector3D> shape = {QVector3D(firstPoint.x, gdalDEM[posToRasterIdx(firstPoint)], firstPoint.z )};

    GVector2D vectorBetweenPoints = (secondPoint - firstPoint).normalized();

    float distanceBetweenPoints = firstPoint.dist(secondPoint);
    if (distanceBetweenPoints == 0)
        return {};

    auto&& skip = [&](const QVector3D& pt) 
    {
        for (auto&& point : shape)
            if (point == pt)
                return true;

        return false;
    };

    // The distance between two DEM points is equal to 1 so (0,0) is the top left corner, (nCols, nRows) is the bottom right corner
    int distance = 1;
    while (true)
    {
        GVector2D movement = {
            (std::round(vectorBetweenPoints.x * distance)),
            (std::round(vectorBetweenPoints.z * distance))};

        GVector2D newPosition = firstPoint + movement;
        QVector3D newPoint(newPosition.x, gdalDEM[posToRasterIdx(newPosition)], newPosition.z);
        distance++;

        if (distanceBetweenPoints < firstPoint.dist(newPosition))
            break;

        if (skip(newPoint))
            continue;

        shape.emplace_back(newPoint);
    }

    return shape;
}

EValleyProfile SatScanAnalysis::getValleyProfile(const std::vector<QVector3D>& shapeBetweenPoints)
{
    if (shapeBetweenPoints.size() < 3)
        return EValleyProfile::Undefined;

    int lowestPointIdx = 0;
    for (int i = 1; i < shapeBetweenPoints.size() - 1; ++i)
    {
        if (shapeBetweenPoints[lowestPointIdx].y() > shapeBetweenPoints[i].y())
            lowestPointIdx = i;
    }

    GVector2D firstPeak(shapeBetweenPoints[0].y(), 0);
    GVector2D secondPeak(shapeBetweenPoints[shapeBetweenPoints.size() - 1].y(), shapeBetweenPoints.size() - 1);
    GVector2D localLowest(shapeBetweenPoints[lowestPointIdx].y(), lowestPointIdx);

    float valleyAngle = (firstPeak - localLowest).normalized().angle((secondPeak - localLowest).normalized());
    if (valleyAngle < 130)
        return EValleyProfile::Sharp;
    else if (valleyAngle < 165)
        return EValleyProfile::Normal;

    return EValleyProfile::Gentle;
}

EValleyProfile SatScanAnalysis::getMainRidgeProfile(const MountainData& mountain)
{
    auto&& ridgePoints = mountain.getMountainPts();
    auto&& peakPointsIndices = mountain.getPeakIndices();

    std::multimap<float, int> similarPeaks;
    // Point 0 of ridge points is the highest - main - peak
    float mainPeakHeight = ridgePoints[0].height;
    for (auto&& peak : peakPointsIndices)
    {
        // Skip main peak
        if (peak == 0)
            continue;

        if (fCmp(ridgePoints[peak].height, mainPeakHeight, 0.1f) == std::strong_ordering::equal)
            similarPeaks.emplace(ridgePoints[peak].height, peak);
    }

    if (similarPeaks.empty())
        return EValleyProfile::Normal;

    int idx = similarPeaks.rbegin()->second;

    std::vector<QVector3D> shapeBetweenPeaks;
    shapeBetweenPeaks.emplace_back(QVector3D(0.0f, ridgePoints[idx].height, 0.0f));

    while (true)
    {
        idx = ridgePoints[idx].parent;
        if (idx == 0)
            break;

        shapeBetweenPeaks.emplace_back(QVector3D(0.0f, ridgePoints[idx].height, 0.0f));
    }

    return getValleyProfile(shapeBetweenPeaks);
}

bool SatScanAnalysis::checkIntersection(const Segment2D& potentialProfile, const std::vector<MountainData>& mountainsToCheckAgainst)
{

    for(auto&& mountain : mountainsToCheckAgainst)
    {
        auto&& allRidgelines = mountain.getRidgeline();
        auto&& allRidgePoints = mountain.getMountainPts();

        for (auto&& ridgeToCheck : allRidgelines)
        {
            auto&& pts = ridgeToCheck.ridgePointsIndices;
            for (int i = 0; i < pts.size() - 1; ++i)
            {
                Segment2D otherSegment(allRidgePoints[pts[i]].pos, allRidgePoints[pts[i + 1]].pos);
                if (otherSegment.intersects(potentialProfile, false))
                    return false;
            }
        }
    }

    return true;
}

void SatScanAnalysis::analyzeOuterValleyAndSlopeProfile()
{
    // Mountain id in vMountains, all segment profiles
    std::unordered_map<int, std::vector<int>> valleyProfileMap;
    std::unordered_map<int, std::vector<int>> slopeProfileMap;

    for (int i = 0; i < vMountains.size() - 1; ++i)
    {
        auto&& fullRidgeline = vMountains[i].getRidgeline();
        auto&& allRidgePoints = vMountains[i].getMountainPts();

        float maxDistance = 0.0f;
        for (int i = 0; i < allRidgePoints.size() - 1; ++i)
            for (int j = i + 1; j < allRidgePoints.size(); ++j)
            {
                auto&& distance = allRidgePoints[i].pos.dist(allRidgePoints[j].pos);
                if (maxDistance < distance)
                    maxDistance = distance;
            }
        maxDistance /= 3.0f;

        for (int j = i + 1; j < vMountains.size(); ++j)
        {
            auto&& otherFullRidgeline = vMountains[j].getRidgeline();
            auto&& otherAllRidgePoints = vMountains[j].getMountainPts();

            for(auto&& ridge : fullRidgeline)
            {
                for (auto&& otherRidge : otherFullRidgeline)
                {
                    Segment2D potentialSegment(allRidgePoints[ridge.localPeakIdx].pos, otherAllRidgePoints[otherRidge.localPeakIdx].pos);
                    if (potentialSegment.length() <= maxDistance && checkIntersection(potentialSegment, vMountains))
                    {
                        if (auto&& shape = getDEMShapeBetweenPoints(potentialSegment.first, potentialSegment.second); shape)
                        {
                            int valleyProfile = static_cast<int>(getValleyProfile(*shape));
                            valleyProfileMap[i].emplace_back(valleyProfile);
                            valleyProfileMap[j].emplace_back(valleyProfile);

                            auto&& profilePair = analyzeSlopeProfilePair(*shape);
                            slopeProfileMap[i].emplace_back(static_cast<int>(profilePair.first));
                            slopeProfileMap[j].emplace_back(static_cast<int>(profilePair.second));
                        }
                    }
                }
            }
        }
    }

    for (auto&& [idx, profiles] : valleyProfileMap)
    {
        if (profiles.empty())
            continue;

        float roundedAverage = std::round(static_cast<float>(std::accumulate(profiles.begin(), profiles.end(), 0)) / static_cast<float>(profiles.size()));
        EValleyProfile outerValleyType = static_cast<EValleyProfile>(static_cast<int>(roundedAverage));
        vMountains[idx].setOuterValleyProfile(outerValleyType);
    }

    for (auto&& [idx, profiles] : slopeProfileMap)
    {
        if (profiles.empty())
            continue;

        float roundedAverage = std::round(static_cast<float>(std::accumulate(profiles.begin(), profiles.end(), 0)) / static_cast<float>(profiles.size()));
        ESlopeCurve slopeProfile = static_cast<ESlopeCurve>(static_cast<int>(roundedAverage));
        vMountains[idx].setSlopeProfile(slopeProfile);
    }
}

std::pair<ESlopeCurve, ESlopeCurve> SatScanAnalysis::analyzeSlopeProfilePair(const std::vector<QVector3D>& shape)
{
    int localMinimumIdx = 0;

    for (int i = 0; i < shape.size(); ++i)
        if (shape[localMinimumIdx].y() > shape[i].y())
            localMinimumIdx = i;

    std::vector<QVector3D> singleSlopeShape(localMinimumIdx + 1);
    auto iter = shape.begin();
    std::advance(iter, localMinimumIdx + 1);
    std::copy(shape.begin(), iter, singleSlopeShape.begin());

    ESlopeCurve firstProfile = analyzeSlopeProfile(singleSlopeShape);

    // Due to DEM inaccuracy it is possible for multiple points to have same value.
    // As such only the first point should be considered as foot of the slope
    for (int i = shape.size() - 1; i > localMinimumIdx; --i)
        if (shape[localMinimumIdx].y() == shape[i].y())
        {
            localMinimumIdx = i;
            continue;
        }

    singleSlopeShape.clear();
    iter = shape.begin();
    std::advance(iter, localMinimumIdx + 1);
    singleSlopeShape.resize(shape.size() - (localMinimumIdx));
    std::reverse_copy(--iter, shape.end(), singleSlopeShape.begin());

    ESlopeCurve secondProfile = analyzeSlopeProfile(singleSlopeShape);

    return { firstProfile, secondProfile };
}

ESlopeCurve SatScanAnalysis::analyzeSlopeProfile(const std::vector<QVector3D>& shape)
{
    float midHeight = (shape[0].y() + shape[shape.size() - 1].y()) / 2;
    int higherCount = 0;

    for (int i = 0; i < shape.size(); ++i)
        if (shape[i].y() >= midHeight)
            higherCount++;

    float percent = float(higherCount) / shape.size();
    ESlopeCurve profile = ESlopeCurve::Undefined;
    if (percent <= 0.3)
        profile = ESlopeCurve::Concave;
    else if (percent >= 0.6)
        profile = ESlopeCurve::Convex;
    else
        profile = ESlopeCurve::Normal;

    return profile;
}

void SatScanAnalysis::analyzeMainAndInnerValleyProfiles(MountainData* mountain)
{
    ////////////////////////////////////////////////
    // Ridgeline Valley Profile (Ridgeline passes - checked between main peak and next peaks along the main ridgeline)
    auto&& allRidgePoints = mountain->getMountainPts();
    auto&& peakPointsIndices = mountain->getPeakIndices();
    auto&& mainPeak = allRidgePoints[0];
    auto&& fullRidgeline = mountain->getRidgeline();

    auto points = mainPeak.children;

    EValleyProfile ridgelineValleyType = getMainRidgeProfile(*mountain);

    ////////////////////////////////////////////////
    // Inner Valley Profile (Valleys between ridges of mountain)
    std::vector<int> allProfiles;
    std::unordered_set<int> ridgesChecked;

    for (auto&& ridge : fullRidgeline)
    {
        if (ridge.rootPtIdx == 0)
            continue;

        auto&& ridgePoints = ridge.ridgePointsIndices;
        ridgesChecked.emplace(ridge.branchPtIdx);

        int highestPoint = ridge.localPeakIdx;

        for (auto&& otherRidge : fullRidgeline)
        {
            // Skip main ridges
            if (ridge.rootPtIdx == 0)
                continue;

            // Skip self
            if (ridge.branchPtIdx == otherRidge.branchPtIdx)
                continue;

            // Skip already checked ridges
            if(ridgesChecked.contains(otherRidge.branchPtIdx))
                continue;

            auto&& otherRidgePoints = otherRidge.ridgePointsIndices;
            int otherHighestPoint = otherRidge.localPeakIdx;

            Segment2D potentialProfile(allRidgePoints[highestPoint].pos, allRidgePoints[otherHighestPoint].pos);

            if (!checkIntersection(potentialProfile, { *mountain }))
                continue;

            if (auto&& shape = getDEMShapeBetweenPoints(allRidgePoints[highestPoint].pos, allRidgePoints[otherHighestPoint].pos); shape)
                allProfiles.emplace_back(static_cast<int>(getValleyProfile(*shape)));
        }
    }

    if (allProfiles.empty())
        return;

    float roundedAverage = std::round(static_cast<float>(std::accumulate(allProfiles.begin(), allProfiles.end(), 0)) / static_cast<float>(allProfiles.size()));
    EValleyProfile innerValleyType = static_cast<EValleyProfile>(static_cast<int>(roundedAverage));
    mountain->setInnerValleyProfile(innerValleyType);
}

void SatScanAnalysis::setDomainParameters(const RidgelineDomain& domainRequestData)
{
    if (auto&& domain = Generation::Data::get()->findDomainByGuid(domainRequestData.domainId); domain)
    {
        auto domainData = (*domain)->getData<EDomainType::Terrain>();
        auto&& landform = domainData->landform;
        domainData->landformVariation = domainData->getDefaultVariation(landform);

        // Landform Parameters
        domainData->landformInstanceParams = PLandformTypes[domainData->landformVariation];

        // TODO: Having no ridgelines (extreme case of plains) parameterize something
        if (vMountains.empty())
            return;

        auto&& pts = vMountains.front().getMountainPts();
        auto&& squares = (*domain)->getSquares();
        int ridgelineSquareCount = 0;
        for (auto&& pt : pts)
        {
            GPoint gPt = { static_cast<int>(std::floor((pt.pos.x * fScaleX + fOffsetX) / GRID_SEGMENT_WIDTH)), static_cast<int>(std::floor((pt.pos.z * fScaleZ + fOffsetZ) / GRID_SEGMENT_WIDTH)) };
            if (squares.contains(gPt))
                ridgelineSquareCount++;
        }

        float ridgelineAreaFactor = static_cast<float>(ridgelineSquareCount) / static_cast<float>(squares.size());

        // Ridge generation parameters
        auto&& ridgeline = vMountains.front().getRidgeline();
        int squareCount = squares.size();
        int ridgelineCount = ridgeline.size();

        if(ridgelineAreaFactor < 0.1f)
            domainData->ridgeGenParams.size = ERidgeSize::Small;
        else if (ridgelineAreaFactor < 0.25f)
            domainData->ridgeGenParams.size = ERidgeSize::Medium;
        else
            domainData->ridgeGenParams.size = ERidgeSize::Large;

        // TODO: although this is quite correct when tested, something more sophisticated would be desired
        if(ridgelineCount > 30)
            domainData->ridgeGenParams.complexityMain = ERidgeComplexity::PlentySubridges;
        else if (ridgelineCount < 10)
            domainData->ridgeGenParams.complexityMain = ERidgeComplexity::FewSubridges;
        else
            domainData->ridgeGenParams.complexityMain = ERidgeComplexity::SomeSubridges;

        // Valley angles
        auto getAnglePair = [](EValleyProfile profile) 
        {
            switch (profile)
            {
            case EValleyProfile::Sharp:
                return std::pair<float, float>(15.0f, 25.0f);
            case EValleyProfile::Normal:
                return std::pair<float, float>(7.0f, 15.0f);
            case EValleyProfile::Gentle:
                return std::pair<float, float>(4.0f, 7.0f);
            default:
                return std::pair<float, float>(4.0f, 25.0f);
            }
        };

//         IsohypseSlopeAngleInfo ridgelineValley;
//         EValleyProfile ridgelineProfile = vMountains.front().getRidgelineValleyProfile();
//         ridgelineValley.factorRange = getAnglePair(ridgelineProfile);
//         ridgelineValley.flatness = 0.9f;
//         domainData->landformInstanceParams->slopeAngleSameRidgesLevel0 = ridgelineValley;
// 
//         IsohypseSlopeAngleInfo innerValley;
//         EValleyProfile innerValleyProfile = vMountains.front().getInnerValleyProfile();
//         innerValley.factorRange = getAnglePair(innerValleyProfile);
//         innerValley.flatness = 0.9f;
//         domainData->landformInstanceParams->slopeAngleSameRidges = innerValley;
// 
//         IsohypseSlopeAngleInfo outerValley;
//         EValleyProfile outerValleyProfile = vMountains.front().getOuterValleyProfile();
//         outerValley.factorRange = getAnglePair(outerValleyProfile);
//         outerValley.flatness = 0.9f;
//         domainData->landformInstanceParams->slopeAngleDifferentRidges = outerValley;

        // Slope Angles
        auto&& slopeProfile = vMountains.front().getSlopeProfile();

        std::pair<float, float> curveRatio;
        std::pair<float, float> slopeDropAngle;

        if (slopeProfile == ESlopeCurve::Concave)
        {
            if (landform == ELandform::Mountains)
            {
                slopeDropAngle = { 10.0, 15.0 };
                curveRatio = { 0.15, 0.4 };
            }
            else if (landform == ELandform::Hills)
            {
                slopeDropAngle = { 5.0, 10.0 };
                curveRatio = { 0.15, 0.4 };
            }
        }
        else if (slopeProfile == ESlopeCurve::Convex)
        {
            if (landform == ELandform::Mountains)
            {
                slopeDropAngle = { 25.0, 35.0 };
                curveRatio = { 0.6, 0.85 };
            }
            else if (landform == ELandform::Hills)
            {
                slopeDropAngle = { 20.0, 30.0 };
                curveRatio = { 0.6, 0.85 };
            }
        }
        else
        {
            if (landform == ELandform::Mountains)
            {
                slopeDropAngle = { 15.0, 25.0 };
                curveRatio = { 0.45f, 0.55f };
            }
            else if (landform == ELandform::Hills)
            {
                slopeDropAngle = { 10.0, 15.0 };
                curveRatio = { 0.45f, 0.55f };
            }
        }

//         domainData->landformInstanceParams->IsohypseDropAngle = slopeDropAngle;
//         domainData->landformInstanceParams->IsohypseCurveRatio = curveRatio;

//         switch (domainRequestData.lf)
//         {
//         case EHammondLandforms::LowMountains:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.4f, 0.5f };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.2, 0.5 };
//             break;
//         case EHammondLandforms::HighMountains:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.5, 0.7 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 1.0, 1.0 };
//             domainData->landformInstanceParams->IsohypseDropAngle = { 25.0, 30.0 };
//             break;
//         case EHammondLandforms::OpenLowMountains:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.05, 0.10 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.3, 0.8 };
//             domainData->landformInstanceParams->IsohypseDropAngle = { 30.0, 40.0 };
//             outerValley.factorRange = {5, 10};
//             outerValley.flatness = 0.9f;
//             domainData->landformInstanceParams->slopeAngleDifferentRidges = outerValley;
//             innerValley.factorRange = {5, 10};
//             innerValley.flatness = 0.9f;
//             domainData->landformInstanceParams->slopeAngleSameRidges = innerValley;
//             ridgelineValley.factorRange = {3, 5};
//             ridgelineValley.flatness = 0.9f;
//             domainData->landformInstanceParams->slopeAngleSameRidgesLevel0 = ridgelineValley;
//             break;
//         case EHammondLandforms::OpenHighMountains:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.05, 0.1 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.5, 1.0 };
//             domainData->landformInstanceParams->IsohypseDropAngle = { 35.0, 40.0 };
//             break;
//         case EHammondLandforms::LowHills:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.6, 0.9 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.4, 0.8 };
//             break;
//         case EHammondLandforms::OpenLowHills:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.1, 0.2 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.6, 0.9 };
//             domainData->landformInstanceParams->IsohypseDropAngle = { 25.0, 30.0 };
//             break;
//         case EHammondLandforms::HighHills:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.8, 1.0 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.6, 1.0 };
//             break;
//         case EHammondLandforms::OpenHighHills:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.1, 0.15 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.6, 1.0 };
//             domainData->landformInstanceParams->IsohypseDropAngle = { 30.0, 35.0 };
//             break;
//         case EHammondLandforms::TablelandsWithHighRelief:
//             domainData->tableland = ETableLand::Plateau;
//             break;
//         case EHammondLandforms::TablelandsWithLowRelief:
//             domainData->tableland = ETableLand::Plateau;
//             break;
//         case EHammondLandforms::TablelandsWithOpenHighRelief:
//             domainData->tableland = ETableLand::Mesa;
//             break;
//         case EHammondLandforms::TablelandsWithOpenLowRelief:
//             domainData->tableland = ETableLand::Mesa;
//             break;
//         case EHammondLandforms::IrregularPlainsWithHills:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.3, 0.4 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.6, 1.0 };
//             domainData->landformInstanceParams->ridgeMaxTreeLevel = 2;
//             break;
//         case EHammondLandforms::IrregularPlains:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.2, 0.4 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.3, 0.8 };
//             break;
//         case EHammondLandforms::Plains:
//             domainData->landformInstanceParams->ridgeDensityPerSquare = { 0.1, 0.3 };
//             domainData->landformInstanceParams->ridgeAverageSize = { 0.3, 1.0 };
//             break;
//         default:
//             break;
//         }

//         QString debugTxt;
//         debugTxt.append("Density Per Square: " + toQString(domainData->landformInstanceParams->ridgeDensityPerSquare.first) + " - " + toQString(domainData->landformInstanceParams->ridgeDensityPerSquare.second) + '\n');
//         debugTxt.append("Average Size: " + toQString(domainData->landformInstanceParams->ridgeAverageSize.first) + " - " + toQString(domainData->landformInstanceParams->ridgeAverageSize.second) + '\n');
//         debugTxt.append("Drop angle: " + toQString(domainData->landformInstanceParams->IsohypseDropAngle.first) + " - " + toQString(domainData->landformInstanceParams->IsohypseDropAngle.second) + '\n');
//         debugTxt.append("Curve ratio: " + toQString(domainData->landformInstanceParams->IsohypseCurveRatio.first) + " - " + toQString(domainData->landformInstanceParams->IsohypseCurveRatio.second) + '\n');
// 
//         debugTxt.append("Main Ridge Valleys: " + toQString(domainData->landformInstanceParams->slopeAngleSameRidgesLevel0.factorRange.first) + " - " + toQString(domainData->landformInstanceParams->slopeAngleSameRidgesLevel0.factorRange.second) + '\n');
//         debugTxt.append("Inner Valleys: " + toQString(domainData->landformInstanceParams->slopeAngleSameRidges.factorRange.first) + " - " + toQString(domainData->landformInstanceParams->slopeAngleSameRidges.factorRange.second) + '\n');
//         debugTxt.append("Outer Valleys: " + toQString(domainData->landformInstanceParams->slopeAngleDifferentRidges.factorRange.first) + " - " + toQString(domainData->landformInstanceParams->slopeAngleDifferentRidges.factorRange.second) + '\n');
// 
//         debugTxt.append("Spread: " + toQString(domainData->ridgeGenParams.spread) + '\n');
//         debugTxt.append("Complexity: " + toQString(domainData->ridgeGenParams.complexity) + '\n');
//         debugTxt.append("Size: " + toQString(domainData->ridgeGenParams.size) + '\n');
//         OmniLog(ELoggingLevel::Info) <<= debugTxt;
    }
}