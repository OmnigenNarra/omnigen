#include "stdafx.h"
#include "TerrainModRiverData.h"
#include "RiverMarker.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Utils/Interpolation.h"

namespace Generation
{
    std::tuple<float, float, QVector3D, QVector3D> getBasicRowData(const RiverSimpleRow& row)
    {
        auto&& right = row.rightBound;
        auto&& left = row.leftBound;
        return { distance(right, left), row.depth, right, (left - right).normalized() };
    }

    std::vector<QVector3D> buildRiverRow(const RiverSimpleRow& row, const std::array<float, 7>& u, const std::array<float, 7>& d)
    {
        static QVector3D upDir = { 0, 1, 0 };
        auto [width, depth, rightPos, leftDir] = getBasicRowData(row);
        //spawn<DLineMarker>(row.rightBound, row.leftBound, Colors::white, 0.0f, ELineDecorator::Arc);

        std::vector<QVector3D> result(7);
        for (int i = 0; i < 7; ++i)
        {
            result[i] = rightPos + leftDir * width * u[i] - upDir * depth * d[i];
            result[i].setY(result[i].y() /* + row.riverPt.y() */);
        }

        return result;
    }

    std::vector<QVector3D> buildPointBarRow(const RiverSimpleRow& row, std::array<float, 7> u, std::array<float, 7> d, float accAngle, float pointBarMagnitude)
    {
        float f = fastSin(qDegreesToRadians(accAngle));
        float fa = std::abs(f);
        float sgn = (fa != 0.0f) ? (f / fa) : 0.0f;

        f = sgn * std::sqrt(fa) * pointBarMagnitude;
        fa = std::abs(f);

        float u15 = u[5] - u[1];
        float u2f = (u[2] - u[1]) / u15;
        float u3f = (u[3] - u[1]) / u15;
        float u4f = (u[4] - u[1]) / u15;

        if (f < 0)
        {
            // Right point-bar
            u[1] += fa * 0.5f;
            d[1] = 0.1f;

            // Left undercut
            u[5] += (1.0 - u[5]) * fa;
            d[5] += fa;
            d[4] += fa;
        }
        else
        {
            // Left point-bar
            u[5] -= fa * 0.5f;
            d[5] = 0.1f;

            // Right undercut
            u[1] -= u[1] * fa;
            d[1] += fa;
            d[2] += fa;
        }

        u[2] = std::lerp(u[1], u[5], u2f);
        u[3] = std::lerp(u[1], u[5], u3f);
        u[4] = std::lerp(u[1], u[5], u4f);

        return buildRiverRow(row, u, d);
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::Aa>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;

            // Sculpt random waterfalls
            if (i < rows.size() - 1)
            {
                float hDiff = rows[i].riverPt.y() - rows[i + 1].riverPt.y();
                if (hDiff < 100.0f)
                    continue;

                // Add a row almost directly below last row.
                RiverSimpleRow stepRow = rows[i];
                stepRow.rightBound = std::lerp(rows[i].rightBound, rows[i + 1].rightBound, 0.01f);
                stepRow.leftBound = std::lerp(rows[i].leftBound, rows[i + 1].leftBound, 0.01f);
                stepRow.depth = 1.5 * rows[i].depth;
                stepRow.riverPt = rows[i + 1].riverPt;

                RiverRowInfo stepInfo;
                stepInfo.CP = buildRiverRow(stepRow, u, d);
                //spawn<DLineMarker>(stepInfo.CP[3]);
                result <<= stepInfo;
            }
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::A>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;

            // Sculpt random steps
            if (i < rows.size() - 1)
            {
                float hDiff = rows[i].riverPt.y() - rows[i + 1].riverPt.y();
                if (hDiff < 30.0f)
                    continue;

                // Add a row almost directly below last row.
                RiverSimpleRow stepRow = rows[i];
                stepRow.rightBound = std::lerp(rows[i].rightBound, rows[i + 1].rightBound, 0.05f);
                stepRow.leftBound = std::lerp(rows[i].leftBound, rows[i + 1].leftBound, 0.05f);
                stepRow.depth = 1.3 * rows[i].depth;
                stepRow.riverPt = rows[i + 1].riverPt;

                RiverRowInfo stepInfo;
                stepInfo.CP = buildRiverRow(stepRow, u, d);
                //spawn<DLineMarker>(stepInfo.CP[3]);
                result <<= stepInfo;
            }
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::B>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::C>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result = { { buildRiverRow(rows[0], u, d) } };
        
        float accAngle = 0.0f;
        for (int i = 1; i < rows.size(); ++i)
        {
            auto s0 = rows[i - 1].rightBound - rows[i - 1].leftBound;
            auto s1 = rows[i].rightBound - rows[i].leftBound;
            accAngle += angle180S(s0.normalized(), s1.normalized());
            
            static const float fallback = 5.0f;
            if (float a = std::abs(accAngle); a > fallback)
                accAngle -= (accAngle / a) * fallback;

            RiverRowInfo nInfo;
            nInfo.CP = buildPointBarRow(rows[i], u, d, accAngle, 1.0f);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::D>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::DA>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::E>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result = { { buildRiverRow(rows[0], u, d) } };

        float accAngle = 0.0f;
        for (int i = 1; i < rows.size(); ++i)
        {
            auto s0 = rows[i - 1].rightBound - rows[i - 1].leftBound;
            auto s1 = rows[i].rightBound - rows[i].leftBound;
            accAngle += angle180S(s0.normalized(), s1.normalized());

            static const float fallback = 1.0f;
            if (float a = std::abs(accAngle); a > fallback)
                accAngle -= (accAngle / a) * fallback;

            RiverRowInfo nInfo;
            nInfo.CP = buildPointBarRow(rows[i], u, d, accAngle, 1.0f);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::F>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<RiverRowInfo> Riverbed::generateRiverSegmentCPs<ERiverType::G>(std::vector<QVector3D>* nextRow)
    {
        std::vector<RiverRowInfo> result;
        
        for (int i = 0; i < rows.size(); ++i)
        {
            RiverRowInfo nInfo;
            nInfo.CP = buildRiverRow(rows[i], u, d);
            result <<= nInfo;
        }

        return result;
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::Aa>()
    {
        // Randomly increase steepness
        for (int i = rows.size() - 2; i >= 0; --i)
            if (randomChance() < 0.4f)
                rows[i].riverPt.setY(rows[i + 1].riverPt.y());

        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::A>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::B>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::C>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::D>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::DA>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::E>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::F>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    template<> std::vector<QVector3D> Riverbed::generateRiverSegmentFirstRow<ERiverType::G>()
    {
        return buildRiverRow(rows[0], u, d);
    }

    void RiverSegmentData::computeParams(const std::vector<RiverArc>& arcs, const QSharedPointer<DRiverMarker>& river)
    {
        auto&& traits = ERiverTypeConstexpr::UseIn<EAC::GetRiverTraits>(type);
        auto&& riverPts = river->getControlPoints();

        int firstIdx = arcs[arcRange[0]].range[0];
        int lastIdx = arcs[arcRange[1]].range[1];

        // Average flood plains width
        float avgBoundWidth = 0.0f;
        for (int i = firstIdx; i < lastIdx; ++i)
            for (auto&& bound : river->getRiverBounds())
                avgBoundWidth += std::get<float>(directionalBoundDistance(bound, riverPts[i]));
        avgBoundWidth /= float(lastIdx - firstIdx + 1);

        // Water plane width
        std::uniform_real_distribution<float> entrenchmentDist(traits.entrenchment.first, traits.entrenchment.second);
        float entrenchment = entrenchmentDist(gRandomEngine);
        float waterPlaneWidth = avgBoundWidth / entrenchment;

        // Riverbed width
        static std::uniform_real_distribution<float> waterLevelInBedDist(0.7, 0.9);
        float waterLevel = waterLevelInBedDist(gRandomEngine);
        avgWidth = waterPlaneWidth / waterLevel;
        avgWidth = std::clamp(avgWidth, 200.0f, 5000.f);

        // Riverbed depth
        std::uniform_real_distribution<float> wdRatioDist(traits.wdRatio.first, traits.wdRatio.second);
        float wdRatio = wdRatioDist(gRandomEngine);
        avgDepth = avgWidth / wdRatio;
        avgDepth = std::clamp(avgDepth, 0.0f, 5000.f);
    }

    void Riverbed::initialize()
    {
        EBedShape shape;
        switch (type)
        {
        case ERiverType::B:
        case ERiverType::C:
            shape = randomPick(std::vector{ EBedShape::Triangular, EBedShape::Trapezoidal, EBedShape::Rectangular, EBedShape::Parabolic }, gRandomEngine);
            break;

        case ERiverType::Aa: 
        case ERiverType::A:
        case ERiverType::E:
        case ERiverType::G:
            shape = randomPick(std::vector{ EBedShape::Triangular, EBedShape::Trapezoidal, EBedShape::Parabolic }, gRandomEngine);
            break;

        case ERiverType::D:
        case ERiverType::DA:
        case ERiverType::F:
            shape = randomPick(std::vector{ EBedShape::Trapezoidal, EBedShape::Rectangular, EBedShape::Parabolic }, gRandomEngine);
            break;
        }

        std::tie(u, d) = getTemplate(shape);
    }

    std::vector<Generation::RiverRowInfo> Riverbed::generateMesh(std::vector<QVector3D>* nextRow)
    {
        return ERiverTypeConstexpr::UseIn<GenerateRiverSegmentCPs>(type, *this, nextRow);
    }

    std::vector<QVector3D> Riverbed::generateFirstRow()
    {
        return ERiverTypeConstexpr::UseIn<GenerateFirstRow>(type, *this);
    }

    std::tuple<std::array<float, 7>, std::array<float, 7>> getTemplate(EBedShape s)
    {
        constexpr static std::array<float, 7> u0{ 0.0, 0.17, 0.34, 0.5, 0.66, 0.83, 1.0 };
        constexpr static std::array<float, 7> d0{ 0.0, 0.33, 0.66, 1.0, 0.66, 0.33, 0.0 };

        constexpr static std::array<float, 7> u1{ 0.0, 0.15, 0.3, 0.5, 0.7, 0.85, 1.0 };
        constexpr static std::array<float, 7> d1{ 0.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.0 };

        constexpr static std::array<float, 7> u2{ 0.0, 0.01, 0.25, 0.5, 0.75, 0.99, 1.0 };
        constexpr static std::array<float, 7> d2{ 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0 };

        constexpr static std::array<float, 7> u3{ 0.0, 0.1, 0.4, 0.5, 0.6, 0.9, 1.0 };
        constexpr static std::array<float, 7> d3{ 0.0, 0.5, 0.9, 1.0, 0.9, 0.5, 0.0 };

        switch (s)
        {
        case EBedShape::Triangular: return std::tuple{ u0, d0 };
        case EBedShape::Trapezoidal: return std::tuple{ u1, d1 };
        case EBedShape::Rectangular: return std::tuple{ u2, d2 };
        case EBedShape::Parabolic: return std::tuple{ u3, d3 };
        }

        Q_ASSERT(false);
        return {};
    }

}