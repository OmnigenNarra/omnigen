#pragma once
#include "../TerrainBlockDesert.h"

class DuneGraph;

namespace Generation
{
    template<>
    struct DesertClusterSubData<EDesertBlockSubtype::DuneLongitudinal> : DesertClusterSubDataBase
    {
        DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData);

        virtual std::unordered_set<int> customGrow(const std::unordered_set<int>& candidates) override;
        virtual QSharedPointer<DesertSubClusterBase> createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster) override;

        //Left to right
        std::vector<int> cellGraph;

        //Used when we construct the TerrainBlockCluster to set the type
        bool haveDominantMetaCluster;

        GVector2D baseWindDirection;
    };

    template<>
    class DesertSubCluster<EDesertBlockSubtype::DuneLongitudinal> : public DesertSubClusterBase
    {
    public:
        DesertSubCluster() = default;
        DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster, std::vector<int> inCellGraph);

        virtual void generate() override;

    private:
        std::vector<int> cellGraph;

    private:

        void removeSharpAngles();
        void straightenTheCellGraph();
        void straightenTheCellGraphImpl();
        DuneGraph generateDuneShape() const;
    };
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneLongitudinal>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneLongitudinal>& object, OmniBin<std::ios::in>& omniBin);