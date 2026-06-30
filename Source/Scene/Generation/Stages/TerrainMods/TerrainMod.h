#pragma once
#include "River/TerrainModRiver.h"
#include "Lake/TerrainModLake.h"
#include "Desert/TerrainModSand.h"
static_assert(int(Generation::ETerrainMod::Last) == __LINE__ - 2);

namespace EAC
{
    struct GenerateMods
    {
        template<Generation::ETerrainMod TM>
        static std::vector<QSharedPointer<Generation::TerrainModBase>> Action()
        {
            return Generation::TerrainMod<TM>::generateAll();
        }
    };

    struct GetSubmitTarget
    {
        template<Generation::ETerrainMod TM>
        static Generation::ETerrainMod Action()
        {
            return Generation::TerrainMod<TM>::SubmitAs;
        }
    };

    struct ClearMods
    {
        template<Generation::ETerrainMod TM>
        static void Action()
        {
            Generation::TerrainMod<TM>::clearAll();
        }
    };

    struct ApplyMods
    {
        template<Generation::ETerrainMod TM>
        static TerrainMeshVertex Action(const std::vector<TerrainMeshVertex>& alterations)
        {
            return Generation::TerrainMod<TM>::apply(alterations);
        }
    };

    struct SaveTerrainMods
    {
        template<Generation::ETerrainMod TM>
        static void Action(const std::vector<QSharedPointer<Generation::TerrainModBase>>& mods, OmniBin<std::ios::out>& omniBin)
        {
            omniBin << mods.size();

            for(auto&& mod : mods)
                omniBin << mod.staticCast<Generation::TerrainMod<TM>>();
        }
    };

    struct LoadTerrainMods
    {
        template<Generation::ETerrainMod TM>
        static std::vector<QSharedPointer<Generation::TerrainModBase>> Action(OmniBin<std::ios::in>& omniBin)
        {
            std::vector<QSharedPointer<Generation::TerrainModBase>> results;

            size_t count;
            omniBin >> count;
            results.reserve(count);

            for (size_t i = 0; i < count; ++i)
            {
                QSharedPointer<Generation::TerrainMod<TM>> mod;
                omniBin >> mod;
                results << mod;
            }

            return results;
        }
    };

    struct PostLoadTerrainMod
    {
        template<Generation::ETerrainMod TM>
        static void Action(Generation::TerrainModBase* object)
        {
            Generation::TerrainMod<TM>::postLoad(object);
        }
    };
}