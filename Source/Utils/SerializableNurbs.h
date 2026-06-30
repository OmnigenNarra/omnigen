#pragma once

#include <Mathematics/NURBSSurface.h>

template <int N, typename Real>
class SerializableNurbs : public gte::NURBSSurface<N, Real>
{
public:   
    struct Input
    {
        gte::BasisFunctionInput<Real> uInput = gte::BasisFunctionInput<Real>(2, 1);
        gte::BasisFunctionInput<Real> vInput = gte::BasisFunctionInput<Real>(2, 1);
        std::vector<gte::Vector<N, Real>> mControls;
        std::vector<Real> weights;
    };

    SerializableNurbs(const Input& input = Input()) 
        : gte::NURBSSurface<N, Real>(input.uInput, input.vInput, input.mControls.data(), input.weights.data())
        , input0(input.uInput)
        , input1(input.vInput)
    {
        this->input0 = input.uInput;
        this->input1 = input.vInput;
    }
    
    SerializableNurbs(const SerializableNurbs& ns)
        : gte::NURBSSurface<N, Real>(ns.input0, ns.input1, ns.mControls.data(), ns.mWeights.data())
        , input0(ns.input0)
        , input1(ns.input1)
    {
    }

    SerializableNurbs& operator = (const SerializableNurbs& ns)
    {
        this->input0 = ns.input0;
        this->input1 = ns.input1;

        gte::BasisFunctionInput<Real> const* input[2] = { &input0, &input1 };
        for (int i = 0; i < 2; ++i)
            this->mBasisFunction[i].Create(*input[i]);
        
        this->mNumControls = ns.mNumControls;
        this->mUMin = ns.mUMin;
        this->mUMax = ns.mUMax;
        this->mVMin = ns.mVMin;
        this->mVMax = ns.mVMax;
        this->mControls = ns.mControls;
        this->mWeights = ns.mWeights;
        this->mConstructed = true;

        return *this;
    }


private:
    gte::BasisFunctionInput<Real> input0;
    gte::BasisFunctionInput<Real> input1;
    FRIEND_OMNIBIN(SerializableNurbs);
};

template<int N, typename Real>
inline void omniSave(const SerializableNurbs<N, Real>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.GetNumControls(0) << object.GetNumControls(1);
    omniBin << object.GetBasisFunction(0).GetDegree() << object.GetBasisFunction(1).GetDegree();

    int size = object.GetNumControls(0) * object.GetNumControls(1);
    for (int i = 0; i < size; ++i)
        omniBin << object.GetControls()[i] << object.GetWeights()[i];
}

template<int N, typename Real>
inline void omniLoad(SerializableNurbs<N, Real>& object, OmniBin<std::ios::in>& omniBin)
{
    int sizeU, sizeV;
    omniBin >> sizeU >> sizeV;

    int degU, degV;
    omniBin >> degU >> degV;

    std::vector<gte::Vector<3, float>> mControls(sizeU * sizeV);
    std::vector<float> weights(mControls.size());
    for (size_t i = 0; i < mControls.size(); ++i)
        omniBin >> mControls[i] >> weights[i];

    gte::BasisFunctionInput<float> uInput(sizeU, degU);
    gte::BasisFunctionInput<float> vInput(sizeV, degV);
    SerializableNurbs<3, float>::Input input{ std::move(uInput), std::move(vInput), std::move(mControls), std::move(weights) };

    object.~SerializableNurbs();
    new (&object) SerializableNurbs<3, float>(input);
}