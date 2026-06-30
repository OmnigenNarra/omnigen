#pragma once
#include "../AssetBase.h"
#include "../Common/Texture.h"

// Currently only used with Structures?
template<>
struct OmnigenAsset<EAsset::Texture> : OmnigenAssetBase
{
    OmnigenAsset();

    const Texture* operator()(ETextureComponentOut tc) const;

    std::array<int, 2> getDimensions() const;
    const float getUnitsPerPixel() const { return unitsPerPixel; }
    const float getMaxDisplacement() const { return maxDisplacement; }

    void setUnitsPerPixel(float inUUP);
    void setMaxDisplacement(float inMaxDisplacement);

    virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    virtual void makeUniqueName() override;

    void createOutputs();

    static std::vector<QSharedPointer<OmnigenAssetBase>> newAsset();

    std::map<ETextureComponentOut, Texture> outputs;

protected:
    float unitsPerPixel = 1.0f;
    float maxDisplacement = 10.0f;

    FRIEND_OMNIBIN(OmnigenAsset);
    friend class QAssetCompilerDialog;
};

using OmnigenAsset_Texture = OmnigenAsset<EAsset::Texture>;

void omniSave(const OmnigenAsset_Texture& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenAsset_Texture& object, OmniBin<std::ios::in>& omniBin);