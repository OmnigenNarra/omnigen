#pragma once
#include <QComboBox>
#include <QStandardItemModel>
#include "FieldImplBase.h"

template<typename T>
class ComboFieldEdit : public QFieldImplBase
{
public:
    ComboFieldEdit(const std::vector<T>& inOptions, const std::vector<bool>& inOptionsEnabled = {})
        : comboBox(new QComboBox)
        , options(inOptions)
        , optionsEnabled(inOptionsEnabled)
    {
        setLayout(new QHBoxLayout);
        layout()->addWidget(comboBox);
        QObject::connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { valueChanged(); });

        // Temporary workaround to make the popup appear below the ComboBox
        comboBox->setEditable(true);
        comboBox->lineEdit()->setReadOnly(true);

        for (auto&& option : options)
            comboBox->addItem(toQString(option));

        if (!optionsEnabled.empty())
        {
            QStandardItemModel* model = static_cast<QStandardItemModel*>(comboBox->model());

            for (int i = 0; i < optionsEnabled.size(); ++i)
                if (!optionsEnabled[i])
                    model->item(i)->setFlags(model->item(i)->flags() & ~Qt::ItemIsEnabled);
        }
    }

    void set(const T& newValue)
    {
        blockSignals(true);
        comboBox->setCurrentIndex(indexOf(options, newValue));
        blockSignals(false);
    }

    T get()
    {
        return options[comboBox->currentIndex()];
    }

protected:
    QComboBox* comboBox = nullptr;
    std::vector<T> options;
    std::vector<bool> optionsEnabled;
};

template<typename T>
std::vector<T> gatherEnumsForComboField()
{
    std::vector<T> result;

    auto enumValues = magic_enum::enum_values<T>();
    result.resize(enumValues.size());

    for (int i = 0; i < result.size(); ++i)
        result[i] = enumValues[i];

    if (magic_enum::enum_name(result.back()) == "Last")
        result.pop_back();

    return result;
}

template<typename T>
class ComboFieldEditTexSlot : public ComboFieldEdit<T>
{
};

template<>
class ComboFieldEditTexSlot<qint64> : public ComboFieldEdit<qint64>
{
public:
    static const QString& findTexName(qint64 guid);

    ComboFieldEditTexSlot(const std::vector<qint64>& inOptions, bool useThisInsteadOfBaseClass)
        : ComboFieldEdit<qint64>(inOptions)
    {
        // Temporary workaround to make the popup appear below the ComboBox
        comboBox->setEditable(true);
        comboBox->lineEdit()->setReadOnly(true);

        for (auto&& option : options)
            comboBox->addItem(findTexName(option));
    }
};