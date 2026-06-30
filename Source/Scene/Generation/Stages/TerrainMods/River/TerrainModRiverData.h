#pragma once
#include "Utils/EnumAsConstexpr.h"
#include "Utils/OmniBin/OmniBinQt.h"

namespace Generation
{
    enum class ERiverType
    {
        Aa,
        A,
        B,
        C,
        D,
        DA,
        E,
        F,
        G,

        Last
    };
    ENABLE_ENUM_AS_CONSTEXPR(ERiverType, ERiverType::Last);
}

class DRiverMarker;

struct RiverParams
{
    std::pair<float, float> slopeAngleRange = { -1, -1 };
    std::pair<float, float> wdRatio = { -1, -1 };
    std::pair<float, float> sinusoity = { -1, -1 };
    std::pair<float, float> entrenchment = { -1, -1 };
};

namespace Generation
{
    struct RiverSegmentData;

    struct RiverRowInfo
    {
        std::vector<QVector3D> CP;
    };

    struct RiverNurbAxisPoint
    {
        QVector3D pos;
        float width;
        float depth;
        ERiverType type;
    };

    struct RiverNurbBoundPoint
    {
        QVector3D pos;
        QVector3D axisPoint;
        float width;
        float depth;
        ERiverType type;
    };

    struct RiverSimpleRow
    {
        RiverSimpleRow(QVector3D rBound, QVector3D lBound, const RiverNurbAxisPoint& riverAxisPoint)
            : rightBound(std::move(rBound))
            , leftBound(std::move(lBound))
            , depth(riverAxisPoint.depth)
            , riverPt(riverAxisPoint.pos)
        {}

        QVector3D rightBound;
        QVector3D leftBound;
        float depth;
        QVector3D riverPt;
    };

    enum class EBedShape
    {
        Triangular,
        Trapezoidal,
        Rectangular,
        Parabolic
    };
    std::tuple<std::array<float, 7>, std::array<float, 7>> getTemplate(EBedShape s);

    struct Riverbed
    {
        ERiverType type;
        std::vector<RiverSimpleRow> rows;
        
        void initialize();
        std::vector<RiverRowInfo> generateMesh(std::vector<QVector3D>* nextRow);
        std::vector<QVector3D> generateFirstRow();

        std::array<float, 7> u;
        std::array<float, 7> d;

    private:
        struct GenerateRiverSegmentCPs
        {
            template<ERiverType RT>
            static std::vector<RiverRowInfo> Action(Riverbed& bed, std::vector<QVector3D>* nextRow)
            {
                return bed.generateRiverSegmentCPs<RT>(nextRow);
            }
        };

        struct GenerateFirstRow
        {
            template<ERiverType RT>
            static std::vector<QVector3D> Action(Riverbed& bed)
            {
                return bed.generateRiverSegmentFirstRow<RT>();
            }
        };

        template<ERiverType RT>
        std::vector<RiverRowInfo> generateRiverSegmentCPs(std::vector<QVector3D>* nextRow)
        {
            Q_ASSERT(false);
            return {};
        }
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::Aa>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::B>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::C>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::D>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::DA>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::E>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::F>(std::vector<QVector3D>* nextRow);
        template<> std::vector<RiverRowInfo> generateRiverSegmentCPs<ERiverType::G>(std::vector<QVector3D>* nextRow);

        template<ERiverType RT>
        std::vector<QVector3D> generateRiverSegmentFirstRow()
        {
            Q_ASSERT(false);
            return {};
        }

        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::Aa>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::B>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::C>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::D>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::DA>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::E>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::F>();
        template<> std::vector<QVector3D> generateRiverSegmentFirstRow<ERiverType::G>();
    };

    enum class ERiverSide
    {
        None = 0,
        Left = -1,
        Right = 1
    };

    struct RiverArc
    {
        std::array<int, 2> range;
        ERiverSide side = ERiverSide::None;
    };

    struct RiverSegmentData
    {
        ERiverType type;
        std::array<int, 2> arcRange;
        float avgWidth;
        float avgDepth;

        void computeParams(const std::vector<RiverArc>& arcs, const QSharedPointer<DRiverMarker>& river);
    };

    template<ERiverType RT>
    struct RiverTraits
    {
        inline static RiverParams data; // loaded from ini
    };
}

namespace EAC
{
    struct SetRiverTraits
    {
        template<Generation::ERiverType RT>
        static void Action(const RiverParams& params)
        {
            Generation::RiverTraits<RT>::data = params;
        }
    };

    struct GetRiverTraits
    {
        template<Generation::ERiverType RT>
        static const RiverParams& Action()
        {
            return Generation::RiverTraits<RT>::data;
        }
    };
}

