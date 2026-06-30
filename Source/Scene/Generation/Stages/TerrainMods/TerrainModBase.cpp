#include "stdafx.h"
#include "TerrainModBase.h"

namespace Generation
{
    TerrainModBase::TerrainModBase(ETerrainMod inType, const QSet<int>& inArea)
        : type(inType)
        , area(inArea)
        , guid(makeGuid())
    {
    }
}

void omniSave(const Generation::TerrainModBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.type;
    omniBin << object.area;
    omniBin << object.guid;
}

void omniLoad(Generation::TerrainModBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.type;
    omniBin >> object.area;
    omniBin >> object.guid;
}
