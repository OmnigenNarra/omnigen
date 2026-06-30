#pragma once
#include "../TerrainModBase.h"
#include "Utils/Polygon.h"

void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::Lake>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::Lake>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    struct EnvBound;

    template<>
    class TerrainMod<ETerrainMod::Lake> : public TerrainModBase
    {
        struct ControlPoint
        {
            GVector2D position;
            float shoreMargin;
            float bottomMargin;
        };

    public:
        TerrainMod(QSet<int> inArea, Polygon2D inAreaPoly, std::vector<ControlPoint> inPts);

        static void postLoad(TerrainModBase* object);
        static std::vector<QSharedPointer<TerrainModBase>> generateAll();
        static TerrainMeshVertex apply(const std::vector<TerrainMeshVertex>& alterations);
        static void clearAll();

        TerrainMod() = default;

        constexpr static ETerrainMod SubmitAs = ETerrainMod::River;
        virtual void submitAll(ModAlterationsList* mal) const override;
        virtual QVector4D getDebugColor() const override;

        float calculateVertexOffset(const TerrainMeshVertex& v) const;

        static QSharedPointer<TerrainModBase> createLake(const QSet<int>& area);
        static QSet<int> computeLakeArea(int seed);

    protected:
        void showWaterSurface() const;
        static std::unordered_set<int> chooseLakeSeeds();

        std::vector<ControlPoint> controlPoints;
        Polygon2D areaPolygon;
        float depth = 500;
        mutable float surfaceHeight = std::numeric_limits<float>::max();

        FRIEND_OMNIBIN_NS(TerrainMod)
    };
}
