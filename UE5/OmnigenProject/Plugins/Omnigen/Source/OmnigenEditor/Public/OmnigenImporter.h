#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OmnigenDataTypes.h"

#include "OmnigenImporter.generated.h"

class UOmnigenAsset;
class UMaterialInterface;
class UMaterialInstanceConstant;
class AWaterBodyCustom;
class IAssetTools;

UCLASS()
class OMNIGENEDITOR_API UOmnigenImporter : public UObject
{
	GENERATED_BODY()

public:
	// Public API
	void LoadAssetFromFile(UOmnigenAsset* InOmnigenAsset, const FString& Filename, bool Reimport);

private:
	// Helper types and variables
    struct MeshGenSettings
    {
        FString Name;
        FString Path;
        bool UseNanite = false;
        int32 LightmapResolution = 64;
        int32 LumenMeshCards = 12;
        bool RecomputeNormals = false;
    };

	static const inline FString ReimportSuffix = TEXT("__OmnigenInternalReimportSuffix");

    static const inline EPixelFormat TexturePixelFormat = EPixelFormat::PF_B8G8R8A8;

    static const inline int32 RockSubmaterialCount = 4;
    static const inline int32 CoverSubmaterialCount = 1;

    static const inline FName TerrainMaterial_RockDiffuseHeightArray = "RockDiffuseHeightArray";
    static const inline FName TerrainMaterial_RockNormalArray = "RockNormalArray";
    static const inline FName TerrainMaterial_RockTexIDs = "RockTexIDs";
    static const inline FName TerrainMaterial_RockTileSizes = "RockTileSizes";
    static const inline FName TerrainMaterial_RockMaxDisplacements = "RockMaxDisplacements";

    static const inline FName TerrainMaterial_CoverDiffuseHeightArray = "CoverDiffuseHeightArray";
    static const inline FName TerrainMaterial_CoverNormalArray = "CoverNormalArray";
    static const inline FName TerrainMaterial_CoverTexIDs = "CoverTexIDs";
    static const inline FName TerrainMaterial_CoverTileSizes = "CoverTileSizes";
    static const inline FName TerrainMaterial_CoverMaxDisplacements = "CoverMaxDisplacements";

    static const inline FName TerrainMaterial_TilingNoise = "TilingNoise";

    static const inline FName PlantMaterial_DiffuseAlpha = "DiffuseAlpha";
    static const inline FName PlantMaterial_Normal = "Normal";

	// Asset being loaded
	UOmnigenAsset* OmnigenAsset = nullptr;
	bool bReimport = false;

	// Name helpers and paths
    FString FilenameNoExtension;
    FString ImportDir;
    int32 TerrainChunkIdx = 0;
    int32 RiverIdx = 0;
    int32 LakeIdx = 0;

	// Positioning
    FVector3f GlobalCenter = FVector3f::ZeroVector;
    FVector3f MinCoords = FVector3f(std::numeric_limits<float>::max());
    FVector3f MaxCoords = FVector3f(-std::numeric_limits<float>::max());

	// Engine pointers
    UTexture2D* DefaultDiffuseTexture = nullptr;
    UTexture2D* DefaultNormalTexture = nullptr;
    IAssetTools* AssetTools = nullptr;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
	// Omnigen files parsing
	Omnigen::OEFData ParseOmnigenExportFile(const FString& ExportFilePath);
	TArray<TArray<Omnigen::Material>> ParseOmnigenMaterials(const Omnigen::OEFData& OmnigenData, Omnigen::EAsset AssetType, FStringView TypeString);
	Omnigen::Plant ParsePlantAssetFile(const std::string& PlantFilePath);
    void LoadFoliage(const Omnigen::OEFData& OmnigenData);

	// Geometry conversion & positioning
	void ComputeExtents(const TArray<Omnigen::TerrainChunk>& Chunks);
	void ConvertTerrainGeometry(TArray<Omnigen::TerrainChunk>* Chunks);
	void ConvertPlantGeometry(Omnigen::Plant* Plant);
	void ConvertWaterGeometry(Omnigen::WaterMesh* Mesh, int32 VerticesPerPolygon);

	template<bool bHasNormal = true, typename T>
	void ConvertGeometry(TArray<T>* Vertices, TArray<uint32>* Indices, const  int32 VerticesPerPolygon = 3);

	template<typename VertexType>
	void ConvertInstancedGeometry(TArray<VertexType>* Vertices, TArray<uint32>* Indices, TArray<Omnigen::TRS>* InstanceTransforms);

    // Engine replacements
    bool CanCreateAsset(const FString& AssetName, const FString& PackageName, const FText& OperationText, UObject*& OutPreviousVersion) const;
    bool CheckForDeletedPackage(const UPackage* Package) const;
	UObject* CreateAssetWithPackage(FString AssetName, const FString& RelativePath, UClass* AssetClass, UFactory* Factory);

    // Actor placement
    AStaticMeshActor* CreateTerrainChunkActor(const Omnigen::TerrainChunk& Chunk);
    AWaterBodyCustom* CreateRiverActor(const Omnigen::WaterMesh& RiverMesh);
    AWaterBodyCustom* CreateLakeActor(const Omnigen::WaterMesh& LakeMesh);
    AStaticMeshActor* CreateOceanActor();

    // Meshes
    UStaticMesh* GenerateStaticMesh(FMeshDescription&& MeshDescription, const TArray<UMaterialInstance*>& MaterialInstances, const MeshGenSettings& GenSettings);
    FMeshDescription GenerateTerrainChunkMeshDescription(const Omnigen::TerrainChunk& Chunk);
    FMeshDescription GeneratePlantMeshDescription(const Omnigen::PlantVariation::Geometry& Geometry, const TArray<UMaterialInstance*>& MaterialInstances);
    FMeshDescription GenerateRiverMeshDescription(const Omnigen::WaterMesh& Geometry);
    FMeshDescription GenerateLakeMeshDescription(const Omnigen::WaterMesh& Geometry);
    FMeshDescription GenerateOceanMeshDescription();
    
    // Materials and textures
    FTerrainMaterialTextureData CreateTerrainMaterialTextureData(const TArray<TArray<Omnigen::Material>>& Materials, FStringView MaterialTypeName);
    TArray<TMap<ETextureComponent, UTexture2D*>> CreatePlantMaterialTextureData(const Omnigen::Plant& Plant);

    void Texture_TranscribeBuffer(const Omnigen::Texture* Texture);
    UTexture2D* Texture_CreateObject(int32 Width, int32 Height, TOptional<FString> SaveName = {}, TOptional<FString> SavePath = {});
    void Texture_Fill(const Omnigen::Texture& Texture, UTexture2D* UnrealTexture, bool IsAsset);
    UTexture2DArray* CreateNewTexture2DArray(const FString& Name, TArray<TWeakObjectPtr<UTexture2D>> Objects);

    UMaterial* CreateTerrainMaterial();
    UMaterial* CreatePlantMaterial();
    UMaterialInstanceConstant* CreateMaterialInstance(UMaterialInterface* BaseMaterial, const FString& Name, const FString& RelativePath);
    UMaterialInstanceConstant* CreateTerrainMaterialInstance(const Omnigen::TerrainChunk& Chunk, const FString& ChunkName);
    UMaterialInstanceConstant* CreatePlantMaterialInstance(const TMap<ETextureComponent, UTexture2D*>& MaterialData, const FString& PlantName);

	// Foliage
    void CreateFoliageTypeAndInstances(const Omnigen::Plant& Plant, const TArray<TMap<ETextureComponent, UTexture2D*>>& MaterialsData);
    UFoliageType* CreateFoliageType(const Omnigen::Plant& Plant, int VariationIdx, const TArray<UMaterialInstance*>& MaterialInstances);
};

template<bool bHasNormal /*= true*/, typename T>
void UOmnigenImporter::ConvertGeometry(TArray<T>* Vertices, TArray<uint32>* Indices, const int32 VerticesPerPolygon /*= 3*/)
{
    // Centralize: apply and convert to Z = height
    for (auto&& Vertex : *Vertices)
    {
        Vertex.position =
        {
            Vertex.position.X - GlobalCenter.X,
            Vertex.position.Z - GlobalCenter.Z,
            Vertex.position.Y
        };

        if constexpr (bHasNormal)
        {
            Swap(Vertex.normal.Y, Vertex.normal.Z);
        }
    }

    // Reverse triangle winding order
    for (int32 ti = 0; ti < Indices->Num(); ti += VerticesPerPolygon)
        Swap((*Indices)[ti + 0], (*Indices)[ti + 2]);
}

template<typename VertexType>
void UOmnigenImporter::ConvertInstancedGeometry(TArray<VertexType>* Vertices, TArray<uint32>* Indices, TArray<Omnigen::TRS>* InstanceTransforms)
{
    // Convert normals
    for (auto&& Vertex : *Vertices)
    {
        Vertex.position =
        {
            Vertex.position.X,
            Vertex.position.Z,
            Vertex.position.Y
        };

        Swap(Vertex.normal.Y, Vertex.normal.Z);
    }

    // Reverse triangle winding order
    for (int32 ti = 0; ti < Indices->Num(); ti += 3)
        Swap((*Indices)[ti + 0], (*Indices)[ti + 2]);

    for (auto&& trs : *InstanceTransforms)
    {
        trs.translation =
        {
            trs.translation.X - GlobalCenter.X,
            trs.translation.Z - GlobalCenter.Z,
            trs.translation.Y
        };

        Swap(trs.scale.Y, trs.scale.Z);
        Swap(trs.rotationAxis.Y, trs.rotationAxis.Z);
    }
}