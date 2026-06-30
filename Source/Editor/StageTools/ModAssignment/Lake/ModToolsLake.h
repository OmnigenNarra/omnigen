#pragma once
#include "../ModToolsBase.h"

namespace Design
{
    template<>
    struct ModTools<Generation::ETerrainMod::Lake> : ModToolsBase
    {
        virtual void bind() override;
        virtual void unbind() override;
        virtual QWidget* create() override;

    private:
        bool isSpawningLake = false;

        bool spawnLake(const QMouseEvent& me);
        bool spawnLake_Undo(const QMouseEvent&);

        // Viewport mouse controls
        bool eventFilter(QObject* obj, QEvent* event);
    };
}
