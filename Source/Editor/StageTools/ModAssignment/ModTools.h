#pragma once
#include "River/ModToolsRiver.h"
#include "Lake/ModToolsLake.h"

namespace EAC
{
    struct GetModTools
    {
        template<Generation::ETerrainMod TM>
        static Design::ModToolsBase* Action()
        {
            static Design::ModTools<TM> instance;
            return &instance;
        }
    };
}