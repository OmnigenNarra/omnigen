#include "stdafx.h"
#include "LithoCluster.h"
#include "../../OmnigenGeneration.h"

namespace Generation
{
    LithoCluster::LithoCluster(const qint64& inTypeId, const std::unordered_set<int>& inCells, const std::unordered_map<int, LithoThresholdData>& inThresholdData /*= {}*/)
        : typeId(inTypeId)
        , cells(inCells)
        , thresholdData(inThresholdData)
    {
        if (!thresholdData.empty())
            return;

        static hybrid_int_distribution<int> firstThresholdDist(2, 8, 0.3, 0.5);
        int firstThreshold = firstThresholdDist(gRandomEngine);

        hybrid_int_distribution<int> secondThresholdDist(firstThreshold * 2, firstThreshold * 3, 0.1, 0.5);
        int secondThreshold = secondThresholdDist(gRandomEngine);
        thresholdData[firstThreshold] = LithoThresholdData{ 1 };
        thresholdData[secondThreshold] = LithoThresholdData{ 2 };
    }
}

void omniSave(const Generation::LithoCluster& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.typeId;
    omniBin << object.cells;
    omniBin << object.thresholdData;
}

void omniLoad(Generation::LithoCluster& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.typeId;
    omniBin >> object.cells;
    omniBin >> object.thresholdData;
}
