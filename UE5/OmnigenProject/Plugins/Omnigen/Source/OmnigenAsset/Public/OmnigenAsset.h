#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Internationalization/Text.h"
#include "Foliage/Public/FoliageType_InstancedStaticMesh.h"
#include "Materials/MaterialInstanceConstant.h"

#include "OmnigenAsset.generated.h"

class UTexture2DArray;
class UTexture2D;
class AStaticMeshActor;

UENUM(BlueprintType)
enum class ETextureComponent : uint8
{
	Diffuse,
	Normal,
	AOR
};

namespace Omnigen
{
	using ETextureComponent = ::ETextureComponent;
}

USTRUCT(BlueprintType)
struct FTerrainMaterialTextureData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TMap<ETextureComponent, TSoftObjectPtr<UTexture2DArray>> ComponentArrays;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<float> TileSizes;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<float> MaxDisplacements;
};

UCLASS(BlueprintType, hidecategories = (Object))
class OMNIGENASSET_API UOmnigenAsset
	: public UObject
{
	GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "World")
	TSoftObjectPtr<UWorld> World;

    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "World")
    TArray<TSoftObjectPtr<AActor>> Actors;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Source")
	FString Path;

	// Terrain
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Terrain")
	TSoftObjectPtr<UMaterial> TerrainMasterMaterial;

 	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Terrain")
 	TArray<TSoftObjectPtr<UStaticMesh>> TerrainMeshes;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Terrain")
    TArray<TSoftObjectPtr<UMaterialInstance>> TerrainMaterialInstances;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Terrain")
	FTerrainMaterialTextureData RockTextureData;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Terrain")
	FTerrainMaterialTextureData CoverTextureData;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Terrain")
	TSoftObjectPtr<UTexture2D> TilingNoiseTexture;

	// Foliage
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Foliage")
	TSoftObjectPtr<UMaterial> FoliageMasterMaterial;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Foliage")
    TArray<TSoftObjectPtr<UStaticMesh>> FoliageMeshes;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Foliage")
    TArray<TSoftObjectPtr<UMaterialInstanceConstant>> FoliageMaterialInstances;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Foliage")
    TArray<TSoftObjectPtr<UTexture2D>> FoliageTextures;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Foliage")
    TArray<TSoftObjectPtr<UFoliageType>> FoliageTypes;

    // Water
    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
    TSoftObjectPtr<UMaterial> RiverMasterMaterial;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
    TSoftObjectPtr<UMaterial> LakeMasterMaterial;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
    TSoftObjectPtr<UMaterial> OceanMasterMaterial;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
	TArray<TSoftObjectPtr<UStaticMesh>> WaterMeshes;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
    TSoftObjectPtr<UMaterialInstance> RiverMaterialInstance;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
    TSoftObjectPtr<UMaterialInstance> LakeMaterialInstance;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Water")
    TSoftObjectPtr<UMaterialInstance> OceanMaterialInstance;

	void ClearForReimport();
};
