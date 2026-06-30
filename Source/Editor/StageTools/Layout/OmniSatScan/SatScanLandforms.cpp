#include "stdafx.h"
#include "SatScanLandforms.h"
#include "Omnigen.h"
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "OmniSatScan.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>

SatScanLandforms::SatScanLandforms(float demHeightFactor) : fHeightMultiplier(demHeightFactor)
{
    // Initial setup
    GDALDataset* dataset, *slopeData;
    GDALDatasetH hDataset, hOutData;
    GDALAllRegister();
    fillClassification();

    std::string input = "Output/Earth Engine/DEMRaster.tif";
    std::string output = "Output/Earth Engine/SlopeRaster.tif";
    std::string ihInput = "Output/Earth Engine/IHRaster.tif";

    GDALDriver* driverTiff;
    driverTiff = GetGDALDriverManager()->GetDriverByName("GTiff");

    dataset = static_cast<GDALDataset*>(GDALOpen(input.c_str(), GA_ReadOnly));
    dataset->GetRasterBand(1)->ComputeStatistics(false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    auto nCols = dataset->GetRasterBand(1)->GetXSize();
    auto nRows = dataset->GetRasterBand(1)->GetYSize();

    // Slope Raster
    hDataset = static_cast<GDALDatasetH>(dataset);

    const char* papszArgv[] = { "-s", "111120", "-p", nullptr };
    GDALDEMProcessingOptions* gdalOptions = GDALDEMProcessingOptionsNew(const_cast<char**>(papszArgv), nullptr);
    hOutData = GDALDEMProcessing(output.c_str(), hDataset, "slope", nullptr, gdalOptions, nullptr);
    GDALDEMProcessingOptionsFree(gdalOptions);

    // Slope Data
    slopeData = static_cast<GDALDataset*>(hOutData);

    // Buffers
    vMainRaster.resize(nCols * nRows);
    vSlopeRaster.resize(nCols * nRows);

    dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, nCols, nRows, vMainRaster.data(), nCols, nRows, GDT_Float32, 0, 0);
    slopeData->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, nCols, nRows, vSlopeRaster.data(), nCols, nRows, GDT_Float32, 0, 0);

    GDALClose(hOutData);
    GDALClose(dataset);
    GDALDestroyDriverManager();
}

EHammondLandforms SatScanLandforms::computeLandform(std::vector<int> rasterIndices)
{
    // Elevation change class
    auto rc = getReliefClass(rasterIndices);

    // Gentle Slope (Percent of Neighborhood Over Slope margin) 
    // and Percent of Neighborhood Over Gentle Slope margin with upland/lowlands distinction computations
    float upGentleCount = 0;
    float lowGentleCount = 0;
    float reliefThreshold = (fMaxHeight + fMinHeight) * 0.5;
    float noDataCounter = 0;

    float upperCount = 0;
    float extremeSlopes = 0;

    for (auto&& idx : rasterIndices)
    {
        if (vMainRaster[idx] >= reliefThreshold)
            upperCount++;

        if (vSlopeRaster[idx] == fNoData)
            noDataCounter++;

        if (vSlopeRaster[idx] >= 2.0)
            extremeSlopes++;

        // Gentle slope margin
        if (vSlopeRaster[idx] < 0.3)
        {
            if (vMainRaster[idx] >= reliefThreshold)
                upGentleCount++;
            else
                lowGentleCount++;
        }
    }

    // Percent of Neighborhood Over Gentle Slope margin with upland/lowlands distinction
    float upgPercent = upGentleCount / upperCount;
    float lowgPercent = lowGentleCount / ((rasterIndices.size() - noDataCounter) - upperCount);
    float extremePercent = extremeSlopes / (rasterIndices.size() - noDataCounter);

    auto pc = getProfileClass(upgPercent, lowgPercent, extremePercent);

    // Gentle Slope Class (Percent of Neighborhood Over Gentle Slope margin)
    float gsPercent = (upGentleCount + lowGentleCount) / (rasterIndices.size() - noDataCounter);
    auto glc = getGentleSlopeClass(gsPercent);

    // Final Landform
    int landformResult = int(rc) + int(pc) + int(glc);
    auto lf = getLandformType(landformResult);

    if (lf == EHammondLandforms::Undefined)
        OmniLog(ELoggingLevel::Info) << "Undefined lf with number: " <<= QString::number(landformResult);

    return lf;
}

bool SatScanDomainGeneration::createLandformDomains(std::vector<RidgelineDomain>* domainsRequested, const QSet<GPoint>& availableSquares, bool copyFromWorld)
{
    if (availableSquares.empty() || domainsRequested->empty())
        return false;

    float lfPer = 0.0f;
    for (auto&& per : *domainsRequested)
        lfPer += per.areaPercentage;

    Q_ASSERT(lfPer <= 1.0001);

    // TODO: temporary for ridge analysis visualization/debugging
    // Most likely ridgelines will be calculated first, and from them appropriate domains
    if (copyFromWorld)
    {
        auto&& layoutTools = getStageTools<EGenerationStage::Layout>();
        layoutTools->createDomainFromSquares(EDomainType::Terrain, availableSquares, false);
        auto&& domain = Generation::Data::get()->getAllDomains().back().second;
        emit Editable::aboutToBeModified(domain);

        auto domainData = domain->getData<EDomainType::Terrain>();
        domainData->landform = ELandform::Mountains;
        domainData->name = "Ridgeline Test Domain";
        emit Editable::modified(domain);
        return true;
    }

    int selectedGridCount = availableSquares.size();

    // Compute how many squares should be generated per landform, based on it's occurrence
    for (int i = 0; i < domainsRequested->size(); i++)
    {
        (*domainsRequested)[i].assignedSquareCount = (*domainsRequested)[i].areaPercentage * availableSquares.size();
        selectedGridCount = selectedGridCount - (*domainsRequested)[i].assignedSquareCount;
    }

    // Removes landforms with no squares assigned
    for (auto&& it = domainsRequested->begin(); it != domainsRequested->end();)
        if (it->assignedSquareCount == 0)
            it = domainsRequested->erase(it);
        else
            ++it;

    Q_ASSERT(selectedGridCount >= 0);

    // Assure all leftover squares are assigned
    if(selectedGridCount != 0)
    {
        // If more than 20% of available area is not assigned to a ridgeline, assume flat plains
        if (static_cast<float>(selectedGridCount) > static_cast<float>(availableSquares.size()) * 0.2)
        {
            RidgelineDomain plains = { .lf = EHammondLandforms::Plains, .assignedSquareCount = selectedGridCount };
            domainsRequested->emplace_back(plains);
        }
        // Leftovers are added to the largest domain
        else
        {
            auto largestDomain = domainsRequested->begin();

            for (auto it = domainsRequested->begin(); it != domainsRequested->end(); ++it)
                if (largestDomain->assignedSquareCount < it->assignedSquareCount)
                    largestDomain = it;

            largestDomain->assignedSquareCount += selectedGridCount;
        }
    }

    for (int i = 0; i < domainsRequested->size(); i++)
        OmniLog(ELoggingLevel::Info) << "Landform " << toQString((*domainsRequested)[i].lf) << " was assigned: " << toQString((*domainsRequested)[i].assignedSquareCount) <<= " squares.";

    OmniLog(ELoggingLevel::Info) << "All squares available: " <<= toQString(availableSquares.size());

    // Pre-generate check if landform have satisfactory sizes, if not downgrade landform type
    while(!assureSatisfiedLandformRestrictions(domainsRequested))

    std::vector<std::pair<EHammondLandforms, QSet<GPoint>>> domainsToGen;

    if (assignSquaresToDomains(*domainsRequested, availableSquares))
        generateDomainsFromResults(*domainsRequested);
    else
        return false;

    return true;
}

EHammondLandforms SatScanLandforms::getLandformType(int result)
{
    EHammondLandforms lf = EHammondLandforms::Undefined;
    for (auto&& it = mClassification.constBegin(); it != mClassification.constEnd(); ++it)
        if (it.value().find(result) != it.value().constEnd())
        {
            lf = it.key();
            break;
        }

    return lf;
}

void SatScanLandforms::fillClassification()
{
    mClassification[EHammondLandforms::LowMountains] = QSet({ 140, 240, 150, 152, 250 });
    mClassification[EHammondLandforms::HighMountains] = QSet({ 160, 162, 260 });
    mClassification[EHammondLandforms::OpenLowMountains] = QSet({ 251, 351, 451, 350, 450 });
    mClassification[EHammondLandforms::OpenHighMountains] = QSet({ 261, 360, 361, 460, 461 });

    mClassification[EHammondLandforms::LowHills] = QSet({ 220, 230, 320 });
    mClassification[EHammondLandforms::HighHills] = QSet({ 130, 240, 340 });
    mClassification[EHammondLandforms::OpenHighHills] = QSet({ 241, 341 });
    mClassification[EHammondLandforms::OpenLowHills] = QSet({ 221, 231, 321, 331 });

    mClassification[EHammondLandforms::TablelandsWithHighRelief] = QSet({ 152, 162, 252, 262 });
    mClassification[EHammondLandforms::TablelandsWithLowRelief] = QSet({ 132, 142, 232, 242 });
    mClassification[EHammondLandforms::TablelandsWithOpenHighRelief] = QSet({ 352, 362, 452, 462 });
    mClassification[EHammondLandforms::TablelandsWithOpenLowRelief] = QSet({ 332, 342, 432, 442 });

    mClassification[EHammondLandforms::IrregularPlainsWithHills] = QSet({ 330, 421, 431, 441 });
    mClassification[EHammondLandforms::IrregularPlains] = QSet({ 310, 312, 320, 322, 412, 420 });

    mClassification[EHammondLandforms::Plains] = QSet({ 410, 411 });
}

EProfileClass SatScanLandforms::getProfileClass(float upperPercent, float lowerPercent, float extremeSlopesPercent)
{
    // Percent of Neighborhood Over 8% Slope with upland/lowlands distinction
    EProfileClass pc = EProfileClass::Undefined;

    if ((upperPercent >= 0.2 && extremeSlopesPercent >= 0.05) || (extremeSlopesPercent >= upperPercent && extremeSlopesPercent > 0.1))
        pc = EProfileClass::PC2;
    else if (lowerPercent > 0.5)
        pc = EProfileClass::PC1;
    else
        pc = EProfileClass::PC0;

    return pc;
}

EGentleSlopeClass SatScanLandforms::getGentleSlopeClass(float slopePercent)
{
    // Gentle Slope Class (Percent of Neighborhood Over 8% Slope)
    EGentleSlopeClass glc = EGentleSlopeClass::Undefined;

    if (1 - slopePercent > 0.8)        glc = EGentleSlopeClass::GS100;
    else if (1 - slopePercent > 0.60)   glc = EGentleSlopeClass::GS200;
    else if (1 - slopePercent > 0.3)   glc = EGentleSlopeClass::GS300;
    else                            glc = EGentleSlopeClass::GS400;

    return glc;
}

bool SatScanDomainGeneration::assureSatisfiedLandformRestrictions(std::vector<RidgelineDomain>* domainsRequested, bool downgrade)
{
    for (int i = 0; i < domainsRequested->size(); i++)
    {
        ELandform lfType;
        int lfIdx = static_cast<int>((*domainsRequested)[i].lf);
        if (lfIdx <= static_cast<int>(EHammondLandforms::OpenHighMountains))
            lfType = ELandform::Mountains;
        else if (lfIdx <= static_cast<int>(EHammondLandforms::OpenHighHills))
            lfType = ELandform::Hills;
        else if (lfIdx <= static_cast<int>(EHammondLandforms::TablelandsWithOpenLowRelief))
            lfType = ELandform::Tablelands;
        else
            continue;

        // Check if each occurrence satisfies type size limitation
        // TODO:: redo this to the new landform variations
        if ((*domainsRequested)[i].assignedSquareCount < 1)
        {
            if (!downgrade)
                return false;

            std::pair<EHammondLandforms, int> largestType({ EHammondLandforms::Undefined, 0 });

            // Downgrade landform
            EHammondLandforms typeToDowngradeTo;

            if (lfType == ELandform::Mountains)
                typeToDowngradeTo = EHammondLandforms::HighHills;
            else
                typeToDowngradeTo = EHammondLandforms::IrregularPlainsWithHills;

            QString lfName = toQString((*domainsRequested)[i].lf);

            // Append to existing type, or create a new type
            (*domainsRequested)[i].lf = typeToDowngradeTo;

            OmniLog(ELoggingLevel::Warn) << lfName << " area was too small and was downgraded to " <<= toQString(typeToDowngradeTo);
            return false;
        }
    }

    return true;
}

EReliefClass SatScanLandforms::getReliefClass(const std::vector<int>& AreaIndices)
{
    // Relief Class Computations (Elevation change within a NAW)
    fMinHeight = 9999;
    fMaxHeight = fNoData;

    for (auto&& idx : AreaIndices)
    {
        if (vMainRaster[idx] > fMaxHeight)
            fMaxHeight = vMainRaster[idx];

        if ((vMainRaster[idx] < fMinHeight) && vMainRaster[idx] >= 0)
            fMinHeight = vMainRaster[idx];
    }

    EReliefClass rc = EReliefClass::Undefined;
    float ec = (fMaxHeight - fMinHeight) * fHeightMultiplier;

    if (ec > 900)       rc = EReliefClass::RC60;
    else if (ec > 301)  rc = EReliefClass::RC50;
    else if (ec > 151)  rc = EReliefClass::RC40;
    else if (ec > 91)   rc = EReliefClass::RC30;
    else if (ec > 31)   rc = EReliefClass::RC20;
    else                rc = EReliefClass::RC10;

    return rc;
}

void SatScanDomainGeneration::generateDomainsFromResults(const std::vector<RidgelineDomain>& domainsRequested)
{
    auto&& layoutTools = getStageTools<EGenerationStage::Layout>();

    for (int i = 0; i < domainsRequested.size(); i++)
    {
        if (domainsRequested[i].squares.empty())
            continue;

        // Create domain
        layoutTools->createDomainFromSquares(EDomainType::Terrain, domainsRequested[i].squares, false);

        domainsRequested[i].squares.clear();

        // Name setting - temporary for debug purposes, will be replaced with a proper setData func in LayoutStage
        auto&& domain = Generation::Data::get()->getAllDomains().back().second;
        emit Editable::aboutToBeModified(domain);

        ELandform lf = ELandform::Plains;
        float height = 0;
        float heightFactor = 1.0f;

        // Very temporary parameters (many landforms might be merged, and ELandform types might get stricter limitations etc)
        switch (domainsRequested[i].lf)
        {
            using enum EHammondLandforms;
        case HighMountains: case OpenHighMountains:
            lf = ELandform::Mountains; heightFactor = 1.5;  break;
        case OpenLowMountains: case LowMountains:
            lf = ELandform::Mountains; heightFactor = 1;  break;
        case HighHills: case OpenHighHills:
            lf = ELandform::Hills; heightFactor = 2; break;
        case LowHills: case OpenLowHills:
            lf = ELandform::Hills; heightFactor = 1; break;
        case TablelandsWithHighRelief: case TablelandsWithOpenHighRelief:
            lf = ELandform::Tablelands; heightFactor = 2.0; break;
        case TablelandsWithLowRelief: case TablelandsWithOpenLowRelief:
            lf = ELandform::Tablelands; heightFactor = 1; break;
        case IrregularPlainsWithHills:
            lf = ELandform::RuggedPlains; heightFactor = 4; break;
        case IrregularPlains:
            lf = ELandform::RuggedPlains; heightFactor = 2; break;
        }

        height = Landform::computeMaxRidgeHeight(lf, domain->getSquares().size()) * heightFactor * 100.0f;

        // Modify domain in accordance with the guidelines
        auto domainData = domain->getData<EDomainType::Terrain>();
        domainData->landform = lf;
        domainData->name = toQString(domainsRequested[i].lf);
        domainData->maxHeight = height;

        emit Editable::modified(domain);
        domainsRequested[i].domainId = domain->getGuid();
    }
}

bool SatScanDomainGeneration::assignSquaresToDomains(const std::vector<RidgelineDomain>& domainsRequested, const QSet<GPoint>& availableSqrs)
{
    std::vector<QSet<GPoint>> unclaimedAreaMap = {availableSqrs};

    // <Domain index, assigned area index>
    std::unordered_map<int, int> domainsAssignedToArea;
    for (int i = 0; i < domainsRequested.size(); i++)
    {
        int retriesDueToAreaSplitting = 0;

    restartLast:
        if (domainsRequested[i].assignedSquareCount == 0)
            continue;

        GPoint rootSquare;
        int currentClaimMapIndex = -1;

        // Domains might be assigned to specific areas to ensure proper filling
        if(domainsAssignedToArea.empty())
            currentClaimMapIndex = 0;
        else
            currentClaimMapIndex = domainsAssignedToArea[i];

        rootSquare = getRandomPointFromArea(unclaimedAreaMap[currentClaimMapIndex]);
        // Backup in case of restarting this assignment attempt
        auto unclaimedAreaBackup = unclaimedAreaMap[currentClaimMapIndex];

        if (auto&& squares = assignSquaresForDomain(rootSquare, domainsRequested[i].assignedSquareCount, &unclaimedAreaBackup); squares)
        {
            domainsRequested[i].squares = *squares;

            if (i == domainsRequested.size() - 1)
                continue;

            auto remainingAreas = getRemainingContinuousAreas(unclaimedAreaBackup);
            // If current area was not changed, save assignments and continue
            if (remainingAreas.size() == 1)
            {
                unclaimedAreaMap[currentClaimMapIndex] = unclaimedAreaBackup;
                continue;
            }

            // <Domain index, square count>
            std::vector<std::pair<int, int>> remainingAssignedDomains;
            // Gather all domains that are yet to be fitted, and are assigned to area
            if (domainsAssignedToArea.empty())
                for (int j = i + 1; j < domainsRequested.size(); j++)
                    remainingAssignedDomains.emplace_back(j, domainsRequested[j].assignedSquareCount);
            else
                for (int j = i + 1; j < domainsRequested.size(); j++)
                    if(domainsAssignedToArea[j] == currentClaimMapIndex)
                        remainingAssignedDomains.emplace_back(j, domainsRequested[j].assignedSquareCount);

            if (remainingAreas.size() > remainingAssignedDomains.size())
            {
                // TODO: detect this better (this is due to the area being too constrained/'branched' - 
                // as in a situation where laying a domain must cause the area to split, when not enough domains exist to fill the splits
                retriesDueToAreaSplitting++;
                if(retriesDueToAreaSplitting > 4)
                {
                    std::vector<int> domainsToMerge = {i};
                    for (auto&& domain : remainingAssignedDomains)
                        domainsToMerge.emplace_back(domain.first);

                    retriesDueToAreaSplitting = 0;
                    mergeDomains(domainsRequested, domainsToMerge);
                }

                goto restartLast;
            }

            // If current area was split, check if remaining and assigned domains would fit in such a configuration
             if(auto&& newAssignements = checkIfDomainsFitInArea(remainingAssignedDomains, remainingAreas); newAssignements)
             {
                 unclaimedAreaMap[currentClaimMapIndex] = remainingAreas[0];

                 for (int j = 1; j < remainingAreas.size(); j++)
                     unclaimedAreaMap.emplace_back(remainingAreas[j]);

                 for (auto&& assignementPair : *newAssignements)
                     if (assignementPair.second == 0)
                         domainsAssignedToArea[assignementPair.first] = currentClaimMapIndex;
                     else
                         domainsAssignedToArea[assignementPair.first] = unclaimedAreaMap.size() - remainingAreas.size() + assignementPair.second;

                 continue;
             }

             // Failed to fit remaining domains in areas
             goto restartLast;
        }
        // Failed to generate all squares for domain
        else
            goto restartLast;
    }

    return true;
}

GPoint SatScanDomainGeneration::getRandomPointFromArea(const QSet<GPoint>& area)
{
    auto&& randomIterator = area.begin();
    std::uniform_int_distribution<> distr(0, area.size() - 1);
    int randomRoot = distr(Generation::gRandomEngine);

    std::advance(randomIterator, randomRoot);

    return *randomIterator;
}

std::optional<QSet<GPoint>> SatScanDomainGeneration::assignSquaresForDomain(const GPoint& rootSquare, int domainSquares, QSet<GPoint>* availableSquares)
{
    QSet<GPoint> finalSquares, borderSquares;
    borderSquares.insert(rootSquare);
    finalSquares.insert(rootSquare);
    int sqrsLeft = domainSquares - 1;
    availableSquares->remove(rootSquare);

    while(sqrsLeft > 0)
    {
        if (auto&& tempSquares = expandAreaBorder(borderSquares, availableSquares, &sqrsLeft); tempSquares)
        {
            borderSquares = *tempSquares;
            finalSquares.unite(borderSquares);
        }
        // If a domain fails to expand to its desired size restart the generation
        else
            return {};
    }

    Q_ASSERT(finalSquares.size() == domainSquares);

    return finalSquares;
}

std::optional<QSet<GPoint>> SatScanDomainGeneration::expandAreaBorder(const QSet<GPoint>& borderSquares, QSet<GPoint>* availableSquares, int* domainSquaresLeft)
{
    static const std::array<std::pair<int, int>, 4> sqrJumps({ std::pair(-1, 0), std::pair(0, -1), std::pair(1, 0), std::pair(0, 1) });

    if (borderSquares.empty())
        return {};

    QSet<GPoint> newSeedSquares = borderSquares;

    for (auto&& root : borderSquares)
    {
        std::vector<int> directionChance;
        for (int i = 0; i < sqrJumps.size(); i++)
        {
            GPoint newSqr(root.x + sqrJumps[i].first, root.z + sqrJumps[i].second);
            if (availableSquares->contains(newSqr))
                directionChance.emplace_back(i);
        }

        // Remove all non eligible to expand old points from being the new root points
        if (directionChance.empty())
        {
            newSeedSquares.remove(root);
            continue;
        }

        // For each available direction to expand for a given point, find and prefer directions toward other borders
        auto availableDirections = directionChance;
        for (auto&& dir : availableDirections)
        {
            GPoint firstTest(root.x + (sqrJumps[dir].first * 2), root.z + (sqrJumps[dir].second * 2));
            if (!availableSquares->contains(firstTest))
            {
                for (int j = 0; j < 3; j++)
                    directionChance.emplace_back(dir);

                continue;
            }

            GPoint secondTest(root.x + (sqrJumps[dir].first * 3), root.z + (sqrJumps[dir].second * 3));
            if (!availableSquares->contains(secondTest))
                for (int j = 0; j < 2; j++)
                    directionChance.emplace_back(dir);
        }

        (*domainSquaresLeft)--;
        std::uniform_int_distribution<> distr(0, directionChance.size() - 1);
        int dir = directionChance[distr(Generation::gRandomEngine)];

        // New square
        GPoint newSqr(root.x + sqrJumps[dir].first, root.z + sqrJumps[dir].second);
        availableSquares->remove(newSqr);
        newSeedSquares.insert(newSqr);

        if (*domainSquaresLeft == 0)
            return newSeedSquares;
    }

    if (newSeedSquares.isEmpty())
        return {};

    return newSeedSquares;
}

std::vector<QSet<GPoint>> SatScanDomainGeneration::getRemainingContinuousAreas(const QSet<GPoint>& allRemainingSquares)
{
    QSet<GPoint> remainingSquares = allRemainingSquares;

    auto getNeighborSquares = [&remainingSquares](const QSet<GPoint>& rootSquares) -> std::optional<QSet<GPoint>>
    {
        static const std::array<std::pair<int, int>, 4> directions({ std::pair(-1, 0), std::pair(0, -1), std::pair(1, 0), std::pair(0, 1) });
        QSet<GPoint> neighborSquares;
        for(auto&& root : rootSquares)
        {
            for (auto&& direction : directions)
            {
                GPoint newPoint = { root.x + direction.first, root.z + direction.second };
                if (remainingSquares.contains(newPoint))
                    neighborSquares.insert(newPoint);
            }
        }

        if (neighborSquares.empty())
            return {};

        return neighborSquares;
    };

    std::vector<QSet<GPoint>> continuousAreas;
    while (true)
    {
        // Take any square as root of an unclaimed area
        GPoint areaRootPoint = *remainingSquares.begin();
        continuousAreas.emplace_back(QSet<GPoint>({ areaRootPoint }));
        remainingSquares.remove(areaRootPoint);

        // Find the neighboring squares of the root one, add them to the unclaimed area, and continue searching with the border points as the new search root points
        QSet<GPoint> borderSquares = { areaRootPoint };
        while(true)
        {
            if (auto newSquares = getNeighborSquares(borderSquares); newSquares)
            {
                remainingSquares.subtract(*newSquares);
                continuousAreas.back().unite(*newSquares);
                borderSquares = (*newSquares);
            }
            // If no new border points can be found, break the search
            else
                break;
        }

        if (remainingSquares.isEmpty())
            return continuousAreas;
    }
}

std::optional<std::unordered_map<int, int>> SatScanDomainGeneration::checkIfDomainsFitInArea(const std::vector<std::pair<int, int>>& remainingDomains, const std::vector<QSet<GPoint>>& unclaimedAreas)
{
    std::unordered_map<int, int> newDomainAssignements;

    std::vector<int> areas(unclaimedAreas.size());
    for (int i = 0; i < unclaimedAreas.size(); i++)
        areas[i] = unclaimedAreas[i].size();

    std::vector<int> indicesClaimed;
    std::vector<std::vector<int>> allCombinations;

    // TODO: filter only domains that can fit in targeted area 
    // (keep in mind that each area must have at least 1 domain assigned, while filtering might cause (but doesn't have to) all combinations to be valid)

    // Get all possible domain combinations (minus at least one domain per remaining areas)
    for (int tempVectorSize = 1; tempVectorSize < remainingDomains.size() - (unclaimedAreas.size() - 1); tempVectorSize++)
    {
        std::vector<int> combinationIndices(tempVectorSize);
        getAllPossibleCombinations(&combinationIndices, &allCombinations, indicesClaimed, 0, remainingDomains.size() - 1, 0, tempVectorSize);
    }

    // Sum up square count for every domain combination
    std::vector<int> allSums(allCombinations.size());
    for (int i = 0; i < allCombinations.size(); i++)
        for (auto&& index : allCombinations[i])
            allSums[i] += remainingDomains[index].second;

    for (int i = 0; i < allCombinations.size(); i++)
    {
        if (allSums[i] == areas[0])
        {
            if (areas.size() > 2)
            {
                indicesClaimed = allCombinations[i];
                if (!checkNextArea(&newDomainAssignements, indicesClaimed, remainingDomains, 1, areas))
                    continue;
            }

            for (auto&& index : allCombinations[i])
                newDomainAssignements.emplace(remainingDomains[index].first, 0);

            // Assign all leftover domains to final area
            for (auto&& domain : remainingDomains)
                if (!newDomainAssignements.contains(domain.first))
                {
                    int lastAreaIdx = areas.size() - 1;
                    newDomainAssignements.emplace(domain.first, lastAreaIdx);
                }

            return newDomainAssignements;
        }
    }

    return {};
}

void SatScanDomainGeneration::getAllPossibleCombinations(std::vector<int>* combinationIndices, std::vector<std::vector<int>>* allCombinations, const std::vector<int>& claimedIndices, int startIdx, int endIdx, int currentIdx, int combinationSize)
{
    // When reaching the desired combination size, save the temporary vector in `allCombinations`
    if (currentIdx == combinationSize)
    {
        (*allCombinations).emplace_back(*combinationIndices);
        return;
    }

    for (int i = startIdx; i <= endIdx && endIdx - i + 1 >= combinationSize - currentIdx; i++)
    {
        if(!claimedIndices.empty())
            if (auto result = std::find(claimedIndices.begin(), claimedIndices.end(), i); result != claimedIndices.end())
                continue;

        (*combinationIndices)[currentIdx] = i;
        getAllPossibleCombinations(combinationIndices, allCombinations, claimedIndices, i + 1, endIdx, currentIdx + 1, combinationSize);
    }
}

bool SatScanDomainGeneration::checkNextArea(std::unordered_map<int, int>* newDomainAssignements, const std::vector<int>& indicesClaimed, const std::vector<std::pair<int, int>>& remainingDomains, int areasFilled, const std::vector<int>& areasToFill)
{
    std::vector<std::vector<int>> nextAreaCombinations;

    // Get all possible domain combination (minus the ones already claimed by a different area and minus at least one for each remaining area)
    for (int tempVectorSize = 1; tempVectorSize < remainingDomains.size() - indicesClaimed.size() - (areasToFill.size() - areasFilled - 1); tempVectorSize++)
    {
        std::vector<int> combinationIndices(tempVectorSize);
        getAllPossibleCombinations(&combinationIndices, &nextAreaCombinations, indicesClaimed, 0, remainingDomains.size() - 1, 0, tempVectorSize);
    }

    // Sum up square count for every domain combination
    std::vector<int> nextAreaSums(nextAreaCombinations.size());
    for (int i = 0; i < nextAreaCombinations.size(); i++)
        for (auto&& index : nextAreaCombinations[i])
            nextAreaSums[i] += remainingDomains[index].second;

    for (int i = 0; i < nextAreaCombinations.size(); i++)
    {
        if (nextAreaSums[i] == areasToFill[areasFilled])
        {
            // Last area does not need to be checked, if all previous were filled
            if (areasToFill.size() == areasFilled + 2)
            {
                for (auto&& index : nextAreaCombinations[i])
                    newDomainAssignements->emplace(remainingDomains[index].first, areasFilled);

                return true;
            }
            else
            {
                auto newIndicesClaimed = indicesClaimed;
                newIndicesClaimed.insert(newIndicesClaimed.end(), nextAreaCombinations[i].begin(), nextAreaCombinations[i].end());
                if (checkNextArea(newDomainAssignements, newIndicesClaimed, remainingDomains, areasFilled + 1, areasToFill))
                {
                    for (auto&& index : nextAreaCombinations[i])
                        newDomainAssignements->emplace(remainingDomains[index].first, areasFilled);

                    return true;
                }
            }
        }
    }

    return false;
}

void SatScanDomainGeneration::mergeDomains(const std::vector<RidgelineDomain>& domainsRequested, const std::vector<int> domainsToMerge)
{
    std::map<int, int, std::greater<int>> domainSizeMap;
    int squareSum = 0;

    for (auto&& domainIdx : domainsToMerge)
    {
        int squaresAssigned = domainsRequested[domainIdx].assignedSquareCount;
        squareSum += squaresAssigned;
        domainSizeMap.emplace(squaresAssigned, domainIdx);
    }

    for (auto&& domainIdx : domainsToMerge)
    {
        if (domainIdx == domainSizeMap.begin()->second)
            domainsRequested[domainIdx].assignedSquareCount = squareSum;
        else
            domainsRequested[domainIdx].assignedSquareCount = 0;
    }
}
