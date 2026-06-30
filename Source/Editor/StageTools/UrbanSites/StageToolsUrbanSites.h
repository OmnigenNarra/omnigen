#pragma once
#include "../StageToolsBase.h"

namespace Design
{
    template<>
    class StageTools<EGenerationStage::UrbanSites> final : public StageToolsBase
    {
    public:
        StageTools();

        virtual void bind() override;
        virtual void unbind() override;
        virtual SelectionMgrBase* getSelectionMgr() const override;

        virtual void save(OmniBin<std::ios::out>& writer) const override;
        virtual void load(OmniBin<std::ios::in>& reader) override;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;
    };
}
