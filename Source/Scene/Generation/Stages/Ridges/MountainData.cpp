#include "stdafx.h"
#include "MountainData.h"
#include <ranges>

#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"

MountainData::MountainData(const RidgePoint& peakPoint)
{
    vMountainPts.emplace_back(peakPoint);
}

void MountainData::computeRidgeline()
{
    if (!vRidgeline.empty())
        return;

    auto&& ridgeline = computeShortRidgeline();
    auto endPts = *findEndPoints(0, ridgeline);

    while (true)
    {
        std::vector<int> nextPts;
        for (auto&& pt : endPts)
        {
            if(auto newEndPts = findEndPoints(pt, ridgeline); newEndPts)
                nextPts.insert(nextPts.end(),
                    std::make_move_iterator(newEndPts->begin()),
                    std::make_move_iterator(newEndPts->end()));
        }

        if (nextPts.empty())
            break;

        endPts.clear();
        endPts.insert(endPts.end(),
            std::make_move_iterator(nextPts.begin()),
            std::make_move_iterator(nextPts.end()));
    }
}

void MountainData::drawRidgesInDomain(qint64 domainGuid, int nCols, int nRows, const QSet<GPoint>& availableSquares)
{
    if (vRidgeline.empty())
        computeRidgeline();

    auto&& genData = Generation::Data::get();
    auto&& domain = genData->findDomainByGuid(domainGuid);
    if (!domain)
    {
        OmniLog(ELoggingLevel::Warn) << "No Domain found with guid " << QString::number(domainGuid) <<= " aborting generation.";
        return;
    }

    float xOffset, zOffset, scaleX, scaleZ;
    GPoint bottomRight = { 0,0 };
    GPoint topLeft{ GRID_SEGMENT_COUNT, GRID_SEGMENT_COUNT };

    for (auto&& square : availableSquares)
    {
        if (square.x <= topLeft.x && square.z <= topLeft.z)
            topLeft = square;

        if (square.x >= bottomRight.x && square.z >= bottomRight.z)
            bottomRight = square;
    }

    scaleX = ((bottomRight.x - topLeft.x) * GRID_SEGMENT_WIDTH + GRID_SEGMENT_WIDTH) / static_cast<float>(nCols);
    scaleZ = ((bottomRight.z - topLeft.z) * GRID_SEGMENT_WIDTH + GRID_SEGMENT_WIDTH) / static_cast<float>(nRows);
    xOffset = topLeft.x * GRID_SEGMENT_WIDTH;
    zOffset = topLeft.z * GRID_SEGMENT_WIDTH;

    auto&& mainRidges = vRidgeline | std::views::filter([](const auto& ele) {return ele.ridgeClass == 0; });
    std::vector<QVector3D> mainRidge;
    std::vector<int> children;
    std::vector<int> ridgePoints;

    // Main ridge points (which might consist of two lines both ending at point 0, thus requiring special handling)
    for (auto&& rid : mainRidges)
    {
        std::vector<QVector3D> tempVec;

        int idx = rid.endPtIdx;
        while (rid.rootPtIdx <= idx)
        {
            tempVec.emplace_back(vMountainPts[idx].pos.x * scaleX + xOffset, vMountainPts[idx].height, vMountainPts[idx].pos.z * scaleZ + zOffset);
            ridgePoints.emplace_back(idx);
            if (idx != 0 && vMountainPts[idx].children.size() > 1)
                for (auto&& child : vMountainPts[idx].children)
                    if (auto&& ridgeRes = std::ranges::find_if(ridgePoints, [&child](const auto& ele) {return ele == child; }); ridgeRes == ridgePoints.end())
                        children.emplace_back(child);

            idx = vMountainPts[idx].parent;
        }

        // Add first ridge line to marker points
        if (mainRidge.empty())
            mainRidge.insert(mainRidge.end(),
                std::make_move_iterator(tempVec.begin()),
                std::make_move_iterator(tempVec.end()));
        // Add second ridge line to marker points (needs reversing)
        else
            mainRidge.insert(mainRidge.end(),
                std::make_move_iterator(++tempVec.rbegin()),
                std::make_move_iterator(tempVec.rend()));
    }

    // Children of mountain root [0] must be handled after the full main ridge is computed
    for(auto&& childIdx : vMountainPts[0].children)
        if (auto&& res = std::ranges::find_if(ridgePoints, [&childIdx](const auto& ele) {return ele == childIdx; }); res == ridgePoints.end())
            children.emplace_back(childIdx);

    Q_ASSERT(mainRidge.size() >= 2);

    auto mainRidgeMarker = genData->createMarker<DRidgeMarker, true>(mainRidge, nullptr);

    // Subridges
    if (!children.empty())
    {
        std::vector<std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>> nextRidges = { std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>(children, mainRidgeMarker) };
        while (true)
        {
            std::vector<std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>> newRootPts;
            for (auto&& ele : nextRidges)
                if (auto&& newRoots = makeRidgeMarker(ele.first, ele.second, {scaleX, xOffset, scaleZ, zOffset}); newRoots)
                    newRootPts.insert(newRootPts.end(),
                        std::make_move_iterator((*newRoots).begin()),
                        std::make_move_iterator((*newRoots).end()));

            if (newRootPts.empty())
                break;

            nextRidges.clear();
            nextRidges.insert(nextRidges.end(),
                std::make_move_iterator(newRootPts.begin()),
                std::make_move_iterator(newRootPts.end()));
        }
    }
}

RidgeData MountainData::computeShortRidge(int branchRootIdx)
{
    RidgeData ridge;
    ridge.rootPtIdx = vMountainPts[branchRootIdx].parent;
    ridge.branchPtIdx = branchRootIdx;
    int idx = branchRootIdx;
    std::vector<float> heightVec = { vMountainPts[idx].height , vMountainPts[ridge.rootPtIdx].height };
    float length = vMountainPts[idx].pos.dist(vMountainPts[ridge.rootPtIdx].pos);
    std::vector<int> points = { ridge.rootPtIdx, branchRootIdx};
    std::vector<int> peaks;

    while (vMountainPts[idx].children.size() == 1)
    {
        auto&& oldPos = vMountainPts[idx].pos;
        idx = vMountainPts[idx].children.front();
        heightVec.emplace_back(vMountainPts[idx].height);
        ridge.length += oldPos.dist(vMountainPts[idx].pos);
        points.emplace_back(idx);
    }

    points.emplace_back(idx);
    ridge.endPtIdx = idx;
    ridge.averageHeight = std::accumulate(heightVec.begin(), heightVec.end(), 0.0f) / heightVec.size();
    ridge.ridgePointsIndices = points;

    int highestPoint = ridge.ridgePointsIndices[0];
    for (auto&& point : ridge.ridgePointsIndices)
        if (vMountainPts[highestPoint].height < vMountainPts[point].height)
            highestPoint = point;

    ridge.localPeakIdx = highestPoint;

    return ridge;
}

std::vector<RidgeData> MountainData::computeShortRidgeline()
{
    std::vector<RidgeData> ridgeline;
    for (auto&& childIdx : vMountainPts[0].children)
    {
        auto&& ridge = computeShortRidge(childIdx);
        ridgeline.emplace_back(ridge);
        std::vector<int> ridgeRoots = vMountainPts[ridge.endPtIdx].children;

        while(true)
        {
            // Out of all root points given, compute short ridges (line till the first ridge branch)
            std::vector<int> newRidgeRoots;
            for (auto&& idx : ridgeRoots)
            {
                auto&& rg = computeShortRidge(idx);
                ridgeline.emplace_back(rg);
                if (auto&& children = vMountainPts[rg.endPtIdx].children; !children.empty())
                {
                    // If a ridge ends by branching save them as root points for next search
                    newRidgeRoots.insert(newRidgeRoots.end(),
                        std::make_move_iterator(children.begin()),
                        std::make_move_iterator(children.end()));
                }
            }

            // If no new ridge roots were found, finish ridgeline computations
            if (newRidgeRoots.empty())
                break;

            ridgeRoots.clear();
            ridgeRoots.insert(ridgeRoots.end(),
                std::make_move_iterator(newRidgeRoots.begin()),
                std::make_move_iterator(newRidgeRoots.end()));
        }
    }

    return ridgeline;
}

std::optional<std::vector<int>> MountainData::findEndPoints(int rootPointIdx, const std::vector<RidgeData>& ridgeline)
{
    auto&& ridges = ridgeline | std::views::filter([&rootPointIdx](const auto& ele) {return ele.rootPtIdx == rootPointIdx; });
    if (ridges.empty())
        return {};

    auto&& parentSearchResult = std::ranges::find_if(vRidgeline, [&rootPointIdx](const auto& ele) {return ele.endPtIdx == rootPointIdx; });
    std::vector<int> endPoints;

    // Main Ridges
    if (parentSearchResult == vRidgeline.end())
    {
        // TODO: might require height comparison and/or angle check between ridges
        // Pick the two ridges with highest average height as main ridges
        std::multimap<float, RidgeData> heightMap;
        for (auto&& ridge : ridges)
        {
            endPoints.emplace_back(ridge.endPtIdx);
            heightMap.emplace(ridge.averageHeight, ridge);
        }

        int mainRidgeCount = 0;

        for (auto&& it = heightMap.rbegin(); it != heightMap.rend(); ++it)
        {
            if (mainRidgeCount < 2)
                it->second.ridgeClass = 0;
            else
                it->second.ridgeClass = 1;

            vRidgeline.emplace_back(it->second);
            mainRidgeCount++;
        }

        return endPoints;
    }

    RidgeData parentRidge = *parentSearchResult;

    // Save ridges that have a similar enough height as their parent ridge, along with info if the branch at the end
    std::vector<std::pair<bool, RidgeData>> possibleMerges;

    for (auto&& ridge : ridges)
    {
        ridge.ridgeClass = parentRidge.ridgeClass + 1;
        endPoints.emplace_back(ridge.endPtIdx);
        if (parentRidge.averageHeight > (ridge.averageHeight * 1.1))
        {
            vRidgeline.emplace_back(ridge);
            continue;
        }

        bool branched = false;

        auto&& branch = std::ranges::find_if(ridgeline, [&ridge](const auto& ele) {return ele.rootPtIdx == ridge.endPtIdx; });
        if (branch != ridgeline.end())
            branched = true;

        possibleMerges.emplace_back(branched, ridge);
    }

    if (possibleMerges.size() == 1)
    {
        mergeRidgeWithParent(possibleMerges.front().second);
    }
    else if (!possibleMerges.empty())
    {
        // If only one subridge is branched, merge it, add rest as subridges
        auto results = possibleMerges | std::views::filter([](const auto& ele) {return ele.first == true; });
        if (!results.empty() && ++results.begin() == results.end())
        {
            mergeRidgeWithParent(results.front().second);
            // Add other ridges as subridges
            for (auto&& pair : possibleMerges)
                if (pair.first != true)
                    vRidgeline.emplace_back(pair.second);
        }
        else
        {
            int longestRidgeIdx = 0;

            if (!results.empty())
            {
                // If more than one ridge branches select the longest from them
                for (int i = 0; i < possibleMerges.size(); i++)
                {
                    if (possibleMerges[i].first == true && possibleMerges[i].second.length > possibleMerges[longestRidgeIdx].second.length)
                        longestRidgeIdx = i;
                }
            }
            else
            {
                // If no ridges branch, select the longest from all
                for (int i = 0; i < possibleMerges.size(); i++)
                {
                    if (possibleMerges[i].second.length > possibleMerges[longestRidgeIdx].second.length)
                        longestRidgeIdx = i;
                }
            }

            mergeRidgeWithParent(possibleMerges[longestRidgeIdx].second);
            // Add other ridges as subridges
            for (int i = 0; i < possibleMerges.size(); i++)
                if (i != longestRidgeIdx)
                    vRidgeline.emplace_back(possibleMerges[i].second);
        }
    }

    return endPoints;
}

void MountainData::mergeRidgeWithParent(const RidgeData& ridge)
{
    auto&& parentRidge = std::ranges::find_if(vRidgeline, [&ridge](const auto& ele) {return ele.endPtIdx == ridge.rootPtIdx; });
    Q_ASSERT(parentRidge != vRidgeline.end());

    parentRidge->ridgePointsIndices.insert(parentRidge->ridgePointsIndices.end(), ++ridge.ridgePointsIndices.begin(), ridge.ridgePointsIndices.end());
    parentRidge->localPeakIdx = vMountainPts[parentRidge->localPeakIdx].height < vMountainPts[ridge.localPeakIdx].height ? ridge.localPeakIdx : parentRidge->localPeakIdx;
    parentRidge->endPtIdx = ridge.endPtIdx;
    parentRidge->length += ridge.length;
}

std::optional<std::vector<std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>>> MountainData::makeRidgeMarker(const std::vector<int>& branchPts, QSharedPointer<DRidgeMarker> parentMarker, const std::tuple<float, float, float, float>& scaleData)
{
    auto&& genData = Generation::Data::get();
    std::vector<std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>> nextRidges;

    for(auto&& branchPoint : branchPts)
    {
        auto&& ridgeRes = std::ranges::find_if(vRidgeline, [&branchPoint](const auto& ele) {return ele.branchPtIdx == branchPoint; });

        std::vector<QVector3D> pts;
        std::vector<int> ptIndices;
        std::vector<int> children;
        int idx = ridgeRes->endPtIdx;
        while (ridgeRes->rootPtIdx <= idx)
        {
            ptIndices.emplace_back(idx);
            pts.emplace_back(vMountainPts[idx].pos.x * std::get<0>(scaleData) + std::get<1>(scaleData),
                vMountainPts[idx].height,
                vMountainPts[idx].pos.z * std::get<2>(scaleData) + std::get<3>(scaleData));

            if (ridgeRes->rootPtIdx != idx && vMountainPts[idx].children.size() > 1)
                for (auto&& child : vMountainPts[idx].children)
                    if (auto&& res = std::ranges::find_if(ptIndices, [&child](const auto& ele) {return ele == child; }); res == ptIndices.end())
                        children.emplace_back(child);

            idx = vMountainPts[idx].parent;
        }

        std::vector<QVector3D> reversedPts;
        reversedPts.insert(reversedPts.end(),
            std::make_move_iterator(pts.rbegin()),
            std::make_move_iterator(pts.rend()));

        Q_ASSERT(pts.size() >= 2);

        auto ridgeMarker = genData->createMarker<DRidgeMarker, true>(reversedPts, parentMarker);
        parentMarker->joinRidgeAsSubridge(ridgeMarker);

        if (!children.empty())
            nextRidges.emplace_back(std::pair<std::vector<int>, QSharedPointer<DRidgeMarker>>(children, ridgeMarker));
    }

    if (nextRidges.empty())
        return {};

    return nextRidges;
}
