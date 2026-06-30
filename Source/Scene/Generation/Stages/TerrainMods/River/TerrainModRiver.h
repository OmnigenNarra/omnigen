#pragma once

#include "../TerrainModBase.h"
#include "Utils/Polygon.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockUtils.h"
#include "TerrainModRiverData.h"

class DRiverMarker;
class DRiverNurbsMarker;

void omniSave(const Generation::TerrainMod<Generation::ETerrainMod::River>& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::TerrainMod<Generation::ETerrainMod::River>& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    struct EnvBound;

    template<>
    class TerrainMod<ETerrainMod::River> : public TerrainModBase, public QEnableSharedFromThis<TerrainMod<ETerrainMod::River>>
    {
    public:
        const auto& getName() const { return name; };
        const auto& getMarkerGuid() const { return riverGuid; };

        void setName(const QString& newName);

        static void postLoad(TerrainModBase* object);
        static std::vector<QSharedPointer<TerrainModBase>> generateAll();
        static TerrainMeshVertex apply(const std::vector<TerrainMeshVertex>& alterations);
        static void clearAll();

        TerrainMod(const QSharedPointer<DRiverMarker>& inRiver, const QSharedPointer<TerrainModBase>& inParentMod);

        // Tools can use these
        void createNurbShapes();

        constexpr static ETerrainMod SubmitAs = ETerrainMod::River;
        virtual void submitAll(ModAlterationsList* mal) const override;
        virtual QVector4D getDebugColor() const override;

        static QSharedPointer<TerrainModBase> processSingleRiver(const QSharedPointer<DRiverMarker>& river);

    protected:
        TerrainMod() = default;
       
        // Env bound construction (postprocess)
        std::array<std::vector<QSharedPointer<EnvBound>>, 2> computeRawEnvBounds() const;

        // Helpers
        float calculateRiverOriginDisplacement(const TerrainMeshVertex& origin) const;

        QSharedPointer<TerrainMod<ETerrainMod::River>> parentRiverMod;
        qint64 parentModGuid;

        QSharedPointer<DRiverMarker> river;
        qint64 riverGuid;

        QSharedPointer<DRiverNurbsMarker> nurb;
        qint64 nurbGuid;

        QString name;

        void makeName();

        // maps river idx -> generated Mod idx
        static void processRiver(const QSharedPointer<DRiverMarker>& river, QSharedPointer<TerrainModBase> parentMod, std::vector<QSharedPointer<TerrainModBase>>* outputMods);
        static bool cellContainsRiver(const QSharedPointer<DRiverMarker>& river, const Polygon2D& cell);

        //Function edits child nurb's control points in order to provide seemles merge of a child's nurb to the parent's nurb
        static void computeNurbsMerging(std::vector<RiverRowInfo>* mergedSource, const std::vector<RiverRowInfo>& mergeTarget);

        static std::vector<RiverArc> detectArcs(const QSharedPointer<DRiverMarker>& river);
        static QVector3D findRiverBoundPoint(const GVector2D& edgePoint, float refH);
        static std::vector<RiverNurbAxisPoint> generateRiverAxes(const std::vector<QVector3D>& riverPts, const std::vector<RiverArc>& arcs, const std::vector<RiverSegmentData>& segments);
        static std::array<std::vector<RiverNurbBoundPoint>, 2> refineFloodBounds(const std::vector<RiverNurbAxisPoint>& axis, const QSharedPointer<DRiverMarker>& river);
        static std::vector<RiverRowInfo> createMainRiverbed(const QSharedPointer<DRiverMarker>& river, const std::vector<RiverNurbAxisPoint>& axis, const std::array<std::vector<RiverNurbBoundPoint>, 2>& bounds);

        static inline std::map<TerrainMod<ETerrainMod::River>*, std::vector<RiverRowInfo>> finalRiverbeds;

        FRIEND_OMNIBIN_NS(TerrainMod)
    };
}