#pragma once
#include "EnumAsConstexpr.h"
#include <QSharedPointer>

enum class EInterpolation01
{
    Linear,
    Power,
    InversePower,
    Smoothstep,
    InverseSmoothstep,
    InElastic,
    OutElastic,
    InOutElastic,
    OutBounce,
    InOutBounce,
    Last // aux
};
ENABLE_ENUM_AS_CONSTEXPR(EInterpolation01, EInterpolation01::Last)

enum class EInterpolation010
{
    Zero,
    InverseQuadratic,
    Sine,
    DoublePeakSine,
    Last // aux
};
ENABLE_ENUM_AS_CONSTEXPR(EInterpolation010, EInterpolation010::Last)

namespace Interpolation
{
    struct TechniqueBase
    {
        virtual double interpolate(double t) const { return -1.0f; };
        double operator()(double t) const { return interpolate(t); }
    };

    template<EInterpolation01 mode>
    struct Technique01 : TechniqueBase
    {
    };

    template<EInterpolation010 mode>
    struct Technique010 : TechniqueBase
    {
    };

#define DECLARE_INTERPOLATION_TECHNIQUE(type, name) \
template<> struct Technique##type<EInterpolation##type##::##name> : TechniqueBase \
{ \
    Technique##type(int inA = 0, int inB = 0) \
        : a(inA) \
        , b(inB) \
    {} \
    \
    int a = 0; \
    int b = 0; \
    \
    virtual double interpolate(double t) const override; \
}

#define DEFINE_INTERPOLATION_TECHNIQUE(type, name) \
double Technique##type<EInterpolation##type##::##name>::interpolate(double t) const

    DECLARE_INTERPOLATION_TECHNIQUE(01, Linear);
    DECLARE_INTERPOLATION_TECHNIQUE(01, Power);
    DECLARE_INTERPOLATION_TECHNIQUE(01, InversePower);
    DECLARE_INTERPOLATION_TECHNIQUE(01, Smoothstep);
    DECLARE_INTERPOLATION_TECHNIQUE(01, InverseSmoothstep);
    DECLARE_INTERPOLATION_TECHNIQUE(01, InElastic);
    DECLARE_INTERPOLATION_TECHNIQUE(01, OutElastic);
    DECLARE_INTERPOLATION_TECHNIQUE(01, InOutElastic);
    DECLARE_INTERPOLATION_TECHNIQUE(01, OutBounce);
    DECLARE_INTERPOLATION_TECHNIQUE(01, InOutBounce);
    DECLARE_INTERPOLATION_TECHNIQUE(01, Last);

    DECLARE_INTERPOLATION_TECHNIQUE(010, Zero);
    DECLARE_INTERPOLATION_TECHNIQUE(010, InverseQuadratic);
    DECLARE_INTERPOLATION_TECHNIQUE(010, Sine);
    DECLARE_INTERPOLATION_TECHNIQUE(010, DoublePeakSine);
    DECLARE_INTERPOLATION_TECHNIQUE(010, Last);

    struct TechniqueNode
    {
        TechniqueNode(const QSharedPointer<TechniqueBase>& inTech)
            : technique(inTech)
        {};

        TechniqueNode(const std::vector<QSharedPointer<TechniqueNode>>& inNodes, const std::vector<double>& inWeights)
            : nodes(inNodes)
            , weights(inWeights)
        {}

        QSharedPointer<TechniqueBase> technique;

        std::vector<QSharedPointer<TechniqueNode>> nodes;
        std::vector<double> weights;

        double interpolate(double t) const;
        double operator()(double t) const { return interpolate(t); }
    };

    namespace EAC
    {
        struct MakeTechnique01
        {
            template<EInterpolation01 TQ>
            static QSharedPointer<TechniqueBase> Action(int a = 0, int b = 0)
            {
                return QSharedPointer<Technique01<TQ>>::create(a, b);
            }
        };

        struct MakeTechnique010
        {
            template<EInterpolation010 TQ>
            static QSharedPointer<TechniqueBase> Action(int a = 0, int b = 0)
            {
                return QSharedPointer<Technique010<TQ>>::create(a, b);
            }
        };
    }

    QSharedPointer<TechniqueBase> getInterpolation01(EInterpolation01 type, int a = 0, int b = 0);

    QSharedPointer<TechniqueBase> getInterpolation010(EInterpolation010 type, int a = 0, int b = 0);

    QSharedPointer<TechniqueNode> getTechniqueNode(const QSharedPointer<TechniqueBase>& inTech);
}
