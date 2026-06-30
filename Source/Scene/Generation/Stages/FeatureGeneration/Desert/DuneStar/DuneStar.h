#pragma once
#include "../TerrainBlockDesert.h"

namespace Generation
{
    template<>
    struct DesertClusterSubData<EDesertBlockSubtype::DuneStar> : DesertClusterSubDataBase
    {
        DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData);

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates) override;
        virtual QSharedPointer<DesertSubClusterBase> createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster) override;

    private:

        std::vector<CellsLayer> layers;
        std::unordered_set<int> allLayersCells;
    };

    template<>
    class DesertSubCluster<EDesertBlockSubtype::DuneStar> : public DesertSubClusterBase
    {
    public:
        DesertSubCluster() = default;
        DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster);

        virtual void generate() override;
    };
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneStar>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneStar>& object, OmniBin<std::ios::in>& omniBin);