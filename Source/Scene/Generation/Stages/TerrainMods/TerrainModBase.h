#pragma once
#include <QSharedPointer>
#include "TerrainModData.h"
#include <QVector4D>
#include "Utils/OmniBin/OmniBinQt.h"
#include "../FeatureGeneration/TerrainBlockData.h"
#include "../FeatureGeneration/ClusterMeshMarker.h"
#include "Editable.h"

namespace Generation
{
    class TerrainModBase;
}

void omniSave(const Generation::TerrainModBase& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainModBase& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    class TerrainBlockClusterBase;

    using ModAlterationsList = QHash<int /*cellId*/, QHash<IndexType /*vertex idx*/, std::vector<TerrainMeshVertex> /*options*/>>;

    class TerrainModBase : public Editable
    {
    public:
        TerrainModBase(ETerrainMod inType, const QSet<int>& inArea);
        
        virtual void submitAll(ModAlterationsList*) const = 0;
        virtual QVector4D getDebugColor() const = 0;

        ETerrainMod getType() const { return type; }
        const auto& getArea() const { return area; }
        const auto& getGuid() const { return guid; }

    protected:
        TerrainModBase() = default;

        ETerrainMod type;
        QSet<int> area;
        qint64 guid;

        FRIEND_OMNIBIN_NS(TerrainModBase);
    };

    template<ETerrainMod TM>
    class TerrainMod : public TerrainModBase
    {
    };

    template<>
    class TerrainMod<ETerrainMod::Last> : public TerrainModBase
    {
    public:
        static inline void postLoad(TerrainModBase* object) {};
        static std::vector<QSharedPointer<TerrainModBase>> generateAll() { return {}; };
        static TerrainMeshVertex apply(const std::vector<TerrainMeshVertex>& alterations) { return {}; };
        static void clearAll() {};

        using TerrainModBase::TerrainModBase;

        constexpr static ETerrainMod SubmitAs = ETerrainMod::Last;
        virtual void submitAll(ModAlterationsList*) const override {};
        virtual QVector4D getDebugColor() const override { return QVector4D(); }
    };
}

inline void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::Last>& object, OmniBin<std::ios::out>& omniBin) {};
inline void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::Last>& object, OmniBin<std::ios::in>& omniBin) {};