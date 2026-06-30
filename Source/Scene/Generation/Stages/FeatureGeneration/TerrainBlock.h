#pragma once
#include "Flatland/TerrainBlockFlatland.h"
#include "Fault/TerrainBlockFault.h"
#include "Ridge/TerrainBlockRidge.h"
#include "Slope/TerrainBlockSlope.h"
#include "Beach/TerrainBlockBeach.h"
#include "Cliff/TerrainBlockCliff.h"
#include "Seabed/TerrainBlockSeabed.h"
#include "Precipice/TerrainBlockPrecipice.h"
#include "Desert/TerrainBlockDesert.h"
#include "SmoothSlope/SmoothSlope.h"
static_assert(int(Generation::ETerrainBlock::Last) == __LINE__ - 2);

namespace EAC
{
    struct ChooseBlockType
    {
        template<Generation::ETerrainBlock TB>
        static float Action(const Generation::BlockChanceData& data)
        {
            return Generation::TerrainBlockCluster<TB>::chance(data);
        }
    };

    struct CreateMetaCluster
    {
        template<Generation::ETerrainBlock TB>
        static QSharedPointer<Generation::TerrainBlockMetaClusterBase> Action(const std::unordered_set<int>& inCells, std::optional<qint64> inGuid = std::nullopt)
        {
            auto&& newMetaCluster = QSharedPointer<Generation::TerrainBlockMetaCluster<TB>>::create(inCells);
            if (inGuid)
                newMetaCluster->setGuid(*inGuid);
            emit Editable::created(newMetaCluster);

            return std::move(newMetaCluster);
        }
    };

    struct CreateClusterData
    {
        template<Generation::ETerrainBlock TB>
        static QSharedPointer<Generation::ClusterDataBase> Action(Generation::TerrainBlockMetaClusterBase* metaCluster, int sourceCellId)
        {
            return QSharedPointer<Generation::ClusterData<TB>>::create(static_cast<Generation::TerrainBlockMetaCluster<TB>*>(metaCluster), sourceCellId);
        }
    };

    struct CreateTerrainCluster
    {
        template<Generation::ETerrainBlock TB>
        static QSharedPointer<Generation::TerrainBlockClusterBase> Action(const Generation::ClusterDataBase& data, std::optional<qint64> inGuid = std::nullopt)
        {
            auto&& newCluster = QSharedPointer<Generation::TerrainBlockCluster<TB>>::create(static_cast<const Generation::ClusterData<TB>&>(data));
            if (inGuid)
                newCluster->setGuid(*inGuid);
            emit Editable::created(newCluster);

            return std::move(newCluster);
        }
    };

    struct SaveTerrainMetaCluster
    {
        template<Generation::ETerrainBlock TB>
        static void Action(const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster, OmniBin<std::ios::out>& omniBin)
        {
            omniBin << metaCluster.staticCast<Generation::TerrainBlockMetaCluster<TB>>();
        }
    };

    struct LoadTerrainMetaCluster
    {
        template<Generation::ETerrainBlock TB>
        static QSharedPointer<Generation::TerrainBlockMetaClusterBase> Action(OmniBin<std::ios::in>& omniBin)
        {
            QSharedPointer<Generation::TerrainBlockMetaCluster<TB>> metaCluster;
            omniBin >> metaCluster;
            return metaCluster;
        }
    };

    struct GetBlockColor
    {
        template<Generation::ETerrainBlock TB>
        static QVector4D Action()
        {
            return Generation::TerrainBlockCluster<TB>::getDebugColor();
        }
    };

    struct GetClusterTraits
    {
        template<Generation::ETerrainBlock TB>
        static const auto& Action()
        {
            return Generation::ClusterTraits<TB>::clusterParams;
        }
    };

    struct GetMetaClusterTraits
    {
        template<Generation::ETerrainBlock TB>
        static const auto& Action()
        {
            return Generation::ClusterTraits<TB>::metaClusterParams;
        }
    };
}