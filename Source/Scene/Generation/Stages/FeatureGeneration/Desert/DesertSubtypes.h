#pragma once
#include "TerrainBlockDesert.h"
#include "DuneBarchan/DuneBarchan.h"
#include "DuneLongitudinal/DuneLongitudinal.h"
#include "DuneStar/DuneStar.h"
#include "DuneNabkha/DuneNabkha.h"
#include "DuneSand/DuneSand.h"
static_assert(int(Generation::EDesertBlockSubtype::Last) == __LINE__ - 3);

namespace Generation
{
    namespace EAC
    {
        struct CreateDesertSubData
        {
            template<EDesertBlockSubtype BS>
            static QSharedPointer<DesertClusterSubDataBase> Action(ClusterData<ETerrainBlock::Desert>* baseData)
            {
                return QSharedPointer<DesertClusterSubData<BS>>::create(baseData);
            }
        };

        struct SaveDesertSubCluster
        {
            template<EDesertBlockSubtype BS>
            static void Action(const QSharedPointer<DesertSubClusterBase>& subCluster, OmniBin<std::ios::out>& writer)
            {
                writer << subCluster.staticCast<DesertSubCluster<BS>>();
            }
        };

        struct LoadDesertSubCluster
        {
            template<EDesertBlockSubtype BS>
            static void Action(QSharedPointer<DesertSubClusterBase>& subCluster, OmniBin<std::ios::in>& reader)
            {
                QSharedPointer<DesertSubCluster<BS>> subClusterReal;
                reader >> subClusterReal;
                subCluster = subClusterReal;
            }
        };
    }
}