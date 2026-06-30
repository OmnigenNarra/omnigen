#pragma once
#include <QSlider>
#include "FieldImplBase.h"

template<typename T>
class SliderField : public QFieldImplBase
{
public:
    SliderField(T min, T max, T step = 1)
        : slider(new QSlider(Qt::Horizontal))
    {
        setLayout(new QHBoxLayout);
        layout()->addWidget(slider);
        QObject::connect(slider, &QSlider::valueChanged, this, [&](int) { emit valueChanged(); });
        slider->setMinimum(min);
        slider->setMaximum(max);
        slider->setSingleStep(step);
        slider->setTracking(false);
    }

    void set(const T& newValue)
    {
        slider->setValue(newValue);
    }

    T get()
    {
        return slider->value();
    }

private:
    QSlider* slider = nullptr;
};