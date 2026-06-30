#pragma once
#include <Editor/Sections/History/History.h>
#include "Utils/magic_enum.hpp"
#include "Utils/CoreUtils.h"
#include "FieldImplBase.h"

// For simplest example, refer to LineEditField

// Field interface
struct OmnigenFieldBase
{
    virtual ~OmnigenFieldBase() = default;
    
    // A Field is typically made of 2 widgets: label and input
    virtual QWidget* createFieldLabelWidget();
    virtual QWidget* createFieldInputWidget() = 0;

    // Sync with data
    virtual void update() = 0;

    QString label;
};

// Undo / Redo support
template<typename T>
struct FieldHistoryMgr
{
    static FieldHistoryMgr& get()
    {
        static FieldHistoryMgr instance;
        return instance;
    }

    using ValueGetter = std::function<T()>;
    using ValueSetter = std::function<bool(T)>;

    // @returns false if field was invalidated / destroyed
    bool setValue(const ValueGetter& Get, const ValueSetter& Set, const T& fieldValue)
    {
        if (Get && (Get() == fieldValue))
            return true;

        HISTORY_PUSH(setValue, ValueGetter(), Set, T());

        T hOldValue, hNewValue;
        if (!HISTORY_LOAD2(hOldValue, hNewValue))
        {
            hOldValue = Get();
            hNewValue = fieldValue;
            HISTORY_SAVE2(hOldValue, hNewValue);
        }

        return Set(hNewValue);
    }

    // @returns false if field was invalidated / destroyed
    bool setValue_Undo(const ValueGetter&, const ValueSetter& Set, const T&)
    {
        HISTORY_POP();

        T hOldValue;
        HISTORY_LOAD(hOldValue);

        return Set(hOldValue);
    }
};

enum class EContainerAction
{
    Add,
    Edit,
    Remove
};

template<typename Key, typename Value>
struct MultiFieldHistoryMgr
{
    static MultiFieldHistoryMgr& get()
    {
        static MultiFieldHistoryMgr instance;
        return instance;
    }

    using ValueGetter = std::function<Value(const Key&)>;
    using KeySetter = std::function<bool(const Key&, const Key&)>;
    using ValueSetter = std::function<bool(const Key&, const Value&, EContainerAction)>;

    // @returns false if field was invalidated / destroyed
    bool setKey(const KeySetter& Set, const Key& oldKey, const Key& newKey)
    {
        if (oldKey == newKey)
            return true;

        HISTORY_PUSH(setKey, ValueGetter(), Set, oldKey, newKey);

        return Set(oldKey, newKey);
    }

    // @returns false if field was invalidated / destroyed
    bool setKey_Undo(const KeySetter& Set, const Key& oldKey, const Key& newKey)
    {
        HISTORY_POP();

        return Set(newKey, oldKey);
    }

    // @returns false if field was invalidated / destroyed
    bool setValue(const ValueGetter& Get, const ValueSetter& Set, const Key& key, const Value& newValue, EContainerAction ca)
    {
        if (Get && (Get(key) == newValue))
            return true;

        HISTORY_PUSH(setValue, ValueGetter(), Set, key, newValue, ca);

        Value hOldValue;
        if (!HISTORY_LOAD(hOldValue))
        {
            hOldValue = Get(key);
            HISTORY_SAVE(hOldValue);
        }

        return Set(key, newValue, ca);
    }

    // @returns false if field was invalidated / destroyed
    bool setValue_Undo(const ValueGetter&, const ValueSetter& Set, const Key& key, const Value&, EContainerAction ca)
    {
        HISTORY_POP();

        Value hOldValue;
        HISTORY_LOAD(hOldValue);

        return Set(key, hOldValue, ca);
    }
};

// Only QObjects can use signals, so here we are.
class FieldConnector : public QObject
{
    Q_OBJECT

public:
    template<typename T, typename L>
    void bindSetter(T* field, L lambda)
    {
        connect(field, &QFieldImplBase::valueChanged, this, lambda);
    }

    template<typename T, typename S, typename L>
    void bindAnything(T* object, S signal, L lambda)
    {
        connect(object, signal, this, lambda);
    }
};

// These are used in multi-value fields
template<typename T>
struct FieldTraits
{
    using ValueType = T;
};

template<typename T, size_t N>
struct FieldTraits<std::array<T, N>>
{
    static constexpr inline int staticSize = N;
    using KeyType = size_t;
    using ValueType = T;
};

template<typename T>
struct FieldTraits<std::vector<T>>
{
    static constexpr inline int staticSize = -1;
    using KeyType = size_t;
    using ValueType = T;
};

