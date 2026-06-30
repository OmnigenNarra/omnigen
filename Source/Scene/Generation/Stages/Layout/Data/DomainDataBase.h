#pragma once
#include "Utils/EnumAsConstexpr.h"
#include <QSharedPointer>
#include "Utils/OmniBin/OmniBinQt.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertyList.h"

class DDomain;

enum class EDomainType
{
    Terrain,
    Biome,
    Water,

    Last // unused
};
ENABLE_ENUM_AS_CONSTEXPR(EDomainType, EDomainType::Last);

struct DomainDataBase
{
    virtual void fillProps(QSharedPointer<OmnigenPropertyListBase> props);
    virtual QString makeName(bool isInitial = false);
    static QSharedPointer<DDomain> getOwner(qint64 id);

    // DATA
    QString name;

    FRIEND_OMNIBIN(DomainDataBase);
};

inline void omniSave(const DomainDataBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.name;
}

inline void omniLoad(DomainDataBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.name;
}

template<EDomainType DT>
struct DomainData : DomainDataBase
{
    virtual void fillProps(QSharedPointer<OmnigenPropertyListBase> props) {};
};

namespace EAC
{
    struct CreateDomainData
    {
        template<EDomainType DT>
        static QSharedPointer<DomainDataBase> Action()
        {
            auto data = QSharedPointer<DomainData<DT>>::create();
            data->name = data->makeName(true);
            return data;
        }
    };
}

#undef getSelf