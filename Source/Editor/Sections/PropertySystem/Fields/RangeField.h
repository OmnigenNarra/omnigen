#pragma once
#include "Utils/Widgets/RangeSlider.h"
#include "Utils/CoreUtils.h"
#include "FieldImplBase.h"

template<typename T>
class RangeField : public QFieldImplBase
{
    using V = T::value_type;

public:
    int convertToSliderValue(V realValue)
    {
        if constexpr (std::is_enum_v<V> || std::is_same_v<V, int>)
        {
            return static_cast<int>(realValue);
        }
        else if constexpr (std::is_same_v<V, float>)
        {
            return int(std::round(realValue / step));
        }

        Q_ASSERT(false);
        return -1;
    }

    V convertFromSliderValue(int sliderValue)
    {
        if constexpr (std::is_enum_v<V> || std::is_same_v<V, int>)
        {
            return static_cast<V>(sliderValue);
        }
        else if constexpr (std::is_same_v<V, float>)
        {
            return float(sliderValue * step);
        }

        Q_ASSERT(false);
        return {};
    }

    RangeField(V min, V max, float inStep = 1.0f)
        : rangeSlider(new RangeSlider)
        , step(inStep)
    {
        setLayout(new QHBoxLayout);
        layout()->addWidget(rangeSlider);
        QObject::connect(rangeSlider, &RangeSlider::sliderMoved, this, [&](int, int) { emit QFieldImplBase::valueChanged(); });

        rangeSlider->setMinimum(convertToSliderValue(min));
        rangeSlider->setMaximum(convertToSliderValue(max));
        rangeSlider->setTickInterval(1);

        // Label setup
        int maxLen = 0;
        for (int i = rangeSlider->minimum(); i <= rangeSlider->maximum(); ++i)
        {
            rangeSlider->labels[i] = toQString(convertFromSliderValue(i));
            if (int ls = rangeSlider->labels.at(i).size(); ls > maxLen)
            {
                maxLen = ls;
                rangeSlider->longestLabelKey = i;
            }
        }
    }

    void set(const T& newValues)
    {
        rangeSlider->setLow(convertToSliderValue(newValues[0]));
        rangeSlider->setHigh(convertToSliderValue(newValues[1]));
    }

    T get()
    {
        return { convertFromSliderValue(rangeSlider->low()), convertFromSliderValue(rangeSlider->high()) };
    }

protected:
    RangeSlider* rangeSlider = nullptr;
    const float step;
};