#pragma once
#include "../StageToolsBase.h"

namespace Generation
{
    class TerrainBlockClusterBase;
}

namespace Design
{
    template<>
    class StageTools<EGenerationStage::FeatureGeneration> final : public StageToolsBase
    {
    public:
        StageTools();

        virtual SelectionMgrBase* getSelectionMgr() const override;

        virtual void bind() override;
        virtual void unbind() override;

        virtual void save(OmniBin<std::ios::out>& writer) const override;
        virtual void load(OmniBin<std::ios::in>& reader) override;

        struct ClusterTriangle
        {
            Generation::TerrainBlockClusterBase* cluster;
            IndexType triangleIdx;
        };
        std::optional<ClusterTriangle> findClusterTriangleUnderCursor() const;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;
    };
}
