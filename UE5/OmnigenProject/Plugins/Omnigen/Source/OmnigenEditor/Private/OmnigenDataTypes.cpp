#include "OmnigenDataTypes.h"

void omniLoad(ETextureComponent& object, OmniBin<std::ios::in>& omniBin)
{
	int32 temp;
	omniBin >> temp;

	object = static_cast<ETextureComponent>(temp);
}

void omniLoad(Omnigen::TerrainChunk& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.indices;
	omniBin >> object.vertices;
	omniBin >> object.terrainTextureIds;
	omniBin >> object.coverTextureIds;
}

void omniLoad(Omnigen::Texture& object, OmniBin<std::ios::in>& omniBin)
{
	uint64 byteSize;
	omniBin >> byteSize;

	object.InputTextureBuffer.SetNum(byteSize);
	omniBin >> object.InputTextureBuffer;
}

void omniLoad(Omnigen::Material& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.TileSize;
	omniBin >> object.MaxDisplacement;
	omniBin >> object.Textures;
}

void omniLoad(Omnigen::OEFData& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.MaterialArrays.Add(Omnigen::EAsset::RockMaterial);
	omniBin >> object.MaterialArrays.Add(Omnigen::EAsset::SoilMaterial);
	omniBin >> object.AssetFiles;
	omniBin >> object.TerrainChunks;
	omniBin >> object.RiverMeshes;
	omniBin >> object.LakeMeshes;
	omniBin >> object.bHasOcean;
	omniBin >> object.TileTexture;
}

void omniLoad(Omnigen::PlantVariation& object, OmniBin<std::ios::in>& omniBin)
{
	uint64 LodCount;
	omniBin >> LodCount;

	for (uint64 i = 0; i < LodCount; ++i)
	{
		Omnigen::ELOD lod;
		omniBin >> lod;

		auto&& geometry = object.LODs.Add(lod);
		omniBin >> geometry.indices;
		omniBin >> geometry.vertices;
		omniBin >> geometry.instanceTransforms;
	}
}

void omniLoad(Omnigen::Plant& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.layer;

	uint64 MaterialsCount;
	omniBin >> MaterialsCount;

	object.materials.SetNum(MaterialsCount);
	for (auto&& Material : object.materials)
		omniBin >> Material;

	uint64 VariationsCount;
	omniBin >> VariationsCount;

	object.variations.SetNum(VariationsCount);
	for (auto&& Variation : object.variations)
		omniBin >> Variation;
}

void omniLoad(Omnigen::WaterMesh& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.indices;
	omniBin >> object.vertices;
}
