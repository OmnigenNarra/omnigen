#pragma once
#include "Data/Assets/AssetBase.h"

// Section displaying assets and interfacing with them.
// On launch, meta data files are read to know whether matching assets exist
// During runtime, assets are loaded in the background or on demand when interacted with.

class FlowLayout;

using AssetMeta = std::pair<EAsset, qint64>;

// Small widget representing a single asset in the section, consists of preview (not implemented) and name
class QAssetTile : public QFrame
{
    Q_OBJECT

public:
    QAssetTile(const QSharedPointer<OmnigenAssetBase>& inAsset);

    const auto& getAsset() const { return asset; }
    bool isSelected() const { return bSelected; }

    void setIsSelected(bool b);

    static inline const int tileSize = 100;

signals:
    void selected(qint64);
    void contextMenuRequested(QMouseEvent*, qint64);

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QSharedPointer<OmnigenAssetBase> asset;
    bool bSelected = false;
    QLineEdit* nameWidget = nullptr;
};

// A tile list for accessing asset data.
// LMB: Selects asset (Properties exposed in Properties Section)
// RMB: Context menu
// Assets can be renamed after selecting
class QOmnigenAssetMgrSection : public QWidget
{
    Q_OBJECT

public:
    static QOmnigenAssetMgrSection* get();
    QOmnigenAssetMgrSection(QWidget* parent);

    void createNewAsset();
    void duplicateAsset(const OmnigenAssetBase& asset, QString targetDir = gAssetsPath) const;
    void addAssets(const std::vector<QSharedPointer<OmnigenAssetBase>>& newAssets);
    void clear();
    void loadMetadata();
    void assetLoaderTick();
    void forceLoadAssets(EAsset type, const std::vector<qint64>& ids);
    void forceLoadAssets(const std::vector<AssetMeta>& meta);

    template<EAsset A>
    static std::unordered_map<qint64, QSharedPointer<OmnigenAsset<A>>> getAssets(bool includeDisabled = false)
    {
        std::unordered_map<qint64, QSharedPointer<OmnigenAsset<A>>> results;
        std::vector<qint64> assetsToLoad;
        for (auto&& [id, asset] : assets[A])
        {
            bool enabled = isAssetEnabled(id);
            if (enabled)
                assetsToLoad << id;

            if (enabled || includeDisabled)
                results[id] = asset.staticCast<OmnigenAsset<A>>();
        }

        get()->forceLoadAssets(A, assetsToLoad);
        return results;
    }

    static const auto& getAssets()
    {
        return assets;
    }

    template<EAsset A>
    static std::vector<qint64> getAssetsIds(bool includeDisabled = false)
    {
        std::vector<qint64> results;
        results.reserve(assets[A].size());

        for(auto&& [id, asset] : assets[A])
            if (includeDisabled || isAssetEnabled(id))
                results << id;

        return results;
    }

    // Each new asset is enabled by default, each project keeps a set of disabled assets.
    // Probably should be replaced with a list of enabled assets created on launch and updated on save.
    static void setAssetEnabled(qint64 id, bool enabled);
    static bool isAssetEnabled(qint64 id);

    static const auto& getDisabledAssets() { return disabledAssets; }
    static void syncDisabledAssets();

    // Stub of a system to notify about unsaved assets
    static auto getDirtyAssets()
    {
        std::vector<QSharedPointer<OmnigenAssetBase>> results;
        for (auto&& [type, ids] : dirtyAssets)
            for (auto id : ids)
                results.push_back(assets.at(type).at(id));

        return results;
    }

    static void markAssetDirty(const OmnigenAssetBase& asset)
    {
        dirtyAssets[asset.type].insert(asset.id);
    }

private:
    void rebuildLayout(EAsset assetType = EAsset::Last);
    QWidget* makeAddAssetButton();
    QAssetTile* makeAssetTile(const QSharedPointer<OmnigenAssetBase>& inAsset);
    void selectAsset(qint64 id);
    void createContextMenu(QMouseEvent* event, qint64 id);
    void removeAsset(EAsset type, qint64 id);

    QTimer assetLoaderTimer;
    bool isForceLoading = false;

    QVBoxLayout* tabLayout = nullptr;
    QTabWidget* tabElement = nullptr;

    std::array<FlowLayout*, magic_enum::enum_count<EAsset>() - 1> flowLayouts;
    
    inline static std::map<EAsset, std::unordered_map<qint64, QSharedPointer<OmnigenAssetBase>>> assets = {};
    inline static std::map<EAsset, std::unordered_set<qint64>> dirtyAssets;
    inline static std::map<EAsset, std::unordered_map<qint64, QAssetTile*>> assetTiles = {};
    inline static std::unordered_set<qint64> disabledAssets = {};
};