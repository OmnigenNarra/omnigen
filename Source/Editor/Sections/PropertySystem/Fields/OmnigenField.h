#pragma once
#include "OmnigenFieldBase.h"
#include "LineEditField.h"
#include "ConstField.h"
#include "Utils/Widgets/Spoiler.h"

// Main single-value field implementation
template<typename DataType, typename InputWidget = LineEditField<DataType>>
struct TOmnigenField : OmnigenFieldBase
{
    using Getter = std::function<DataType()>;
    using Setter = std::function<bool(DataType)>;
    using InputMaker = std::function<InputWidget*()>;

    TOmnigenField(const QString& inLabel, const Getter& inGetter, const Setter& inSetter, const InputMaker& inInputMaker = []() { return new InputWidget(); })
        : Get(inGetter)
        , Set(inSetter)
        , MakeInput(inInputMaker)
    {
        label = inLabel;
    }

    virtual QWidget* createFieldInputWidget() override
    {
        inputField = MakeInput();
        fc.bindSetter(inputField, [this]()
            {
                if (!FieldHistoryMgr<DataType>::get().setValue(Get, Set, inputField->get()))
                    return;

                inputField->blockSignals(true);
                inputField->clearFocus();
                inputField->blockSignals(false);
            });

        return inputField;
    };

    virtual void update() override
    {
        inputField->set(Get());
    }

    Getter Get;
    Setter Set;
    InputMaker MakeInput;
    InputWidget* inputField = nullptr;
    FieldConnector fc;
};

template<
    typename DataType,
    template<typename> typename InputWidget = LineEditField,
    template<typename> typename KeyInputWidget = ConstField
>
struct TOmnigenMultiField : OmnigenFieldBase
{
    using ValueType = typename FieldTraits<DataType>::ValueType;
    using InputMaker = std::function<InputWidget<ValueType>* ()>;

    using KeyType = typename FieldTraits<DataType>::KeyType;
    using KeyInputMaker = std::function<KeyInputWidget<KeyType>* ()>;

    using ValueGetter = std::function<ValueType(const KeyType&)>;
    using KeyGetter = std::function<std::vector<KeyType>()>;

    using KeySetter = std::function<bool(const KeyType&, const KeyType&)>;
    using ValueSetter = std::function<bool(const KeyType&, const ValueType&, EContainerAction)>;

    struct SingleInput
    {
        KeyInputWidget<KeyType>* keyWidget = nullptr;
        InputWidget<ValueType>* valueWidget = nullptr;
    };

    TOmnigenMultiField(const QString& inLabel,
        const KeyGetter& inKeyGetter,
        const ValueGetter& inGetter,
        const ValueSetter& inSetter,
        const KeySetter& inKeySetter = [](const KeyType&, const KeyType&) { return true; },
        const InputMaker& inInputMaker = []() { return new InputWidget<ValueType>(); },
        const KeyInputMaker& inKeyInputMaker = []() { return new KeyInputWidget<KeyType>(); }
    )
        : GetValue(inGetter)
        , GetKeys(inKeyGetter)
        , SetValue(inSetter)
        , SetKey(inKeySetter)
        , MakeInput(inInputMaker)
        , MakeKeyInput(inKeyInputMaker)
    {
        label = inLabel;
    }

    virtual QWidget* createFieldInputWidget() override
    {
        fields.clear();
        auto keys = GetKeys();

        masterValueWidget = new Spoiler(QString::asprintf("%d elements", keys.size()));
        auto gridLayout = new QGridLayout();

        for (int i=0; i<keys.size(); ++i)
        {
            auto&& key = keys[i];
            auto&& field = fields[key];

            field.keyWidget = MakeKeyInput();
            field.valueWidget = MakeInput();

            gridLayout->addWidget(field.keyWidget, i, 0);

            if constexpr (FieldTraits<DataType>::staticSize > 0)
            {
                gridLayout->addWidget(field.valueWidget, i, 1);
            }
            else
            {
                auto* auxWidget = new QWidget();
                auto* auxLay = new QHBoxLayout();
                auxWidget->setLayout(auxLay);

                auxLay->addWidget(field.valueWidget);
                auxLay->addWidget(makeRemoveButton(i));

                gridLayout->addWidget(auxWidget, i, 1);
            }

            fc.bindSetter(field.valueWidget, [this, key]()
                {
                    if (!MultiFieldHistoryMgr<KeyType, ValueType>::get().setValue(GetValue, SetValue, key, fields[key].valueWidget->get(), EContainerAction::Edit))
                        return;

                    auto&& field = fields[key];
                    field.valueWidget->blockSignals(true);
                    field.valueWidget->clearFocus();
                    field.valueWidget->blockSignals(false);

                    masterValueWidget->expandImmediately();
                });
        }

        if constexpr (FieldTraits<DataType>::staticSize < 0)
        {
            gridLayout->addWidget(makeAddButton(), gridLayout->rowCount(), 1);
        }

        masterValueWidget->setContentLayout(gridLayout);

        return masterValueWidget;
    };

    virtual void update() override
    {
        auto keys = GetKeys();

        for (auto&& key : keys)
        {
            auto&& field = fields[key];
            field.keyWidget->set(key);
            field.valueWidget->set(GetValue(key));
        }
    }

    QWidget* makeAddButton()
    {
        auto* button = new QPushButton("+");
        fc.bindAnything(button, &QPushButton::clicked, [this]()
            {
                SetValue(KeyType(), ValueType(), EContainerAction::Add);
            });

        return button;
    }

    QWidget* makeRemoveButton(int fieldIdx)
    {
        auto* button = new QPushButton("X");
        fc.bindAnything(button, &QPushButton::clicked, [this, fieldIdx]()
            {
                auto key = fields[fieldIdx].keyWidget->get();
                SetValue(key, ValueType(), EContainerAction::Remove);
            });

        return button;
    }

    ValueGetter GetValue;
    KeyGetter GetKeys;

    ValueSetter SetValue;
    KeySetter SetKey;

    InputMaker MakeInput;
    KeyInputMaker MakeKeyInput;

    Spoiler* masterValueWidget = nullptr;
    QMap<KeyType, SingleInput> fields;
    FieldConnector fc;
};