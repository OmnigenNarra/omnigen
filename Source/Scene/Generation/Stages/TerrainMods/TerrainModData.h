#pragma once
#include "Utils/EnumAsConstexpr.h"

namespace Generation
{
    enum class ETerrainMod
    {
        River,
        Lake,
        Sand,

        Last // Auxiliary member
    };
    ENABLE_ENUM_AS_CONSTEXPR(ETerrainMod, ETerrainMod::Last);
}