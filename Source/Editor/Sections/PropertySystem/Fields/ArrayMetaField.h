#pragma once
#include <QHBoxLayout>

// Produces COUNT BaseField's in a single line
template<typename BaseField, size_t COUNT>
class ArrayField : public QFieldImplBase
{
public:
    using ValueType = decltype(std::declval<BaseField>().get());

    ArrayField()
    {
        setLayout(new QHBoxLayout());

        for (int i = 0; i < COUNT; ++i)
        {
            inputs[i] = new BaseField();
            layout()->addWidget(inputs[i]);
            connect(inputs[i], &BaseField::valueChanged, this, &QFieldImplBase::valueChanged);
        }
    }

    void set(const std::array<ValueType, COUNT>& newValues)
    {
        for (int i = 0; i < COUNT; ++i)
            inputs[i]->set(newValues[i]);
    }

    std::array<ValueType, COUNT> get()
    {
        std::array<ValueType, COUNT> result;
        for (int i = 0; i < COUNT; ++i)
            result[i] = inputs[i]->get();

        return result;
    }

protected:
    std::array<BaseField*, COUNT> inputs;
};