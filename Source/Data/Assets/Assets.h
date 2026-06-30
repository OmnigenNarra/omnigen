#pragma once
#include "Texture/AssetTexture.h"
#include "Structure/AssetStructure.h"
#include "Plant/AssetPlant.h"
#include "RockMaterial/AssetRockMaterial.h"
#include "SoilMaterial/AssetSoilMaterial.h"
static_assert(int(EAsset::Last) == __LINE__ - 2); // Must include all asset classes

#include <tbb/task.h>

namespace EAC
{
    struct CreateAsset
    {
        template<EAsset A>
        static std::vector<QSharedPointer<OmnigenAssetBase>> Action()
        {
            return OmnigenAsset<A>::newAsset();
        }
    };

    struct SaveAsset
    {
        template<EAsset A>
        static void Action(const QSharedPointer<const OmnigenAssetBase>& asset, const QString& dir = gAssetsPath)
        {
            // Save metadata
            QString metadataPath = dir + asset->name + ".meta";
            OmniBin<std::ios::out> metaWriter(metadataPath.toStdString());
            metaWriter << A;
            metaWriter << *asset;

            // Save asset data
            QString assetPath = dir + asset->name + ".oas";
            OmniBin<std::ios::out> assetWriter(assetPath.toStdString());
            assetWriter << static_cast<const OmnigenAsset<A>&>(*asset);
        }
    };

    struct LoadAsset
    {
        // Holding mutex* so exiting app doesn't crash due to mutexes being in use
        static inline std::unordered_map<qint64, std::mutex*> assetLoadGuards;

        template<EAsset A>
        static void Action(const QSharedPointer<OmnigenAssetBase>& asset)
        {
            // Prevent multiple threads from loading the same asset
            std::scoped_lock lock(*assetLoadGuards[asset->id]);

            if (asset->isLoaded)
                return;

            // Load asset data
            OmniBin<std::ios::in> assetReader(asset->dataPath.toStdString());
            assetReader >> static_cast<OmnigenAsset<A>&>(*asset);
            asset->isLoaded = true;
        }
    };

    // TBB task wrapper for LoadAsset action
    template<EAsset A>
    struct LoadAssetTask : public tbb::task
    {
        QSharedPointer<OmnigenAssetBase> asset;

        LoadAssetTask(const QSharedPointer<OmnigenAssetBase>& inAsset)
            : tbb::task()
            , asset(inAsset)
        {}

        tbb::task* execute() override
        {
            LoadAsset::Action<A>(asset);
            return nullptr;
        }
    };

    struct CreateLoadAssetTask
    {
        template<EAsset A>
        static tbb::task* Action(const QSharedPointer<OmnigenAssetBase>& asset)
        {
            return new (tbb::task::allocate_root()) LoadAssetTask<A>(asset);
        }
    };

    // Used in asset loading
    struct InitializeAssetFromMetadata
    {
        template<EAsset A>
        static QSharedPointer<OmnigenAssetBase> Action(OmniBin<std::ios::in>& reader, QString&& metaPath)
        {
            // Create full final object, fill only base info.
            auto asset = QSharedPointer<OmnigenAsset<A>>::create();
            reader >> static_cast<OmnigenAssetBase&>(*asset);

            // Fill additional data
            asset->type = A;
            asset->isLoaded = false;
            asset->dataPath = metaPath.replace(".meta", ".oas");

            // Init asset loading guard
            LoadAsset::assetLoadGuards[asset->id] = new std::mutex();
            return asset;
        }
    };
}