#pragma once
#include "../TerrainModBase.h"
#include "Utils/Polygon.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::Sand>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::Sand>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    struct EnvBound;
    
    struct SandEmbankmentSlice
    {
        QVector3D upPoint;
        QVector3D downPoint;

        GVector2D centerPoint;
        float radius;
    };

    struct SandEmbankmentData
    {
        std::vector<SandEmbankmentSlice> slices;
        bool isfullCircle = true;
    };

    struct SandPolygon
    {
        Polygon2D polygon;
        std::vector<float> heights;
    };

    template<>
    class TerrainMod<ETerrainMod::Sand> : public TerrainModBase
    {
    public:
        TerrainMod(QSet<int>&& inArea, SandEmbankmentData&& sandData);

        static void postLoad(TerrainModBase* object) {};
        static std::vector<QSharedPointer<TerrainModBase>> generateAll();
        static TerrainMeshVertex apply(const std::vector<TerrainMeshVertex>& alterations);
        static void clearAll();
        static QSet<int> computeArea(const SandEmbankmentData& sandData);

        TerrainMod() = default;

        constexpr static ETerrainMod SubmitAs = ETerrainMod::Sand;
        virtual void submitAll(ModAlterationsList* mal) const override;
        virtual QVector4D getDebugColor() const override;

    protected:
        
        static QSharedPointer<TerrainModBase> createSandEmbankment(const Isohypse* ih);

        void prepareSandQuadsData();
        void showSandSurface() const;
    
        SandEmbankmentData sandData;
        std::vector<SandPolygon> sandQuads;

        FRIEND_OMNIBIN_NS(TerrainMod)
    };
}

