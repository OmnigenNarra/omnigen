#pragma once
#include "Mesh.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertyList.h"
#include "Data/Assets/Common/Texture.h"

// Mesh Atlas is a set of meshes using the same set of materials.
// Ex. Plant variations
struct MeshAtlasAssetBase : OmnigenAssetBase
{
    // optional return value
    const auto& getMeshes() const { return meshes; };
    const auto& getMaterials() const { return materials; };

protected:
    std::vector<MeshComponent> meshes;
    std::vector<Material> materials;

    FRIEND_OMNIBIN(MeshAtlasAssetBase);
};

void omniSave(const MeshAtlasAssetBase& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(MeshAtlasAssetBase& object, OmniBin<std::ios::in>& omniBin);

class MeshAtlasPropertyList : public OmnigenPropertyListBase
{
public:
    MeshAtlasPropertyList(qint64 id);
    QSharedPointer<OmnigenAsset<EAsset::Texture>> getChosenTexture();

    std::vector<qint64> cachedTextureIds;
    int chosenTexture;

private:
    const std::unordered_map<qint64, QSharedPointer<OmnigenAsset<EAsset::Texture>>>& cachedTextures;
};