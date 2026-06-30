#pragma once
#include "../AssetBase.h"
#include "../Common/Texture.h"

class QMultiAssetCompilerMainWindow;

template<>
struct OmnigenAsset<EAsset::RockMaterial> : OmnigenAssetBase
{
    OmnigenAsset();

    virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    virtual void makeUniqueName() override;

    static std::vector<QSharedPointer<OmnigenAssetBase>> newAsset();
    const auto& getHardness() const { return hardness; };
    const auto& getMinSize() const { return minSize; };
    const auto& getTextures() const { return materials; };

private:
    int hardness = 1;
    int minSize = 10;

    // Each RockMaterial consts of 4 slots. 
    // 0: Solid Rock
    // 1: Gravel
    // 2: Fine-grained rock / soil / sand
    // 3: Cliff face
    std::array<Material, 4> materials;

    FRIEND_OMNIBIN(OmnigenAsset<EAsset::RockMaterial>);
    friend class QMultiAssetCompilerMainWindow;
};

using OmnigenAsset_RockMaterial = OmnigenAsset<EAsset::RockMaterial>;

void omniSave(const OmnigenAsset_RockMaterial& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenAsset_RockMaterial& object, OmniBin<std::ios::in>& omniBin);