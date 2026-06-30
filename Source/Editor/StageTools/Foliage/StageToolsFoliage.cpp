#include "stdafx.h"
#include "StageToolsFoliage.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/Foliage/StageGeneration_Foliage.h"
#include "Scene/Generation/Stages/Foliage/PlantDrawable.h"
#include "Data/Assets/Plant/AssetPlant.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "FoliageSelection.h"

namespace Design
{
    StageTools<EGenerationStage::Foliage>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::Foliage>::getSelectionMgr() const
    {
        return FoliageSelectionMgr::get();
    }

    void StageTools<EGenerationStage::Foliage>::bind()
    {
        StageToolsBase::bind();
    }

    void StageTools<EGenerationStage::Foliage>::unbind()
    {
        StageToolsBase::unbind();
    }

    struct PlantSerializableData
    {
        std::vector /*plant variations*/<std::vector /*instances*/<MeshAssetInstanceData>> variationInstances;
        QString assetName;
    };

    void omniSave(const PlantSerializableData& object, OmniBin<std::ios::out>& omniBin)
    {
        omniBin << object.variationInstances;
        omniBin << object.assetName;
    }

    void omniLoad(PlantSerializableData& object, OmniBin<std::ios::in>& omniBin)
    {
        omniBin >> object.variationInstances;
        omniBin >> object.assetName;
    }

    using PlantInstanceMap = std::unordered_map<qint64 /*plant id*/, PlantSerializableData>;

    void StageTools<EGenerationStage::Foliage>::save(OmniBin<std::ios::out>& writer) const
    {
        using namespace Generation;

        PlantInstanceMap plantData;
        for (auto&& plantDrawable : StageGen<EGenerationStage::Foliage>::createdPlants)
        {
            auto [asset, variantIdx] = plantDrawable->getAssetInfo();
            auto&& assetData = plantData[asset->id];
            
            assetData.assetName = asset->name;
            assetData.variationInstances.resize(asset->getMeshes().size());
            assetData.variationInstances[variantIdx] = asset->getMeshes()[variantIdx].getGeometry().at(ELOD::Zero)->instanceData;
        }

        writer << plantData;
    }

    void StageTools<EGenerationStage::Foliage>::load(OmniBin<std::ios::in>& reader)
    {
        using namespace Generation;

        PlantInstanceMap plantData;
        reader >> plantData;

        auto availableAssetIds = QOmnigenAssetMgrSection::getAssetsIds<EAsset::Plant>(true);
        std::vector<qint64> ids;
        std::unordered_map<qint64, std::set<int>> usedModels;
        for (auto&& [id, data] : plantData)
        {
            if (!contains(availableAssetIds, id))
            {
                OmniLog(ELoggingLevel::Warn) << "Plant asset [" << data.assetName <<= "] not found. Skipping.";
                continue;
            }

            ids << id;

            for (int varIdx = 0; varIdx < data.variationInstances.size(); ++varIdx)
                if (!data.variationInstances[varIdx].empty())
                    usedModels[id].insert(varIdx);
        }

        QOmnigenAssetMgrSection::get()->forceLoadAssets(EAsset::Plant, ids);
        auto&& plants = QOmnigenAssetMgrSection::getAssets<EAsset::Plant>(true);
        
        for (auto&& [id, variations] : usedModels)
            for (int varIdx : variations)
                plants[id]->getMeshes()[varIdx].getGeometry().at(ELOD::Zero)->instanceData = plantData[id].variationInstances[varIdx];

        StageGen<EGenerationStage::Foliage>::spawnUsedModels(usedModels);
        DPlant::createResources(StageGen<EGenerationStage::Foliage>::createdPlants);
    }
}