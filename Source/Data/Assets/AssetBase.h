#pragma once
#include <QString>
#include <optional>
#include "Utils/OmniBin/OmniBinQt.h"
#include "Utils/EnumAsConstexpr.h"
#include "Utils/Event.h"

class OmnigenPropertyListBase;

// All asset types declared here.
enum class EAsset
{
    Texture,
    Structure,
    Plant,
    RockMaterial,
    SoilMaterial,
    Last
};
ENABLE_ENUM_AS_CONSTEXPR(EAsset, EAsset::Last);

static const QString gAssetsPath = "Resources/Assets/";

enum class EAssetEvents
{
    Modified,
    Count
};

// Not abstract for loading purposes only
struct OmnigenAssetBase : public QEnableSharedFromThis<OmnigenAssetBase>
{
    OmnigenAssetBase();
    virtual ~OmnigenAssetBase() = default;

    virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() { Q_ASSERT(false); return nullptr; };
    virtual void makeUniqueName() { Q_ASSERT(false); };

    static QString getUniqueName(const QString& name);

    qint64 id;
    QString name;
    QString dataPath;
    EAsset type;
    bool isLoaded = true;

    FRIEND_OMNIBIN(OmnigenAssetBase)

    static inline EventManager<EAssetEvents> eventMgr;
};

void omniSave(const OmnigenAssetBase& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenAssetBase& object, OmniBin<std::ios::in>& omniBin);

// Template impl declaration
template<EAsset A>
struct OmnigenAsset : OmnigenAssetBase
{
    static std::vector<QSharedPointer<OmnigenAssetBase>> newAsset() { return {}; }
};