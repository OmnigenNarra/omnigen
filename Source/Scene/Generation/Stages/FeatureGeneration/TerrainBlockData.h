#pragma once
#include <QSharedPointer>
#include "Utils/EnumAsConstexpr.h"
#include <limits>
#include "Utils/OmniBin/OmniBin.h"

class DRiverMarker;
class DRidgeMarker;
class DDomain;

namespace Voronoi
{
    class BoxDiagram;
}

namespace Generation
{
    class TerrainBlockClusterBase;
}

namespace Generation
{
    class DEM;
    struct BorderPoint;

    enum class ETerrainBlock
    {
        Beach,
        Cliff,
        Fault,
        Flatland,
        Ridge,
        Slope,
        Seabed,
        Precipice,
        Desert,
        SmoothSlope,

        Last // Auxiliary member
    };
    ENABLE_ENUM_AS_CONSTEXPR(ETerrainBlock, ETerrainBlock::Last);

    struct BlockChanceData
    {
        float centerH = 0.0f;
        float maxH = -1.0f;
        float minH = std::numeric_limits<float>::max();
        float steepness = 0.0f;                             // deltaH / cell radius

        QSharedPointer<DDomain> terrainDomain;
        QSharedPointer<DDomain> waterDomain;
        QSharedPointer<DDomain> biomeDomain;

        bool isWithinShoreDist = false;
        bool isWithinPeakDist = false;
        bool isWaterSideOfShore = false;
        bool isUnderRidge = false;

        qint64 lithoType;
    };

    // Compatible with ClusterSmoothing.hlsl : BorderPointInfo
#pragma pack(push, 4)
    struct BorderPointInfo
    {
        BorderPointInfo() = default;
        BorderPointInfo(const GVector2D& bp)
            : pos(bp)
        {}

        QVector3D pos;
        float distance = -1.0f; // used by shader
        float weight = 0.0f; // used by shader

        quint32 terrainTexWeights;
        quint32 biomeTexWeights;
        quint32 packParams;
        float temperature = 0.0f;
        float humidity = 0.0f;

        bool operator==(const BorderPointInfo& other) const
        {
            return pos == other.pos;
        }

        void setFinalData(TerrainBlockClusterBase* owner, const BorderPoint& bp);
    };
#pragma pack(pop)
}

using ComparePointPred = std::function<bool(const QVector3D&, const QVector3D&)>;

struct PointByHeightPred
{
    inline bool operator() (const QVector3D& a, const QVector3D& b) const
    {
        return a.y() < b.y();
    };
};

struct PointByDistancePred
{
    QVector3D point;
    PointByDistancePred(const QVector3D& inPoint) : point(inPoint){}
    inline bool operator() (const QVector3D& a, const QVector3D& b) const
    {
        return point.distanceToPoint(a) < point.distanceToPoint(b);
    };
};

struct MeshQueryData
{
    QVector3D pos;
    QSharedPointer<Generation::TerrainBlockClusterBase> cluster;
    IndexType triangleHit;

    auto operator<=>(const MeshQueryData&) const = default;
};

namespace std
{
    template<>
    struct std::hash<Generation::BorderPointInfo>
    {
        std::size_t operator()(Generation::BorderPointInfo const& bp) const noexcept
        {
            return qHash(bp.pos);
        }
    };
}
