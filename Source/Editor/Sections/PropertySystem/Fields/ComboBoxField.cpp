#include "stdafx.h"
#include "ComboBoxField.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"

const QString& ComboFieldEditTexSlot<qint64>::findTexName(qint64 guid)
{
    auto&& textures = QOmnigenAssetMgrSection::getAssets<EAsset::Texture>();
    return textures.at(guid)->name;
}