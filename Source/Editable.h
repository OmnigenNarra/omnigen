#pragma once
#include "Utils/Event.h"
#include <QEnableSharedFromThis>

class OmnigenDrawable;

enum class EEditableEvents
{
    Created,
    AboutToBeModified,
    Modified,
    AboutToBeDeleted,

    Count // aux
};

class Editable
{
public:
    Editable() = default;
    virtual ~Editable() = default;

    static void initEvents()
    {
        EventMgr.DeclareEvent<EEditableEvents::Created, size_t, QSharedPointer<Editable>>();
        EventMgr.DeclareEvent<EEditableEvents::AboutToBeModified, QSharedPointer<Editable>>();
        EventMgr.DeclareEvent<EEditableEvents::Modified, QSharedPointer<Editable>, bool>();
        EventMgr.DeclareEvent<EEditableEvents::AboutToBeDeleted, QSharedPointer<Editable>>();
    }

    static void blockEvents(bool b)
    {
        EventMgr.BlockEvents(b);
    }

    template<typename T>
    static void created(const QSharedPointer<T>& object)
    {
        EventMgr.TriggerEvent<EEditableEvents::Created>(typeid(T).hash_code(), object.staticCast<Editable>());
    }

    static void created(size_t typeHash, const QSharedPointer<Editable>& object)
    {
        EventMgr.TriggerEvent<EEditableEvents::Created>(typeHash, object);
    }

    static void aboutToBeModified(const QSharedPointer<Editable>& object)
    {
        EventMgr.TriggerEvent<EEditableEvents::AboutToBeModified>(object);
    }

    static void modified(const QSharedPointer<Editable>& object, bool needsPropReset = false)
    {
        EventMgr.TriggerEvent<EEditableEvents::Modified>(object, needsPropReset);
    }

    static void aboutToBeDeleted(const QSharedPointer<Editable>& object)
    {
        EventMgr.TriggerEvent<EEditableEvents::AboutToBeDeleted>(object);
    }

    static inline EventManager<EEditableEvents> EventMgr;
};