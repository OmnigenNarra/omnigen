#pragma once
#include <QLabel>
#include "Utils/CoreUtils.h"
#include "FieldImplBase.h"

// This field only displays values
template<typename T>
class ConstField : public QFieldImplBase
{
public:
    ConstField()
        : label(new QLabel)
    {
        setLayout(new QHBoxLayout);
        layout()->addWidget(label);
    }

    void set(const T& value)
    {
        label->setText(toQString(value));
    }

    T get() const
    {
        return fromQString<T>(label->text());
    }

protected:
    QLabel* label = nullptr;
};