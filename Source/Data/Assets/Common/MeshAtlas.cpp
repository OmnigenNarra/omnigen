#include "stdafx.h"
#include "MeshAtlas.h"
#include "Omnigen.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include "Utils/Triangulation/Triangulation.h"
#include "Editor/Dialogs/AssetCompiler/AssetCompilerDialog.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"

void omniSave(const MeshAtlasAssetBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const OmnigenAssetBase&>(object);
    omniBin << object.meshes;
    omniBin << object.materials;
}

void omniLoad(MeshAtlasAssetBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<OmnigenAssetBase&>(object);
    omniBin >> object.meshes;
    omniBin >> object.materials;
}

MeshAtlasPropertyList::MeshAtlasPropertyList(qint64 id)
    : OmnigenPropertyListBase(id)
    , cachedTextureIds(Omnigen::get()->getAssetsSection()->getAssetsIds<EAsset::Texture>())
    , cachedTextures(Omnigen::get()->getAssetsSection()->getAssets<EAsset::Texture>())
{
}

QSharedPointer<OmnigenAsset<EAsset::Texture>> MeshAtlasPropertyList::getChosenTexture()
{
    return cachedTextures.at(cachedTextureIds[chosenTexture]);
}
