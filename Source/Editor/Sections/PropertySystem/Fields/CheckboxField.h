#pragma once
#include <QCheckBox>
#include "FieldImplBase.h"

template<typename T>
class CheckBoxField : public QFieldImplBase
{
    static_assert(std::is_same_v<T, bool>);

public:
    CheckBoxField()
        : checkBox(new QCheckBox)
    {
        setLayout(new QHBoxLayout);
        layout()->addWidget(checkBox);
        QObject::connect(checkBox, &QCheckBox::stateChanged, this, [&](int) { emit valueChanged(); });
    }

    void set(const T& newValue)
    {
        checkBox->setChecked(newValue);
    }

    T get()
    {
        return checkBox->isChecked();
    }

private:
    QCheckBox* checkBox = nullptr;
};