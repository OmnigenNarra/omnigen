#pragma once
#include "OmniBinUE.h"
#include "OmnigenAsset.h"
#include "Quaternion.h"

namespace Omnigen
{
	enum class EAsset
	{
		Texture,
		Structure,
		Plant,
		RockMaterial,
		SoilMaterial,
		Last
	};

	enum class ELOD
	{
		Zero,
		Mid,
		Far,
		Last
	};

    enum class EBiomeLayer
    {
        Floor,      // 0m - 0.5m
        Low,        // 0.5m - 1m
        Middle,     // 1m - 2m
        High        // > 2m
    };

#pragma pack(push, 4)
	struct TerrainMeshVertex
	{
		FVector3f position;
		FVector3f normal;

		uint32 terrainTexWeights;
		uint32 coverTexWeights;
		uint32 packParams;

		float displacementFactor;
		float unused_temperature;
		float unused_humidity;
	};
#pragma pack(pop)

	struct Texture
	{
		TArray<uint8> InputTextureBuffer;

		int32 Width;
		int32 Height;
		TArray64<uint8> UncompressedPixelData;
	};

	struct Material
	{
		TMap<ETextureComponent, Texture> Textures;
		float TileSize;
		float MaxDisplacement;
	};

	struct TerrainChunk
	{
		TArray<TerrainMeshVertex> vertices;
		TArray<uint32> indices;

		TArray<uint32> terrainTextureIds;
		TArray<uint32> coverTextureIds;
	};

	struct WaterMesh
	{
		struct Vertex
		{
			FVector3f position;
		};

		TArray<Vertex> vertices;
		TArray<uint32> indices;
	};

	struct RockMaterial
	{
		TStaticArray<Material, 4> materials;
	};

	struct CoverMaterial
	{
		TStaticArray<Material, 1> materials;
	};

	struct OEFData
	{
		TArray<TerrainChunk> TerrainChunks;
		TMap<EAsset, std::unordered_map<int64, std::string>> AssetFiles;
		TMap<EAsset, TArray<int64>> MaterialArrays;
		Texture TileTexture;
		TArray<WaterMesh> RiverMeshes;
		TArray<WaterMesh> LakeMeshes;
		bool bHasOcean;
	};

	struct PlantVertex
	{
		FVector3f position;
		FVector3f normal;
		FVector2f uv;
		int32 materialID;
	};

	struct TRS
	{
		FVector3f translation;
		FVector3f scale;
		FVector3f rotationAxis;
		float rotationAngleRad;
	};

	struct PlantVariation
	{
		struct Geometry
		{
			TArray<PlantVertex> vertices;
			TArray<uint32> indices;

			TArray<TRS> instanceTransforms;
		};

		TMap<ELOD, Geometry> LODs;
	};

	struct Plant
	{
		FString name;
		EBiomeLayer layer;
		TArray<PlantVariation> variations;
		TArray<Material> materials;
	};
}

template<> constexpr bool serializeAsPOD<Omnigen::TerrainMeshVertex> = true;

template<> constexpr bool serializeAsPOD<ETextureComponent> = false;
void omniLoad(ETextureComponent& object, OmniBin<std::ios::in>& omniBin);

void omniLoad(Omnigen::Texture& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Omnigen::Material& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Omnigen::TerrainChunk& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Omnigen::WaterMesh& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Omnigen::OEFData& object, OmniBin<std::ios::in>& omniBin);

template<> constexpr bool serializeAsPOD<Omnigen::PlantVertex> = true;
template<> constexpr bool serializeAsPOD<Omnigen::TRS> = true;
void omniLoad(Omnigen::PlantVariation& object, OmniBin<std::ios::in>& omniBin);
void omniLoad(Omnigen::Plant& object, OmniBin<std::ios::in>& omniBin);
