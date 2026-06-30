#pragma once
#include "Utils/OmniBin/OmniBinQt.h"
#include <QMap>
#include <QSet>

namespace Generation
{
    class LithoCluster;
}

void omniSave(const Generation::LithoCluster& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::LithoCluster& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    struct LithoThresholdData
    {
        int texSlot = 0;
    };

    class LithoCluster
    {
    public:
        LithoCluster() = default;
        LithoCluster(const qint64& inTypeId, const std::unordered_set<int>& inCells, const std::unordered_map<int, LithoThresholdData>& inThresholdData = {});

        const auto getType() const { return typeId; }
        const auto getCells() const { return cells; }
        const auto getThresholdData() const { return thresholdData; }

    private:

        qint64 typeId;
        std::unordered_set<int> cells;
        std::unordered_map<int, LithoThresholdData> thresholdData;

        FRIEND_OMNIBIN_NS(LithoCluster)
    };
}