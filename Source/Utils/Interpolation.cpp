#include "stdafx.h"
#include "Interpolation.h"

namespace Interpolation
{
    DEFINE_INTERPOLATION_TECHNIQUE(01, Linear)
    {
        return t;
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, Power)
    {
        return std::pow(t, a + 1);
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, InversePower)
    {
        return std::pow(t, 1.0f / (a + 1));
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, Smoothstep)
    {
        if (a == 0)
            return t * t * (3.0f - 2.0f * t);
        else if (a == 1)
            return t * t * t * (6.0f * t * t - 15.0f * t + 10.0f);
        else if (a == 2)
            return t * t * t * t * (35.0f - 84.0f * t + 70.0f * t * t - 20.0f * t * t * t);
        else
            return t * t * (3.0f - 2.0f * t); // default is same as a == 0
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, InverseSmoothstep)
    {
        return std::pow(2, 2 * a) * std::pow(t - 0.5f, 2 * a + 1) + 0.5f;
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, InElastic)
    {
        constexpr float c4 = (2.f * std::numbers::pi) / 3.f;

        return t == 0.f
            ? 0.f
            : t == 1.f
                ? 1.f
                : -std::pow(2, 10.f * t - 10.f) * fastSin((t * 10.f - 10.75f) * c4);
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, OutElastic)
    {
        constexpr float c4 = (2.f * std::numbers::pi) / 3.f;

        return t == 0.f
            ? 0.f
            : t == 1.f
                ? 1.f
                : std::pow(2, -10.f * t) * fastSin((t * 10.f - 0.75f) * c4) + 1.f;
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, InOutElastic)
    {
        constexpr float c5 = (2.f * std::numbers::pi) / 4.5f;

        return t == 0.f
            ? 0.f
            : t == 1.f
                ? 1.f
                : t < 0.5f
                    ? -(std::pow(2,  20.f * t - 10.f) * fastSin((20.f * t - 11.125f) * c5)) * 0.5f
                    :  (std::pow(2, -20.f * t + 10.f) * fastSin((20.f * t - 11.125f) * c5)) * 0.5f + 1.f;
    }

    static double easeOutBounce(double t)
    {
        constexpr float n1 = 7.5625f;
        constexpr float d1 = 2.75f;

        if (t < 1.f / d1)
            return n1 * t * t;
        else if (t < 2.f / d1)
            return n1 * (t -= 1.5f / d1) * t + 0.75f;
        else if (t < 2.5f / d1)
            return n1 * (t -= 2.25f / d1) * t + 0.9375f;

        return n1 * (t -= 2.625f / d1) * t + 0.984375f;
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, OutBounce)
    {
        return easeOutBounce(t);
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, InOutBounce)
    {
        return t < 0.5f
            ? (1.f - easeOutBounce(1.f - 2.f * t)) * 0.5f
            : (1.f + easeOutBounce(2.f * t - 1.f)) * 0.5f;
    }

    DEFINE_INTERPOLATION_TECHNIQUE(01, Last)
    {
        return -1.0f;
    }

    /////////////////////////////////////////////////////////////
    DEFINE_INTERPOLATION_TECHNIQUE(010, Zero)
    {
        return 0.0f;
    }

    DEFINE_INTERPOLATION_TECHNIQUE(010, InverseQuadratic)
    {
        return (double(a + 1) * 0.2) * (1.0f - 4 * std::pow(t - 0.5f, 2));
    }

    DEFINE_INTERPOLATION_TECHNIQUE(010, Sine)
    {
        return fastSin(double(a + 1) * 0.2) * fastSin(std::numbers::pi * t);
    }

    DEFINE_INTERPOLATION_TECHNIQUE(010, DoublePeakSine)
    {
        return (double(a + 1) * 0.2) * fastSin(std::numbers::pi * std::pow(2 * t - 1, 2 * b + 2));
    }

    DEFINE_INTERPOLATION_TECHNIQUE(010, Last)
    {
        return -1.0f;
    }

    double TechniqueNode::interpolate(double t) const
    {
        if (technique)
        {
            return technique->interpolate(t);
        }
        else
        {
            std::vector<double> partialResults(nodes.size());

            double result = 0.0f;
            for (int i = 0; i < nodes.size(); ++i)
            {
                partialResults[i] = nodes[i]->interpolate(t);
                result += nodes[i]->interpolate(t) * weights[i];
            }

            return result;
        }
    }

    QSharedPointer<TechniqueBase> getInterpolation01(EInterpolation01 type, int a, int b)
    {
        return EInterpolation01Constexpr::UseIn<Interpolation::EAC::MakeTechnique01>(type, a, b);
    }

    QSharedPointer<TechniqueBase> getInterpolation010(EInterpolation010 type, int a, int b)
    {
        return EInterpolation010Constexpr::UseIn<Interpolation::EAC::MakeTechnique010>(type, a, b);
    }

    QSharedPointer<TechniqueNode> getTechniqueNode(const QSharedPointer<TechniqueBase>& inTech)
    {
        return QSharedPointer<Interpolation::TechniqueNode>::create(inTech);
    }

}