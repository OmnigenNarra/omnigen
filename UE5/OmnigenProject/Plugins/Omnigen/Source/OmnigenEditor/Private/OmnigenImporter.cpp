#include "OmnigenImporter.h"
#include "OmniBinUE.h"
#include "OmnigenAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Blueprint/AsyncTaskDownloadImage.h"
#include "Factories/TextureFactory.h"
#include "PackageTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMeshSocket.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "StaticMeshOperations.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Classes/Components/InstancedStaticMeshComponent.h"
#include "Engine/InstancedStaticMesh.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Texture2D.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialEditingLibrary.h"
#include "EditorAssetLibrary.h"
#include "ImageUtils.h"
#include "Engine/Texture2DArray.h"
#include "UObject/SavePackage.h"
#include "Engine/Classes/Materials/MaterialExpressionSpeedTree.h"
#include "Foliage/Public/FoliageInstancedStaticMeshComponent.h"
#include "Materials/Material.h"
#include "Factories/MaterialFactoryNew.h"
#include "Misc/FileHelper.h"
#include "WaterBodyCustomActor.h"

#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionOutput.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Factories/Texture2DArrayFactory.h"
#include "InstancedFoliageActor.h"

#include "Factories/OmnigenFoliageTypeFactory.h"
#include "Factories/Texture2dFactoryNew.h"
#include "CoreUObject/Public/UObject/GCObjectScopeGuard.h"
#include "UnrealEd/Public/ObjectTools.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "UnrealEd/Public/FileHelpers.h"
#include "EngineAnalytics.h"
#include "Misc/ScopedSlowTask.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"

template<typename Lambda>
auto RunThreaded(int32 Count, int32 BatchSize, const Lambda& LambdaFunction)
{
    using ResultType = std::invoke_result_t<Lambda, int32>;

    const int32 NumThreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
    int32 NumBatches = FMath::DivideAndRoundUp(Count, BatchSize);
    int32 ThreadsToUse = FMath::Min(NumThreads, NumBatches);
    int32 BatchPerThread = FMath::DivideAndRoundUp(NumBatches, ThreadsToUse);

    if constexpr (std::is_same_v<ResultType, void>)
    {
        TArray<TFuture<void>> Futures;
        for (int32 ThreadIndex = 0; ThreadIndex < ThreadsToUse; ++ThreadIndex)
        {
            int32 StartBatchIndex = ThreadIndex * BatchPerThread;
            int32 EndBatchIndex = FMath::Min((ThreadIndex + 1) * BatchPerThread, NumBatches);

            Futures.Emplace(Async(EAsyncExecution::TaskGraph, [&LambdaFunction, StartBatchIndex, EndBatchIndex, BatchSize, Count]()
                {
                    for (int32 BatchIndex = StartBatchIndex; BatchIndex < EndBatchIndex; ++BatchIndex)
                    {
                        int32 StartIndex = BatchIndex * BatchSize;
                        int32 EndIndex = FMath::Min((BatchIndex + 1) * BatchSize, Count);
                        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
                            LambdaFunction(Index);
                    }
                }));
        }

        for (auto& Future : Futures)
            Future.Get();
    }
    else
    {
        TArray<TFuture<TArray<ResultType>>> Futures;

        for (int32 ThreadIndex = 0; ThreadIndex < ThreadsToUse; ++ThreadIndex)
        {
            int32 StartBatchIndex = ThreadIndex * BatchPerThread;
            int32 EndBatchIndex = FMath::Min((ThreadIndex + 1) * BatchPerThread, NumBatches);

            Futures.Emplace(Async(EAsyncExecution::TaskGraph, [&LambdaFunction, StartBatchIndex, EndBatchIndex, BatchSize, Count]()
                {
                    TArray<ResultType> Results;
                    for (int32 BatchIndex = StartBatchIndex; BatchIndex < EndBatchIndex; ++BatchIndex)
                    {
                        int32 StartIndex = BatchIndex * BatchSize;
                        int32 EndIndex = FMath::Min((BatchIndex + 1) * BatchSize, Count);
                        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
                        {
                            Results.Emplace(LambdaFunction(Index));
                        }
                    }
                    return Results;
                }));
        }

        TArray<ResultType> Results;
        for (auto& Future : Futures)
        {
            TArray<ResultType> BatchResults = Future.Get();
            Results.Append(BatchResults);
        }
        return Results;
    }
}

void UOmnigenImporter::LoadAssetFromFile(UOmnigenAsset* InOmnigenAsset, const FString& Filename, bool Reimport)
{
	AddToRoot();

    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        UE_LOG(LogTemp, Warning, TEXT("GEditor || GEditor->GetEditorWorldContext().World() false"));
        return;
    }

	bReimport = Reimport;
	OmnigenAsset = InOmnigenAsset;

    FString UnusedExtension;
    FPaths::Split(Filename, ImportDir, FilenameNoExtension, UnusedExtension);

	AssetTools = &FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const FString DefaultDiffuseTexturePath = TEXT("/Engine/EngineMaterials/DefaultDiffuse.DefaultDiffuse");
	DefaultDiffuseTexture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(DefaultDiffuseTexturePath));

	const FString DefaultNormalTexturePath = TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal");
	DefaultNormalTexture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(DefaultNormalTexturePath));

	// Read file contents
	auto OmnigenData = ParseOmnigenExportFile(Filename);

	ComputeExtents(OmnigenData.TerrainChunks);

	// Texture arrays
	auto RockMaterials = ParseOmnigenMaterials(OmnigenData, Omnigen::EAsset::RockMaterial, TEXT("Rock"));
	auto CoverMaterials = ParseOmnigenMaterials(OmnigenData, Omnigen::EAsset::SoilMaterial, TEXT("Cover"));

	// Material data
	OmnigenAsset->RockTextureData = CreateTerrainMaterialTextureData(RockMaterials, TEXT("Rock"));
	OmnigenAsset->CoverTextureData = CreateTerrainMaterialTextureData(CoverMaterials, TEXT("Cover"));

	// Tile noise
	Texture_TranscribeBuffer(&OmnigenData.TileTexture);
	OmnigenAsset->TilingNoiseTexture = Texture_CreateObject(OmnigenData.TileTexture.Width, OmnigenData.TileTexture.Height, FString("OmnigenTileNoise"), FString("Terrain"));
	Texture_Fill(OmnigenData.TileTexture, OmnigenAsset->TilingNoiseTexture.Get(), true);

	// Process Terrain
	ConvertTerrainGeometry(&OmnigenData.TerrainChunks);
	{
        FScopedSlowTask Feedback(OmnigenData.TerrainChunks.Num(), FText::FromStringView(TEXT("Loading Terrain")));
        Feedback.MakeDialog();

		for (auto&& TerrainChunk : OmnigenData.TerrainChunks)
		{
			Feedback.EnterProgressFrame(1);
			CreateTerrainChunkActor(TerrainChunk);
		}
	}

    // Process Foliage
	LoadFoliage(OmnigenData);

	// Process Water
    if (!OmnigenAsset->RiverMasterMaterial)
    {
        const FString SimpleRiverMaterialPath = TEXT("/Water/Caustics/Materials/Preview/WaterCausticsPreview.WaterCausticsPreview");
        OmnigenAsset->RiverMasterMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SimpleRiverMaterialPath));
    }
    if (!OmnigenAsset->LakeMasterMaterial)
    {
        const FString SimpleLakeMaterialPath = TEXT("/Water/Caustics/Materials/Preview/WaterCausticsPreview.WaterCausticsPreview");
        OmnigenAsset->LakeMasterMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SimpleLakeMaterialPath));
    }
    if (!OmnigenAsset->OceanMasterMaterial)
    {
        const FString OceanMaterialPath = TEXT("/Water/Materials/WaterSurface/Water_FarMesh.Water_FarMesh");
        OmnigenAsset->OceanMasterMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(OceanMaterialPath));
    }

    if (!OmnigenAsset->RiverMaterialInstance || OmnigenAsset->RiverMaterialInstance->GetMaterial() != OmnigenAsset->RiverMasterMaterial)
    {
        OmnigenAsset->RiverMaterialInstance = CreateMaterialInstance(OmnigenAsset->RiverMasterMaterial.Get(), FString::Printf(TEXT("%s_RiverMaterial"), *FilenameNoExtension), TEXT("Water/Materials"));
    }
    if (!OmnigenAsset->LakeMaterialInstance || OmnigenAsset->LakeMaterialInstance->GetMaterial() != OmnigenAsset->LakeMasterMaterial)
    {
        OmnigenAsset->LakeMaterialInstance = CreateMaterialInstance(OmnigenAsset->LakeMasterMaterial.Get(), FString::Printf(TEXT("%s_LakeMaterial"), *FilenameNoExtension), TEXT("Water/Materials"));
    }
    if (OmnigenData.bHasOcean && (!OmnigenAsset->OceanMaterialInstance || OmnigenAsset->OceanMaterialInstance->GetMaterial() != OmnigenAsset->OceanMasterMaterial))
    {
        OmnigenAsset->OceanMaterialInstance = CreateMaterialInstance(OmnigenAsset->OceanMasterMaterial.Get(), FString::Printf(TEXT("%s_OceanMaterial"), *FilenameNoExtension), TEXT("Water/Materials"));
    }

	{
		FScopedSlowTask Feedback(OmnigenData.RiverMeshes.Num(), FText::FromStringView(TEXT("Loading Rivers")));
		Feedback.MakeDialogDelayed(1.0f);

        for (auto&& RiverMesh : OmnigenData.RiverMeshes)
        {
			Feedback.EnterProgressFrame(1);
            ConvertWaterGeometry(&RiverMesh, 4);
            CreateRiverActor(RiverMesh);
        }
	}
	{
        FScopedSlowTask Feedback(OmnigenData.LakeMeshes.Num(), FText::FromStringView(TEXT("Loading Lakes")));
        Feedback.MakeDialogDelayed(1.0f);

        for (auto&& LakeMesh : OmnigenData.LakeMeshes)
        {
			Feedback.EnterProgressFrame(1);
            ConvertWaterGeometry(&LakeMesh, 3);
            CreateLakeActor(LakeMesh);
        }
	}
	
	if (OmnigenData.bHasOcean)
		CreateOceanActor();

	RemoveFromRoot();
}

Omnigen::OEFData UOmnigenImporter::ParseOmnigenExportFile(const FString& ExportFilePath)
{
	OmniBin<std::ios::in> Reader(TCHAR_TO_UTF8(*ExportFilePath));

	Omnigen::OEFData Content;
	Reader >> Content;

	return Content;
}

TArray<TArray<Omnigen::Material>> UOmnigenImporter::ParseOmnigenMaterials(const Omnigen::OEFData& OmnigenData, Omnigen::EAsset AssetType, FStringView TypeString)
{
	TArray<TArray<Omnigen::Material>> OutMaterials;
    if (!OmnigenData.AssetFiles.Contains(AssetType))
        return OutMaterials;

	auto&& MaterialIds = OmnigenData.MaterialArrays[AssetType];
	OutMaterials.SetNum(MaterialIds.Num());
	auto&& MaterialAssetPaths = OmnigenData.AssetFiles[AssetType];

    RunThreaded(MaterialIds.Num(), 1, [&](int32 Idx)
        {
            auto ID = MaterialIds[Idx];
            auto AssetPath = TCHAR_TO_ANSI(*ImportDir) + std::string("\\") + MaterialAssetPaths.at(ID);
            OmniBin<std::ios::in> Reader(AssetPath);

            uint64 SubMaterialCount;
            Reader >> SubMaterialCount;

            TArray<Omnigen::Material> SubMaterials;
            SubMaterials.SetNum(SubMaterialCount);

            for (uint8 i = 0; i < SubMaterialCount; ++i)
                Reader >> SubMaterials[i];

            OutMaterials[Idx] = MoveTemp(SubMaterials);
        });

	return OutMaterials;
}

Omnigen::Plant UOmnigenImporter::ParsePlantAssetFile(const std::string& PlantFilePath)
{
    auto AssetPath = TCHAR_TO_ANSI(*ImportDir) + std::string("\\") + PlantFilePath;
    OmniBin<std::ios::in> Reader(AssetPath);

    Omnigen::Plant Plant;
    Reader >> Plant;

    Plant.name = FPaths::GetBaseFilename(ANSI_TO_TCHAR(AssetPath.data()));

    return Plant;
}

void UOmnigenImporter::LoadFoliage(const Omnigen::OEFData& OmnigenData)
{
    if (!OmnigenData.AssetFiles.Contains(Omnigen::EAsset::Plant))
        return;

    auto&& PlantFiles = OmnigenData.AssetFiles[Omnigen::EAsset::Plant];
    UE_LOG(LogTemp, Warning, TEXT("Loading Foliage"));

    TArray<int64> PlantKeys;
    PlantKeys.Reserve(PlantFiles.size());
    for (auto&& [Id, PlantFile] : PlantFiles)
        PlantKeys.Add(Id);

    TArray<Omnigen::Plant> Plants = RunThreaded(PlantKeys.Num(), 1, [&](int32 Idx)
        {
            auto Plant = ParsePlantAssetFile(PlantFiles.at(PlantKeys[Idx]));
            ConvertPlantGeometry(&Plant);
            return Plant;
        });

    FScopedSlowTask Feedback(Plants.Num(), FText::FromString(TEXT("Loading Foliage")));
    Feedback.MakeDialog();

    for (auto& Plant : Plants)
    {
        Feedback.EnterProgressFrame(1);
        auto MaterialData = CreatePlantMaterialTextureData(Plant);
        CreateFoliageTypeAndInstances(Plant, MaterialData);
    }
}

void UOmnigenImporter::ComputeExtents(const TArray<Omnigen::TerrainChunk>& Chunks)
{
    for (auto&& Chunk : Chunks)
    {
        FVector3f ChunkCenter = FVector3f::ZeroVector;
        for (auto&& Vertex : Chunk.vertices)
        {
            ChunkCenter += Vertex.position;

            MinCoords.X = FMath::Min(MinCoords.X, Vertex.position.X);
            MinCoords.Y = FMath::Min(MinCoords.Y, Vertex.position.Y);
            MinCoords.Z = FMath::Min(MinCoords.Z, Vertex.position.Z);

            MaxCoords.X = FMath::Max(MaxCoords.X, Vertex.position.X);
            MaxCoords.Y = FMath::Max(MaxCoords.Y, Vertex.position.Y);
            MaxCoords.Z = FMath::Max(MaxCoords.Z, Vertex.position.Z);
        }

        ChunkCenter /= Chunk.vertices.Num();
        GlobalCenter += ChunkCenter;
    }

    GlobalCenter /= Chunks.Num();
}

void UOmnigenImporter::ConvertTerrainGeometry(TArray<Omnigen::TerrainChunk>* Chunks)
{
    for (auto&& Chunk : *Chunks)
        ConvertGeometry(&Chunk.vertices, &Chunk.indices);
}

void UOmnigenImporter::ConvertPlantGeometry(Omnigen::Plant* Plant)
{
    for (auto&& Variation : Plant->variations)
        for (auto&& [Lod, Geometry] : Variation.LODs)
            ConvertInstancedGeometry(&Geometry.vertices, &Geometry.indices, &Geometry.instanceTransforms);
}

void UOmnigenImporter::ConvertWaterGeometry(Omnigen::WaterMesh* Mesh, int32 VerticesPerPolygon)
{
    ConvertGeometry<false>(&Mesh->vertices, &Mesh->indices, VerticesPerPolygon);
}

#define LOCTEXT_NAMESPACE "AssetTools"
bool UOmnigenImporter::CheckForDeletedPackage(const UPackage* Package) const
{
    if (ISourceControlModule::Get().IsEnabled())
    {
        ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
        if (SourceControlProvider.IsAvailable())
        {
            FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Package, EStateCacheUsage::ForceUpdate);
            if (SourceControlState.IsValid() && SourceControlState->IsDeleted())
            {
                // Creating an asset in a package that is marked for delete - revert the delete and check out the package
                if (!SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), Package))
                {
                    // Failed to revert file which was marked for delete
                    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("RevertDeletedFileFailed", "Failed to revert package which was marked for delete."));
                    return false;
                }

                if (!SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), Package))
                {
                    // Failed to check out file
                    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CheckOutFileFailed", "Failed to check out package"));
                    return false;
                }
            }
        }
        else
        {
            FMessageLog EditorErrors("EditorErrors");
            EditorErrors.Warning(LOCTEXT("DeletingNoSCCConnection", "Could not check for deleted file. No connection to source control available!"));
            EditorErrors.Notify();
        }
    }

    return true;
}

bool UOmnigenImporter::CanCreateAsset(const FString& AssetName, const FString& PackageName, const FText& OperationText, UObject*& OutPreviousVersion) const
{
    // @todo: These 'reason' messages are not localized strings!
    FText Reason;
    if (!FName(*AssetName).IsValidObjectName(Reason)
        || !FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots=*/false, &Reason))
    {
        FMessageDialog::Open(EAppMsgType::Ok, Reason);
        return false;
    }

    // We can not create assets that share the name of a map file in the same location
    if (FEditorFileUtils::IsMapPackageAsset(PackageName))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("AssetNameInUseByMap", "You can not create an asset named '{0}' because there is already a map file with this name in this folder."), FText::FromString(AssetName)));
        return false;
    }

    // Find (or create!) the desired package for this object
    UPackage* Pkg = CreatePackage(*PackageName);

    // Handle fully loading packages before creating new objects.
    TArray<UPackage*> TopLevelPackages;
    TopLevelPackages.Add(Pkg);
    if (!UPackageTools::HandleFullyLoadingPackages(TopLevelPackages, OperationText))
    {
        // User aborted.
        return false;
    }

    // We need to test again after fully loading.
    if (!FName(*AssetName).IsValidObjectName(Reason)
        || !FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots=*/false, &Reason))
    {
        FMessageDialog::Open(EAppMsgType::Ok, Reason);
        return false;
    }

    // Check for an existing object
    UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), Pkg, *AssetName);
    if (ExistingObject != nullptr)
    {
        // Object already exists in either the specified package or another package.  Check to see if the user wants
        // to replace the object.
        if (bReimport)
        {
            OutPreviousVersion = ExistingObject;
        }
        else
        {
            bool bWantReplace =
                EAppReturnType::Yes == FMessageDialog::Open(
                    EAppMsgType::YesNo,
                    EAppReturnType::No,
                    FText::Format(
                        NSLOCTEXT("UnrealEd", "ReplaceExistingObjectInPackage_F", "An object [{0}] of class [{1}] already exists in file [{2}].  Do you want to replace the existing object?  If you click 'Yes', the existing object will be deleted.  Otherwise, click 'No' and choose a unique name for your new object."),
                        FText::FromString(AssetName), FText::FromString(ExistingObject->GetClass()->GetName()), FText::FromString(PackageName)));

            if (bWantReplace)
            {
                // Replacing an object.  Here we go!
                // Delete the existing object
                bool bDeleteSucceeded = ObjectTools::DeleteSingleObject(ExistingObject);

                if (bDeleteSucceeded)
                {
                    // Force GC so we can cleanly create a new asset (and not do an 'in place' replacement)
                    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

                    // Old package will be GC'ed... create a new one here
                    Pkg = CreatePackage(*PackageName);
                    Pkg->MarkAsFullyLoaded();
                }
                else
                {
                    // Notify the user that the operation failed b/c the existing asset couldn't be deleted
                    FMessageDialog::Open(EAppMsgType::Ok, FText::Format(NSLOCTEXT("DlgNewGeneric", "ContentBrowser_CannotDeleteReferenced", "{0} wasn't created.\n\nThe asset is referenced by other content."), FText::FromString(AssetName)));
                }

                if (!bDeleteSucceeded || !IsUniqueObjectName(*AssetName, Pkg, Reason))
                {
                    // Original object couldn't be deleted
                    return false;
                }
            }
            else
            {
                // User chose not to replace the object; they'll need to enter a new name
                return false;
            }
        }
    }

    // Check for a package that was marked for delete in source control
    if (!CheckForDeletedPackage(Pkg))
    {
        return false;
    }

    return true;
}

UObject* UOmnigenImporter::CreateAssetWithPackage(FString AssetName, const FString& RelativePath, UClass* AssetClass, UFactory* Factory)
{
    FString PackagePath = FPaths::Combine(FString::Printf(TEXT("/Game/%s/"), *FilenameNoExtension), RelativePath);
    //AssetTools->CreateUniqueAssetName(PackageName, "", PackagePath, FinalName);
    //return AssetTools->CreateAsset(RelativeName, PackageDir, AssetClass, Factory);

    FGCObjectScopeGuard DontGCFactory(Factory);

    // Verify the factory class
    if (!ensure(AssetClass || Factory))
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MustSupplyClassOrFactory", "The new asset wasn't created due to a problem finding the appropriate factory or class for the new asset."));
        return nullptr;
    }

    if (AssetClass && Factory && !ensure(AssetClass->IsChildOf(Factory->GetSupportedClass())))
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidFactory", "The new asset wasn't created because the supplied factory does not support the supplied class."));
        return nullptr;
    }

    FString PackageName = UPackageTools::SanitizePackageName(PackagePath + TEXT("/") + AssetName);

    // Make sure we can create the asset without conflicts
    UObject* PreviousVersion = nullptr;
    if (!CanCreateAsset(AssetName, PackageName, LOCTEXT("CreateANewObject", "Create a new object"), PreviousVersion))
    {
        return nullptr;
    }

    // Make a new version with a suffix, will be resolved later
    if (PreviousVersion)
    {
        AssetName += ReimportSuffix;
        PackageName += ReimportSuffix;
    }

    UClass* ClassToUse = AssetClass ? AssetClass : (Factory ? Factory->GetSupportedClass() : nullptr);

    UPackage* Pkg = CreatePackage(*PackageName);
    UObject* NewObj = nullptr;
    EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
    if (Factory)
    {
        // Need simpler factory without presetting white shit
        NewObj = Factory->FactoryCreateNew(ClassToUse, Pkg, FName(*AssetName), Flags, nullptr, GWarn, NAME_None);
    }
    else if (AssetClass)
    {
        NewObj = NewObject<UObject>(Pkg, ClassToUse, FName(*AssetName), Flags);
    }

    if (NewObj)
    {

        Pkg->SetIsExternallyReferenceable(AssetTools->GetCreateAssetsAsExternallyReferenceable());

        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewObj);

        //UAssetToolsImpl::OnNewCreateRecord(AssetClass, false);
        if (FEngineAnalytics::IsAvailable())
        {
            TArray<FAnalyticsEventAttribute> Attribs;
            Attribs.Add(FAnalyticsEventAttribute(TEXT("AssetType"), AssetClass->GetName()));
            Attribs.Add(FAnalyticsEventAttribute(TEXT("Duplicated"), TEXT("No")));

            FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.CreateAsset"), Attribs);
        }

        // Mark the package dirty...
        Pkg->MarkPackageDirty();
    }

    if (PreviousVersion)
    {
        TArray<UObject*> AssetsToReplace = { PreviousVersion };
        ObjectTools::ForceReplaceReferences(NewObj, AssetsToReplace);
        ObjectTools::ForceDeleteObjects({ PreviousVersion }, false);

        AssetName.RemoveFromEnd(ReimportSuffix);
        PackageName.RemoveFromEnd(ReimportSuffix);

        Pkg->Rename(*PackageName);
        NewObj->Rename(*AssetName);
    }

    return NewObj;
}
#undef LOCTEXT_NAMESPACE

AStaticMeshActor* UOmnigenImporter::CreateTerrainChunkActor(const Omnigen::TerrainChunk& Chunk)
{
    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        UE_LOG(LogTemp, Warning, TEXT("GEditor || GEditor->GetEditorWorldContext().World() false"));
        return nullptr;
    }

    MeshGenSettings GenSettings;
    GenSettings.UseNanite = true;
    GenSettings.LumenMeshCards = 32;

    // Actor entity
    AStaticMeshActor* ChunkActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AStaticMeshActor>();
    OmnigenAsset->Actors.Add(ChunkActor);
    GenSettings.Name = FString::Printf(TEXT("%s_Chunk_%i"), *FilenameNoExtension, TerrainChunkIdx++);
    GenSettings.Path = TEXT("Terrain/Meshes");
    ChunkActor->SetActorLabel(GenSettings.Name, false);

    // Material
    auto* MaterialInstance = CreateTerrainMaterialInstance(Chunk, GenSettings.Name);
    OmnigenAsset->TerrainMaterialInstances.Add(MaterialInstance);

    // Mesh
    UStaticMesh* StaticMesh = GenerateStaticMesh(GenerateTerrainChunkMeshDescription(Chunk), { MaterialInstance }, GenSettings);
    ChunkActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
    OmnigenAsset->TerrainMeshes.Add(StaticMesh);

    return ChunkActor;
}

AWaterBodyCustom* UOmnigenImporter::CreateRiverActor(const Omnigen::WaterMesh& RiverMesh)
{
    MeshGenSettings GenSettings;
    GenSettings.Name = FString::Printf(TEXT("%s_River_%i"), *FilenameNoExtension, RiverIdx++);
    GenSettings.Path = TEXT("Water/Meshes");
    GenSettings.RecomputeNormals = true;

    // Actor entity
    AWaterBodyCustom* RiverActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AWaterBodyCustom>();
    OmnigenAsset->Actors.Add(RiverActor);
    RiverActor->SetActorLabel(GenSettings.Name, false);

    // Mesh
    UStaticMesh* StaticMesh = GenerateStaticMesh(GenerateRiverMeshDescription(RiverMesh), { nullptr }, GenSettings);
    OmnigenAsset->WaterMeshes.Add(StaticMesh);

    RiverActor->GetWaterBodyComponent()->SetWaterMeshOverride(StaticMesh);
    RiverActor->GetWaterBodyComponent()->SetWaterMaterial(OmnigenAsset->RiverMaterialInstance.LoadSynchronous());

    FOnWaterBodyChangedParams UpdateParams;
    UpdateParams.bShapeOrPositionChanged = true;
    RiverActor->GetWaterBodyComponent()->UpdateAll(UpdateParams);

    return RiverActor;
}

AWaterBodyCustom* UOmnigenImporter::CreateLakeActor(const Omnigen::WaterMesh& LakeMesh)
{
    MeshGenSettings GenSettings;
    GenSettings.Name = FString::Printf(TEXT("%s_Lake_%i"), *FilenameNoExtension, LakeIdx++);
    GenSettings.Path = TEXT("Water/Meshes");
    GenSettings.RecomputeNormals = true;

    // Actor entity
    AWaterBodyCustom* LakeActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AWaterBodyCustom>();
    OmnigenAsset->Actors.Add(LakeActor);
    LakeActor->SetActorLabel(GenSettings.Name, false);

    // Mesh
    UStaticMesh* StaticMesh = GenerateStaticMesh(GenerateLakeMeshDescription(LakeMesh), { nullptr }, GenSettings);
    OmnigenAsset->WaterMeshes.Add(StaticMesh);

    LakeActor->GetWaterBodyComponent()->SetWaterMeshOverride(StaticMesh);
    LakeActor->GetWaterBodyComponent()->SetWaterMaterial(OmnigenAsset->RiverMaterialInstance.LoadSynchronous());

    FOnWaterBodyChangedParams UpdateParams;
    UpdateParams.bShapeOrPositionChanged = true;
    LakeActor->GetWaterBodyComponent()->UpdateAll(UpdateParams);

    return LakeActor;
}

AStaticMeshActor* UOmnigenImporter::CreateOceanActor()
{
    MeshGenSettings GenSettings;
    GenSettings.Name = FString::Printf(TEXT("%s_Ocean"), *FilenameNoExtension);
    GenSettings.Path = TEXT("Water/Meshes");

    // Actor entity
    AStaticMeshActor* OceanActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AStaticMeshActor>();
    OmnigenAsset->Actors.Add(OceanActor);
    OceanActor->SetActorLabel(GenSettings.Name, false);

    // Mesh
    UStaticMesh* StaticMesh = GenerateStaticMesh(GenerateOceanMeshDescription(), { OmnigenAsset->RiverMaterialInstance.LoadSynchronous() }, GenSettings);
    OmnigenAsset->WaterMeshes.Add(StaticMesh);

    OceanActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);

    return OceanActor;
}

UStaticMesh* UOmnigenImporter::GenerateStaticMesh(FMeshDescription&& MeshDescription, const TArray<UMaterialInstance*>& MaterialInstances, const MeshGenSettings& GenSettings)
{
    check(MeshDescription.Polygons().Num() > 0);

    UStaticMesh* StaticMesh = static_cast<UStaticMesh*>(CreateAssetWithPackage(GenSettings.Name, GenSettings.Path, UStaticMesh::StaticClass(), nullptr));
    check(StaticMesh);

    // Lighting settings
    StaticMesh->SetLightingGuid();
    StaticMesh->SetLightMapResolution(GenSettings.LightmapResolution);
    StaticMesh->SetLightMapCoordinateIndex(1);

    // Build setup
    FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();
    SrcModel.BuildSettings.bRecomputeNormals = GenSettings.RecomputeNormals;
    SrcModel.BuildSettings.bRecomputeTangents = true;
    SrcModel.BuildSettings.bRemoveDegenerates = false;
    SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = true;
    SrcModel.BuildSettings.bUseFullPrecisionUVs = false;
    SrcModel.BuildSettings.bGenerateLightmapUVs = true;
    SrcModel.BuildSettings.SrcLightmapIndex = 0;
    SrcModel.BuildSettings.DstLightmapIndex = 1;
    SrcModel.BuildSettings.MaxLumenMeshCards = GenSettings.LumenMeshCards;

    SrcModel.ReductionSettings.MaxDeviation = 0.0f;
    SrcModel.ReductionSettings.PercentTriangles = 1.0f;
    SrcModel.ReductionSettings.PercentVertices = 1.0f;

    StaticMesh->CreateMeshDescription(0, MoveTemp(MeshDescription));
    StaticMesh->CommitMeshDescription(0);

    // Materials
    for (auto&& MaterialInstance : MaterialInstances)
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial(MaterialInstance));

    // Nanite
    StaticMesh->NaniteSettings.bEnabled = GenSettings.UseNanite;
    StaticMesh->NaniteSettings.FallbackPercentTriangles = 1.0f;
    StaticMesh->NaniteSettings.FallbackRelativeError = 0.0f;

    // Build mesh from source
    StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
    StaticMesh->Build(true);

    // Notify asset registry of new asset
    FAssetRegistryModule::AssetCreated(StaticMesh);

    return StaticMesh;
}

FMeshDescription UOmnigenImporter::GenerateTerrainChunkMeshDescription(const Omnigen::TerrainChunk& Chunk)
{
    FMeshDescription MeshDescription;

    FStaticMeshAttributes AttributeGetter(MeshDescription);
    AttributeGetter.Register();

    auto MeshPositions = AttributeGetter.GetVertexPositions();

    auto MeshTangents = AttributeGetter.GetVertexInstanceTangents();
    auto MeshBinormalSigns = AttributeGetter.GetVertexInstanceBinormalSigns();
    auto MeshNormals = AttributeGetter.GetVertexInstanceNormals();
    auto MeshColors = AttributeGetter.GetVertexInstanceColors();
    auto MeshUVs = AttributeGetter.GetVertexInstanceUVs();

    FPolygonGroupID PolygonGroupID = MeshDescription.CreatePolygonGroup();

    MeshDescription.ReserveNewVertices(Chunk.vertices.Num());
    MeshDescription.ReserveNewVertexInstances(Chunk.indices.Num());
    MeshDescription.ReserveNewPolygons(Chunk.indices.Num() / 3);
    MeshDescription.ReserveNewEdges(Chunk.indices.Num() / 3 * 2);
    MeshUVs.SetNumChannels(4);

    TArray<FVertexID> VertexIDs;
    VertexIDs.SetNum(Chunk.vertices.Num());

    for (int32 i = 0; i < Chunk.vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        FVertexID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);

        MeshPositions[VertexID] = Chunk.vertices[i].position;
        MeshNormals[VertexInstanceID] = Chunk.vertices[i].normal;

        // Unpacking data
        FVector4f RockWeights = FVector4f(0, 1, 0, 0);
        RockWeights[0] = float((Chunk.vertices[i].terrainTexWeights & (0xFFu << 0u)) >> 0u) / 255.0f;
        RockWeights[1] = float((Chunk.vertices[i].terrainTexWeights & (0xFFu << 8u)) >> 8u) / 255.0f;
        RockWeights[2] = float((Chunk.vertices[i].terrainTexWeights & (0xFFu << 16u)) >> 16u) / 255.0f;
        RockWeights[3] = float((Chunk.vertices[i].terrainTexWeights & (0xFFu << 24u)) >> 24u) / 255.0f;

        FVector4f CoverWeights = FVector4f(0, 0, 0, 0);
        CoverWeights[0] = ((Chunk.vertices[i].coverTexWeights & (0xFF << 0)) >> 0) / 255.0f;
        CoverWeights[1] = ((Chunk.vertices[i].coverTexWeights & (0xFF << 8)) >> 8) / 255.0f;
        CoverWeights[2] = ((Chunk.vertices[i].coverTexWeights & (0xFF << 16)) >> 16) / 255.0f;
        CoverWeights[3] = ((Chunk.vertices[i].coverTexWeights & (0xFF << 24)) >> 24) / 255.0f;

        FVector4f PackParams = FVector4f(0, 0, 0, 0);
        auto&& RockSlot = PackParams[0] = ((Chunk.vertices[i].packParams & (0xFF << 0)) >> 0) / 255.0f;
        auto&& FoliageSeed = PackParams[1] = ((Chunk.vertices[i].packParams & (0xFF << 8)) >> 8) / 255.0f;
        auto&& UnusedA = PackParams[2] = ((Chunk.vertices[i].packParams & (0xFF << 16)) >> 16) / 255.0f;
        auto&& UnusedB = PackParams[3] = ((Chunk.vertices[i].packParams & (0xFF << 24)) >> 24) / 255.0f;

        // Custom treatment for PackParams
        RockSlot *= 2.0f;

        MeshColors.Set(VertexInstanceID, RockWeights);
        MeshUVs.Set(VertexInstanceID, 0, { CoverWeights[0], CoverWeights[1] });
        MeshUVs.Set(VertexInstanceID, 1, { CoverWeights[2], CoverWeights[3] });
        MeshUVs.Set(VertexInstanceID, 2, { RockSlot, FoliageSeed });
        MeshUVs.Set(VertexInstanceID, 3, { UnusedA, UnusedB });

        auto Tangent = FProcMeshTangent(0, 1, 0);
        MeshTangents[VertexInstanceID] = FVector3f(Tangent.TangentX);
        MeshBinormalSigns[VertexInstanceID] = Tangent.bFlipTangentY ? -1.f : 1.f;

        VertexIDs[i] = VertexInstanceID;
    }

    // Create the polygons for this section
    for (int32 ti = 0; ti < Chunk.indices.Num(); ti += 3)
    {
        int32 i0 = Chunk.indices[ti + 0];
        int32 i1 = Chunk.indices[ti + 1];
        int32 i2 = Chunk.indices[ti + 2];

        // Insert a polygon into the mesh
        MeshDescription.CreatePolygon(PolygonGroupID, { VertexIDs[i0], VertexIDs[i1], VertexIDs[i2] });
    }

    return MeshDescription;
}

FMeshDescription UOmnigenImporter::GeneratePlantMeshDescription(const Omnigen::PlantVariation::Geometry& Geometry, const TArray<UMaterialInstance*>& MaterialInstances)
{
    FMeshDescription MeshDescription;

    FStaticMeshAttributes AttributeGetter(MeshDescription);
    AttributeGetter.Register();

    auto MeshPositions = AttributeGetter.GetVertexPositions();

    auto MeshTangents = AttributeGetter.GetVertexInstanceTangents();
    auto MeshBinormalSigns = AttributeGetter.GetVertexInstanceBinormalSigns();
    auto MeshNormals = AttributeGetter.GetVertexInstanceNormals();
    auto MeshColors = AttributeGetter.GetVertexInstanceColors();
    auto MeshUVs = AttributeGetter.GetVertexInstanceUVs();

    auto MaterialSlotNames = AttributeGetter.GetPolygonGroupMaterialSlotNames();

    MeshDescription.ReserveNewVertices(Geometry.vertices.Num());
    MeshDescription.ReserveNewVertexInstances(Geometry.indices.Num());
    MeshDescription.ReserveNewPolygons(Geometry.indices.Num() / 3);
    MeshDescription.ReserveNewEdges(Geometry.indices.Num() / 3 * 2);
    MeshUVs.SetNumChannels(1);

    TArray<FVertexID> VertexIDs;
    VertexIDs.SetNum(Geometry.vertices.Num());

    auto TangentData = FProcMeshTangent(0, 1, 0);
    FVector3f Tangent(TangentData.TangentX);
    float BinormalSign = TangentData.bFlipTangentY ? -1.f : 1.f;

    for (int32 i = 0; i < Geometry.vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        FVertexID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);

        MeshPositions[VertexID] = Geometry.vertices[i].position;
        MeshNormals[VertexInstanceID] = Geometry.vertices[i].normal;
        MeshUVs.Set(VertexInstanceID, 0, Geometry.vertices[i].uv);
        MeshTangents[VertexInstanceID] = Tangent;
        MeshBinormalSigns[VertexInstanceID] = BinormalSign;

        VertexIDs[i] = VertexInstanceID;
    }

    TArray<FPolygonGroupID> PolygonGroupIDs;

    // Create the polygons for this section
    for (int32 ti = 0; ti < Geometry.indices.Num(); ti += 3)
    {
        int32 i0 = Geometry.indices[ti + 0];
        int32 i1 = Geometry.indices[ti + 1];
        int32 i2 = Geometry.indices[ti + 2];

        int32 MaterialIdx = Geometry.vertices[i0].materialID;
        while (PolygonGroupIDs.Num() <= MaterialIdx)
            PolygonGroupIDs.Add(MeshDescription.CreatePolygonGroup());

        // Insert a polygon into the mesh
        MeshDescription.CreatePolygon(PolygonGroupIDs[MaterialIdx], { VertexIDs[i0], VertexIDs[i1], VertexIDs[i2] });
    }

    // Assign materials
    for (int32 i = 0; i < PolygonGroupIDs.Num(); ++i)
        MaterialSlotNames[PolygonGroupIDs[i]] = MaterialInstances[i]->GetFName();

    return MeshDescription;
}

FMeshDescription UOmnigenImporter::GenerateRiverMeshDescription(const Omnigen::WaterMesh& Geometry)
{
    FMeshDescription MeshDescription;

    FStaticMeshAttributes AttributeGetter(MeshDescription);
    AttributeGetter.Register();

    auto MeshPositions = AttributeGetter.GetVertexPositions();
    MeshDescription.ReserveNewVertices(Geometry.vertices.Num());

    TArray<FVertexID> VertexIDs;
    VertexIDs.SetNum(Geometry.vertices.Num());

    for (int32 i = 0; i < Geometry.vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        FVertexID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);

        MeshPositions[VertexID] = Geometry.vertices[i].position;
        VertexIDs[i] = VertexInstanceID;
    }

    FPolygonGroupID PolygonGroupID = MeshDescription.CreatePolygonGroup();

    // Create the polygons for this section
    // Quad conversion
    for (int32 ti = 0; ti < Geometry.indices.Num(); ti += 4)
    {
        int32 i0 = Geometry.indices[ti + 0];
        int32 i1 = Geometry.indices[ti + 1];
        int32 i2 = Geometry.indices[ti + 2];
        int32 i3 = Geometry.indices[ti + 3];

        // Insert a polygon into the mesh
        MeshDescription.CreatePolygon(PolygonGroupID, { VertexIDs[i0], VertexIDs[i1], VertexIDs[i2] });
        MeshDescription.CreatePolygon(PolygonGroupID, { VertexIDs[i0], VertexIDs[i2], VertexIDs[i3] });
    }

    return MeshDescription;
}

FMeshDescription UOmnigenImporter::GenerateLakeMeshDescription(const Omnigen::WaterMesh& Geometry)
{
    FMeshDescription MeshDescription;

    FStaticMeshAttributes AttributeGetter(MeshDescription);
    AttributeGetter.Register();

    auto MeshPositions = AttributeGetter.GetVertexPositions();
    MeshDescription.ReserveNewVertices(Geometry.vertices.Num());

    TArray<FVertexID> VertexIDs;
    VertexIDs.SetNum(Geometry.vertices.Num());

    for (int32 i = 0; i < Geometry.vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        FVertexID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);

        MeshPositions[VertexID] = Geometry.vertices[i].position;
        VertexIDs[i] = VertexInstanceID;
    }

    FPolygonGroupID PolygonGroupID = MeshDescription.CreatePolygonGroup();

    // Create the polygons for this section
    for (int32 ti = 0; ti < Geometry.indices.Num(); ti += 3)
    {
        int32 i0 = Geometry.indices[ti + 0];
        int32 i1 = Geometry.indices[ti + 1];
        int32 i2 = Geometry.indices[ti + 2];

        // Insert a polygon into the mesh
        MeshDescription.CreatePolygon(PolygonGroupID, { VertexIDs[i0], VertexIDs[i1], VertexIDs[i2] });
    }

    return MeshDescription;
}

FMeshDescription UOmnigenImporter::GenerateOceanMeshDescription()
{
    FMeshDescription MeshDescription;

    FStaticMeshAttributes AttributeGetter(MeshDescription);
    AttributeGetter.Register();

    auto MeshPositions = AttributeGetter.GetVertexPositions();
    MeshDescription.ReserveNewVertices(4);

    TArray<FVertexID> VertexIDs;
    VertexIDs.SetNum(4);

    for (int32 i = 0; i < 4; ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        FVertexID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);
        VertexIDs[i] = VertexInstanceID;
    }

    // Create a quad

    // 300m margin from the edges
    MeshPositions[VertexIDs[0]] = { MinCoords.X - GlobalCenter.X - 30000.f, MinCoords.Z - GlobalCenter.Z - 30000.f, 0.f };
    MeshPositions[VertexIDs[1]] = { MaxCoords.X - GlobalCenter.X + 30000.f, MinCoords.Z - GlobalCenter.Z - 30000.f, 0.f };
    MeshPositions[VertexIDs[2]] = { MaxCoords.X - GlobalCenter.X + 00000.f, MaxCoords.Z - GlobalCenter.Z + 30000.f, 0.f };
    MeshPositions[VertexIDs[3]] = { MinCoords.X - GlobalCenter.X - 30000.f, MaxCoords.Z - GlobalCenter.Z + 30000.f, 0.f };

    FPolygonGroupID PolygonGroupID = MeshDescription.CreatePolygonGroup();

    MeshDescription.CreatePolygon(PolygonGroupID, { VertexIDs[0], VertexIDs[1], VertexIDs[2] });
    MeshDescription.CreatePolygon(PolygonGroupID, { VertexIDs[0], VertexIDs[2], VertexIDs[3] });

    return MeshDescription;
}

FTerrainMaterialTextureData UOmnigenImporter::CreateTerrainMaterialTextureData(const TArray<TArray<Omnigen::Material>>& Materials, FStringView MaterialTypeName)
{
    const TArray<Omnigen::ETextureComponent> SupportedComponents =
    {
        Omnigen::ETextureComponent::Diffuse,
        Omnigen::ETextureComponent::Normal
        // TODO: AOR
    };

    FTerrainMaterialTextureData OutData;

    UE_LOG(LogTemp, Warning, TEXT("%s"), *(FString(TEXT("Loading ")) + MaterialTypeName + TEXT(" Textures")));

    static int32 NameCounter = 0;

	// Async transcribe input buffers
    TArray<const Omnigen::Texture*> OmnigenTexturesToProcess;
    for (auto&& Material : Materials)
        for (auto&& SubMaterial : Material)
            for (auto&& [Component, OmnigenTexture] : SubMaterial.Textures)
                OmnigenTexturesToProcess.Add(&OmnigenTexture);

    RunThreaded(OmnigenTexturesToProcess.Num(), 1, [&](int32 Idx)
        {
            Texture_TranscribeBuffer(OmnigenTexturesToProcess[Idx]);
        });

    TMap<ETextureComponent, TArray<TWeakObjectPtr<UTexture2D>>> SourceTextures;
    TMap<ETextureComponent, TArray<const Omnigen::Texture*>> OmnigenTextures;
    for (auto Key : SupportedComponents)
    {
        SourceTextures.Add(Key);
        OmnigenTextures.Add(Key);
    }

    // Sync create objects & init scalars
    for (auto&& Material : Materials)
		for (auto&& SubMaterial : Material)
		{
			for (auto&& [Component, OmnigenTexture] : SubMaterial.Textures)
				if (SourceTextures.Contains(Component))
				{
					SourceTextures[Component].Add(Texture_CreateObject(OmnigenTexture.Width, OmnigenTexture.Height));
					OmnigenTextures[Component].Add(&OmnigenTexture);
				}

            OutData.TileSizes.Add(SubMaterial.TileSize);
            OutData.MaxDisplacements.Add(SubMaterial.MaxDisplacement);
		}

    // Async fill textures
    for (auto&& [Key, Textures] : SourceTextures)
        RunThreaded(Textures.Num(), 1, [&](int32 Idx)
            {
                auto& UnrealTexture = Textures[Idx];
                auto& OmnigenTexture = OmnigenTextures[Key][Idx];
                Texture_Fill(*OmnigenTexture, UnrealTexture.Get(), false);
            });

    FScopedSlowTask Feedback(SupportedComponents.Num(), FText::FromString(FString(TEXT("Building ")) + MaterialTypeName + TEXT(" Texture Arrays")));
    Feedback.MakeDialog();
	for (int32 i = 0; i < SupportedComponents.Num(); ++i)
	{
		Feedback.EnterProgressFrame(1);
		OutData.ComponentArrays.FindOrAdd(SupportedComponents[i]) = CreateNewTexture2DArray(MaterialTypeName + FString::FromInt(i), SourceTextures[SupportedComponents[i]]);
	}
	
	return OutData;
}

TArray<TMap<ETextureComponent, UTexture2D*>> UOmnigenImporter::CreatePlantMaterialTextureData(const Omnigen::Plant& Plant)
{
    const TArray<Omnigen::ETextureComponent> SupportedComponents =
    {
        Omnigen::ETextureComponent::Diffuse,
        Omnigen::ETextureComponent::Normal
        // TODO: AOR
    };

    TArray<TMap<ETextureComponent, UTexture2D*>> OutData;
    static int32 NameCounter = 0;

    // Async transcribe input buffers
    TArray<const Omnigen::Texture*> OmnigenTexturesToProcess;
    for (auto&& Material : Plant.materials)
        for (auto&& [Component, OmnigenTexture] : Material.Textures)
            OmnigenTexturesToProcess.Add(&OmnigenTexture);

    RunThreaded(OmnigenTexturesToProcess.Num(), 1, [&](int32 Idx)
        {
            Texture_TranscribeBuffer(OmnigenTexturesToProcess[Idx]);
        });

    // Sync create objects & init scalars
    TMap<ETextureComponent, TArray<TWeakObjectPtr<UTexture2D>>> SourceTextures;
    TMap<ETextureComponent, TArray<const Omnigen::Texture*>> OmnigenTextures;
    for (auto Key : SupportedComponents)
    {
        SourceTextures.Add(Key);
        OmnigenTextures.Add(Key);
    }

    for (auto&& Material : Plant.materials)
    {
        auto&& OutMaterial = OutData.AddDefaulted_GetRef();
        for (int32 i = 0; i < SupportedComponents.Num(); ++i)
            OutMaterial.Add(SupportedComponents[i]);

        for (auto&& [Component, OmnigenTexture] : Material.Textures)
            if (SourceTextures.Contains(Component))
            {
                auto* Texture = Texture_CreateObject(OmnigenTexture.Width, OmnigenTexture.Height, Plant.name + "Component" + FString::FromInt(NameCounter++), FString::Printf(TEXT("Foliage/%s"), *Plant.name));
                OmnigenAsset->FoliageTextures.Add(Texture);
                OutMaterial[Component] = Texture;

                SourceTextures[Component].Add(Texture);
                OmnigenTextures[Component].Add(&OmnigenTexture);
            }
    }

    // Async fill textures
    for (auto&& [Key, Textures] : SourceTextures)
    {
        RunThreaded(Textures.Num(), 1, [&](int32 Idx)
            {
                auto& UnrealTexture = Textures[Idx];
                auto& OmnigenTexture = OmnigenTextures[Key][Idx];
                Texture_Fill(*OmnigenTexture, UnrealTexture.Get(), false);
            });

        for (auto&& Texture : Textures)
        {
            Texture.Get()->UpdateResource();
            Texture.Get()->PostEditChange();
        }
    }

    return OutData;
}

void UOmnigenImporter::Texture_TranscribeBuffer(const Omnigen::Texture* InTexture)
{
    //FImageUtils::ImportBufferAsTexture2D
    auto* Texture = const_cast<Omnigen::Texture*>(InTexture);
    auto& Buffer = Texture->InputTextureBuffer;

    IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

    EImageFormat Format = ImageWrapperModule.DetectImageFormat(Buffer.GetData(), Buffer.Num());
    check(Format == EImageFormat::PNG);

    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
    bool bReadCompressedData = ImageWrapper->SetCompressed((void*)Buffer.GetData(), Buffer.Num());
    check(bReadCompressedData);

    Texture->Width = ImageWrapper->GetWidth();
    Texture->Height = ImageWrapper->GetHeight();

    ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, Texture->UncompressedPixelData);

    // Input data no longer needed.
    Texture->InputTextureBuffer.Empty();
}

UTexture2D* UOmnigenImporter::Texture_CreateObject(int32 Width, int32 Height, TOptional<FString> SaveName, TOptional<FString> SavePath)
{
    auto MakeTextureAsset = [&]()
    {
        auto* TextureFactory = NewObject<UTexture2DFactoryNew>();
        TextureFactory->Width = Width;
        TextureFactory->Height = Height;

        UTexture2D* NewTexture = static_cast<UTexture2D*>(CreateAssetWithPackage(*SaveName, *SavePath, UTexture2D::StaticClass(), TextureFactory));

        NewTexture->bNotOfflineProcessed = true;
        NewTexture->Filter = TF_Bilinear;
        NewTexture->MipGenSettings = TMGS_FromTextureGroup;
        NewTexture->CompressionSettings = TC_Default;
        NewTexture->SRGB = true;

        return NewTexture;
    };

    auto MakeTextureTransient = [&]()
    {
        LLM_SCOPE(ELLMTag::Textures);

        UTexture2D* NewTexture = NewObject<UTexture2D>(GetTransientPackage(), FName(NAME_None), RF_Transient);

        NewTexture->SetPlatformData(new FTexturePlatformData());
        NewTexture->GetPlatformData()->SizeX = Width;
        NewTexture->GetPlatformData()->SizeY = Height;
        NewTexture->GetPlatformData()->SetNumSlices(1);
        NewTexture->GetPlatformData()->PixelFormat = TexturePixelFormat;

        NewTexture->AddToRoot();

        return NewTexture;
    };

    // Create object of appropriate type
    return SaveName ? MakeTextureAsset() : MakeTextureTransient();
}

void UOmnigenImporter::Texture_Fill(const Omnigen::Texture& Texture, UTexture2D* UnrealTexture, bool IsAsset)
{
    // Fill with data
    int32 NumBlocksX = Texture.Width / GPixelFormats[TexturePixelFormat].BlockSizeX;
    int32 NumBlocksY = Texture.Height / GPixelFormats[TexturePixelFormat].BlockSizeY;
    FTexture2DMipMap* Mip = new FTexture2DMipMap();
    UnrealTexture->GetPlatformData()->Mips.Add(Mip);
    Mip->SizeX = Texture.Width;
    Mip->SizeY = Texture.Height;
    Mip->SizeZ = 1;
    Mip->BulkData.Lock(LOCK_READ_WRITE);
    Mip->BulkData.Realloc((int64)NumBlocksX * NumBlocksY * GPixelFormats[TexturePixelFormat].BlockBytes);
    Mip->BulkData.Unlock();

    UnrealTexture->Source.Init(Texture.Width, Texture.Height, 1, 1, ETextureSourceFormat::TSF_BGRA8, Texture.UncompressedPixelData.GetData());

    if (IsAsset)
    {
        UnrealTexture->UpdateResource();
        UnrealTexture->PostEditChange();
    }
}

UTexture2DArray* UOmnigenImporter::CreateNewTexture2DArray(const FString& Name, TArray<TWeakObjectPtr<UTexture2D>> Objects)
{
    //Package creation
    FString AssetName = FString::Printf(TEXT("T2DA_%s_%s"), *FilenameNoExtension, *Name);

    // Create the factory used to generate the asset.
    UTexture2DArrayFactory* Factory = NewObject<UTexture2DArrayFactory>();
    Factory->InitialTextures.Empty();

    // Give the selected textures to the factory.
    for (int32 TextureIndex = 0; TextureIndex < Objects.Num(); ++TextureIndex)
    {
        Factory->InitialTextures.Add(Objects[TextureIndex].Get());
    }

    auto* TextureArray = static_cast<UTexture2DArray*>(CreateAssetWithPackage(AssetName, "Terrain/Materials", UTexture2DArray::StaticClass(), Factory));
	TextureArray->UpdateResource();

	return TextureArray;
}

UMaterial* UOmnigenImporter::CreateTerrainMaterial()
{
    auto TerrainMaterial = static_cast<UMaterial*>(CreateAssetWithPackage(TEXT("M_OmnigenTerrain"), TEXT("Terrain/Materials"), UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>()));
    TerrainMaterial->PreEditChange(nullptr);

    const TArray<float> ColumnX = { -1000, -750, -500, -100 };

    // HLSL Node
    auto* HLSLExpression = NewObject<UMaterialExpressionCustom>(TerrainMaterial);
    HLSLExpression->MaterialExpressionGuid = FGuid::NewGuid();
    HLSLExpression->MaterialExpressionEditorX = ColumnX[2];
    HLSLExpression->Description = "OmnigenTerrainShader";
    HLSLExpression->Inputs.Reset();
    FString ShaderPath = FPaths::ProjectPluginsDir() + "Omnigen/Shaders/TerrainShader.hlsl";
    FFileHelper::LoadFileToString(HLSLExpression->Code, *ShaderPath);
    HLSLExpression->OutputType = ECustomMaterialOutputType::CMOT_Float1;

    auto&& Expressions = TerrainMaterial->GetEditorOnlyData()->ExpressionCollection.Expressions;
    Expressions.Add(HLSLExpression);

    HLSLExpression->PreEditChange(nullptr);

    // General settings
    TerrainMaterial->bTangentSpaceNormal = false;

    TArray<TArray<UMaterialExpression*>> ExpressionsByRow;

    // Inputs
    {
        // Rock Diffuse Array
        auto* InputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_RockDiffuseHeightArray);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        InputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
        InputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color;
        InputExpression->Texture = Cast<UTexture>(UTexture2DArray::StaticClass()->GetDefaultObject());
        Expressions.Add(InputExpression);

        ExpressionsByRow.Add({ InputExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, InputExpression);
    }
    {
        // Rock Normal Array
        auto* InputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_RockNormalArray);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        InputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
        InputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
        InputExpression->Texture = Cast<UTexture>(UTexture2DArray::StaticClass()->GetDefaultObject());
        Expressions.Add(InputExpression);

        ExpressionsByRow.Add({ InputExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, InputExpression);
    }
    {
        // Rock Tile Sizes
        auto* InputExpression = NewObject<UMaterialExpressionVectorParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_RockTileSizes);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        InputExpression->MaterialExpressionEditorY = -800;
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionGuid = FGuid::NewGuid();
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Rock Max Displacements
        auto* InputExpression = NewObject<UMaterialExpressionVectorParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_RockMaxDisplacements);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Rock Tex IDs
        auto* InputExpression = NewObject<UMaterialExpressionVectorParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_RockTexIDs);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Rock Tex Weights
        auto* InputExpression = NewObject<UMaterialExpressionVertexColor>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->Desc = TEXT("RockTexWeights");
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionGuid = FGuid::NewGuid();
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = FName(InputExpression->Desc);
        Input.Input.InputName = FName(InputExpression->Desc);
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Rock Slot
        auto* InputExpression = NewObject<UMaterialExpressionTextureCoordinate>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->CoordinateIndex = 2;
        InputExpression->Desc = TEXT("PackParams01");
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* MaskExpression = NewObject<UMaterialExpressionComponentMask>(TerrainMaterial);
        MaskExpression->MaterialExpressionGuid = FGuid::NewGuid();
        MaskExpression->MaterialExpressionEditorX = ColumnX[1];
        MaskExpression->Input.Connect(0, InputExpression);
        MaskExpression->R = true;
        Expressions.Add(MaskExpression);

        ExpressionsByRow.Add({ InputExpression, MaskExpression });

        FCustomInput Input;
        Input.InputName = TEXT("RockSlot");
        Input.Input.InputName = TEXT("RockSlot");
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, MaskExpression);
    }
    {
        // Cover Diffuse Array
        auto* InputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_CoverDiffuseHeightArray);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        InputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
        InputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color;
        InputExpression->Texture = Cast<UTexture>(UTexture2DArray::StaticClass()->GetDefaultObject());
        Expressions.Add(InputExpression);

        ExpressionsByRow.Add({ InputExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, InputExpression);
    }
    {
        // Cover Normal Array
        auto* InputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_CoverNormalArray);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        InputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
        InputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
        InputExpression->Texture = Cast<UTexture>(UTexture2DArray::StaticClass()->GetDefaultObject());
        Expressions.Add(InputExpression);

        ExpressionsByRow.Add({ InputExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, InputExpression);
    }
    {
        // Cover Tile Sizes
        auto* InputExpression = NewObject<UMaterialExpressionVectorParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_CoverTileSizes);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionGuid = FGuid::NewGuid();
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Cover Max Displacements
        auto* InputExpression = NewObject<UMaterialExpressionVectorParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_CoverMaxDisplacements);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Cover Tex IDs
        auto* InputExpression = NewObject<UMaterialExpressionVectorParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_CoverTexIDs);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpression);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpression); // RGB
        AppendExpression->B.Connect(4, InputExpression); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpression, AppendExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Cover Tex Weights
        auto* InputExpressionA = NewObject<UMaterialExpressionTextureCoordinate>(TerrainMaterial);
        InputExpressionA->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpressionA->CoordinateIndex = 0;
        InputExpressionA->Desc = TEXT("CoverTexWeights01");
        InputExpressionA->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpressionA);

        auto* InputExpressionB = NewObject<UMaterialExpressionTextureCoordinate>(TerrainMaterial);
        InputExpressionB->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpressionB->CoordinateIndex = 1;
        InputExpressionB->Desc = TEXT("CoverTexWeights23");
        InputExpressionB->MaterialExpressionEditorX = ColumnX[0];
        Expressions.Add(InputExpressionB);

        auto* AppendExpression = NewObject<UMaterialExpressionAppendVector>(TerrainMaterial);
        AppendExpression->MaterialExpressionGuid = FGuid::NewGuid();
        AppendExpression->MaterialExpressionEditorX = ColumnX[1];
        AppendExpression->A.Connect(0, InputExpressionA); // RGB
        AppendExpression->B.Connect(0, InputExpressionB); // A
        Expressions.Add(AppendExpression);

        ExpressionsByRow.Add({ InputExpressionA, AppendExpression });
        ExpressionsByRow.Add({ InputExpressionB });

        FCustomInput Input;
        Input.InputName = "CoverTexWeights";
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, AppendExpression);
    }
    {
        // Tiling Noise
        auto* InputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(TerrainMaterial);
        InputExpression->MaterialExpressionGuid = FGuid::NewGuid();
        InputExpression->SetParameterName(TerrainMaterial_TilingNoise);
        InputExpression->MaterialExpressionEditorX = ColumnX[0];
        InputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
        InputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Alpha;
        Expressions.Add(InputExpression);

        ExpressionsByRow.Add({ InputExpression });

        FCustomInput Input;
        Input.InputName = InputExpression->GetParameterName();
        Input.Input.InputName = Input.InputName;
        HLSLExpression->Inputs.Add_GetRef(Input).Input.Connect(0, InputExpression);
    }

    // Autoposition Y
    const int32 NodeHeight = 200;
    int32 TotalHeight = NodeHeight * ExpressionsByRow.Num();
    int32 BeginY = -TotalHeight / 2;
    for (int32 i = 0; i < ExpressionsByRow.Num(); ++i)
        for (auto* Expression : ExpressionsByRow[i])
            Expression->MaterialExpressionEditorY = BeginY + i * NodeHeight;

    // Outputs
    {
        // Color
        FCustomOutput Output;
        Output.OutputName = "OutputColor";
        Output.OutputType = ECustomMaterialOutputType::CMOT_Float4;
        HLSLExpression->AdditionalOutputs.Add(Output);

    }
    {
        // Normal
        FCustomOutput Output;
        Output.OutputName = "OutputNormal";
        Output.OutputType = ECustomMaterialOutputType::CMOT_Float3;
        HLSLExpression->AdditionalOutputs.Add(Output);
    }

    HLSLExpression->PostEditChange();

    // Material output binding
    TerrainMaterial->GetEditorOnlyData()->BaseColor.Connect(1, HLSLExpression);
    TerrainMaterial->GetEditorOnlyData()->Normal.Connect(2, HLSLExpression);

    // Roughness
    auto* RoughnessExpression = NewObject<UMaterialExpressionConstant>(TerrainMaterial);
    RoughnessExpression->MaterialExpressionGuid = FGuid::NewGuid();
    RoughnessExpression->MaterialExpressionEditorY = 100;
    RoughnessExpression->MaterialExpressionEditorX = ColumnX[3];
    RoughnessExpression->R = 1.0f;
    Expressions.Add(RoughnessExpression);
    TerrainMaterial->GetEditorOnlyData()->Roughness.Connect(0, RoughnessExpression);

    // Let the material update itself if necessary
    TerrainMaterial->PostEditChange();
    // make sure that any static meshes, etc using this material will stop using the FMaterialResource of the original
    // material, and will use the new FMaterialResource created when we make a new UMaterial in place
    FGlobalComponentReregisterContext RecreateComponents;

    OmnigenAsset->TerrainMasterMaterial = TerrainMaterial;
    return TerrainMaterial;
}

UMaterial* UOmnigenImporter::CreatePlantMaterial()
{
    auto PlantMaterial = static_cast<UMaterial*>(CreateAssetWithPackage(TEXT("M_OmnigenPlant"), TEXT("Foliage"), UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>()));
    PlantMaterial->PreEditChange(nullptr);

    const TArray<float> ColumnX = { -1000, -750, -500, -100 };
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    // General settings
    PlantMaterial->BlendMode = EBlendMode::BLEND_Masked;
    PlantMaterial->SetShadingModel(EMaterialShadingModel::MSM_TwoSidedFoliage);
    PlantMaterial->TwoSided = true;
    PlantMaterial->bTangentSpaceNormal = false;

    auto&& Expressions = PlantMaterial->GetEditorOnlyData()->ExpressionCollection.Expressions;

    // DiffuseAlpha
    auto* DiffuseAlphaInputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(PlantMaterial);
    DiffuseAlphaInputExpression->MaterialExpressionGuid = FGuid::NewGuid();
    DiffuseAlphaInputExpression->SetParameterName(PlantMaterial_DiffuseAlpha);
    DiffuseAlphaInputExpression->MaterialExpressionEditorX = ColumnX[0];
    DiffuseAlphaInputExpression->MaterialExpressionEditorY = -300;
    DiffuseAlphaInputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
    DiffuseAlphaInputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color;
    DiffuseAlphaInputExpression->Texture = DefaultDiffuseTexture;
    Expressions.Add(DiffuseAlphaInputExpression);

    // Normal
    auto* NormalInputExpression = NewObject<UMaterialExpressionTextureObjectParameter>(PlantMaterial);
    NormalInputExpression->MaterialExpressionGuid = FGuid::NewGuid();
    NormalInputExpression->SetParameterName(PlantMaterial_Normal);
    NormalInputExpression->MaterialExpressionEditorX = ColumnX[0];
    NormalInputExpression->MaterialExpressionEditorY = 300;
    NormalInputExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
    NormalInputExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
    NormalInputExpression->Texture = DefaultNormalTexture;
    Expressions.Add(NormalInputExpression);

    // Tex Coords
    auto* TexCoordsInputExpression = NewObject<UMaterialExpressionTextureCoordinate>(PlantMaterial);
    TexCoordsInputExpression->MaterialExpressionGuid = FGuid::NewGuid();
    TexCoordsInputExpression->MaterialExpressionEditorX = ColumnX[0];
    TexCoordsInputExpression->MaterialExpressionEditorY = 0;
    Expressions.Add(TexCoordsInputExpression);

    // Sample DiffuseAlpha
    auto* DiffuseAlphaSampleExpression = NewObject<UMaterialExpressionTextureSample>(PlantMaterial);
    DiffuseAlphaSampleExpression->MaterialExpressionGuid = FGuid::NewGuid();
    DiffuseAlphaSampleExpression->MaterialExpressionEditorX = ColumnX[1];
    DiffuseAlphaSampleExpression->MaterialExpressionEditorY = -300;
    DiffuseAlphaSampleExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
    DiffuseAlphaSampleExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color;
    Expressions.Add(DiffuseAlphaSampleExpression);

    DiffuseAlphaSampleExpression->GetInput(0)->Connect(0, TexCoordsInputExpression);
    DiffuseAlphaSampleExpression->GetInput(1)->Connect(0, DiffuseAlphaInputExpression);

    // Sample Normal
    auto* NormalSampleExpression = NewObject<UMaterialExpressionTextureSample>(PlantMaterial);
    NormalSampleExpression->MaterialExpressionGuid = FGuid::NewGuid();
    NormalSampleExpression->MaterialExpressionEditorX = ColumnX[1];
    NormalSampleExpression->MaterialExpressionEditorY = 300;
    NormalSampleExpression->SamplerSource = ESamplerSourceMode::SSM_FromTextureAsset;
    NormalSampleExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
    Expressions.Add(NormalSampleExpression);

    NormalSampleExpression->GetInput(0)->Connect(0, TexCoordsInputExpression);
    NormalSampleExpression->GetInput(1)->Connect(0, NormalInputExpression);

    // Material output binding
    PlantMaterial->GetEditorOnlyData()->BaseColor.Connect(0, DiffuseAlphaSampleExpression);
    PlantMaterial->GetEditorOnlyData()->Opacity.Connect(4, DiffuseAlphaSampleExpression);
    PlantMaterial->GetEditorOnlyData()->OpacityMask.Connect(4, DiffuseAlphaSampleExpression);
    PlantMaterial->GetEditorOnlyData()->Normal.Connect(0, NormalSampleExpression);

    // Specular
    auto* SpecularExpression = NewObject<UMaterialExpressionConstant>(PlantMaterial);
    SpecularExpression->MaterialExpressionGuid = FGuid::NewGuid();
    SpecularExpression->MaterialExpressionEditorY = 100;
    SpecularExpression->MaterialExpressionEditorX = ColumnX[3];
    SpecularExpression->R = 0.3f;
    Expressions.Add(SpecularExpression);
    PlantMaterial->GetEditorOnlyData()->Specular.Connect(0, SpecularExpression);

    // Let the material update itself if necessary
    PlantMaterial->PostEditChange();
    // make sure that any static meshes, etc using this material will stop using the FMaterialResource of the original
    // material, and will use the new FMaterialResource created when we make a new UMaterial in place
    FGlobalComponentReregisterContext RecreateComponents;

    OmnigenAsset->FoliageMasterMaterial = PlantMaterial;
    return PlantMaterial;
}

UMaterialInstanceConstant* UOmnigenImporter::CreateMaterialInstance(UMaterialInterface* BaseMaterial, const FString& Name, const FString& RelativePath)
{
    FString AssetName = FString::Printf(TEXT("%sInstancedMaterial"), *Name);
    auto* MaterialInstance = static_cast<UMaterialInstanceConstant*>(CreateAssetWithPackage(AssetName, RelativePath, UMaterialInstanceConstant::StaticClass(), NewObject<UMaterialInstanceConstantFactoryNew>()));
    check(MaterialInstance);

    MaterialInstance->SetFlags(RF_Standalone);
    MaterialInstance->SetParentEditorOnly(BaseMaterial);

    // Register for saving
    MaterialInstance->MarkPackageDirty();
    MaterialInstance->PostEditChange();

    return MaterialInstance;
}

UMaterialInstanceConstant* UOmnigenImporter::CreateTerrainMaterialInstance(const Omnigen::TerrainChunk& Chunk, const FString& ChunkName)
{
	if (!OmnigenAsset->TerrainMasterMaterial)
		OmnigenAsset->TerrainMasterMaterial = CreateTerrainMaterial();

	auto* MaterialInstance = CreateMaterialInstance(OmnigenAsset->TerrainMasterMaterial.Get(), ChunkName, TEXT("Terrain/Materials"));

	// Rock Texture Data
	FVector4f RockTextureIds;
	FVector4f RockTileSizes;
	FVector4f RockMaxDisplacements;
	for (size_t i = 0; i < Chunk.terrainTextureIds.Num(); ++i)
	{
		RockTextureIds[i] = Chunk.terrainTextureIds[i];
		RockTileSizes[i] = OmnigenAsset->RockTextureData.TileSizes[RockSubmaterialCount * RockTextureIds[i]];
		RockMaxDisplacements[i] = OmnigenAsset->RockTextureData.MaxDisplacements[RockSubmaterialCount * RockTextureIds[i]];
	}
	MaterialInstance->SetTextureParameterValueEditorOnly(TerrainMaterial_RockDiffuseHeightArray, OmnigenAsset->RockTextureData.ComponentArrays[Omnigen::ETextureComponent::Diffuse].Get());
	MaterialInstance->SetTextureParameterValueEditorOnly(TerrainMaterial_RockNormalArray, OmnigenAsset->RockTextureData.ComponentArrays[Omnigen::ETextureComponent::Normal].Get());
	MaterialInstance->SetVectorParameterValueEditorOnly(TerrainMaterial_RockTexIDs, RockTextureIds);
	MaterialInstance->SetVectorParameterValueEditorOnly(TerrainMaterial_RockTileSizes, RockTileSizes);
	MaterialInstance->SetVectorParameterValueEditorOnly(TerrainMaterial_RockMaxDisplacements, RockMaxDisplacements);

	// Cover Texture Data
	FVector4f CoverTextureIds;
	FVector4f CoverTileSizes;
	FVector4f CoverMaxDisplacements;
	for (size_t i = 0; i < Chunk.coverTextureIds.Num(); ++i)
	{
		CoverTextureIds[i] = Chunk.coverTextureIds[i];
		CoverTileSizes[i] = OmnigenAsset->CoverTextureData.TileSizes[CoverSubmaterialCount * CoverTextureIds[i]];
		CoverMaxDisplacements[i] = OmnigenAsset->CoverTextureData.MaxDisplacements[CoverSubmaterialCount * CoverTextureIds[i]];
	}
	MaterialInstance->SetTextureParameterValueEditorOnly(TerrainMaterial_CoverDiffuseHeightArray, OmnigenAsset->CoverTextureData.ComponentArrays[Omnigen::ETextureComponent::Diffuse].Get());
	MaterialInstance->SetTextureParameterValueEditorOnly(TerrainMaterial_CoverNormalArray, OmnigenAsset->CoverTextureData.ComponentArrays[Omnigen::ETextureComponent::Normal].Get());
	MaterialInstance->SetVectorParameterValueEditorOnly(TerrainMaterial_CoverTexIDs, CoverTextureIds);
	MaterialInstance->SetVectorParameterValueEditorOnly(TerrainMaterial_CoverTileSizes, CoverTileSizes);
	MaterialInstance->SetVectorParameterValueEditorOnly(TerrainMaterial_CoverMaxDisplacements, CoverMaxDisplacements);

	// Tiling noise
	MaterialInstance->SetTextureParameterValueEditorOnly(TerrainMaterial_TilingNoise, OmnigenAsset->TilingNoiseTexture.Get());

	// Register for saving
	MaterialInstance->MarkPackageDirty();
	MaterialInstance->PostEditChange();

	return MaterialInstance;
}

UMaterialInstanceConstant* UOmnigenImporter::CreatePlantMaterialInstance(const TMap<ETextureComponent, UTexture2D*>& MaterialData, const FString& PlantName)
{
    if (!OmnigenAsset->FoliageMasterMaterial)
        OmnigenAsset->FoliageMasterMaterial = CreatePlantMaterial();

    UMaterialInstanceConstant* MaterialInstance = CreateMaterialInstance(OmnigenAsset->FoliageMasterMaterial.Get(), PlantName, FString::Printf(TEXT("Foliage/%s"), *PlantName));
    OmnigenAsset->FoliageMaterialInstances.Add(MaterialInstance);

    MaterialInstance->SetFlags(RF_Standalone);
    UMaterialEditingLibrary::SetMaterialInstanceParent(MaterialInstance, OmnigenAsset->FoliageMasterMaterial.Get());

    // Rock Textures
    MaterialInstance->SetTextureParameterValueEditorOnly(PlantMaterial_DiffuseAlpha, MaterialData[ETextureComponent::Diffuse]);
    MaterialInstance->SetTextureParameterValueEditorOnly(PlantMaterial_Normal, MaterialData[ETextureComponent::Normal]);

    // Register for saving
    MaterialInstance->MarkPackageDirty();
    MaterialInstance->PostEditChange();

    return MaterialInstance;
}

void UOmnigenImporter::CreateFoliageTypeAndInstances(const Omnigen::Plant& Plant, const TArray<TMap<ETextureComponent, UTexture2D*>>& MaterialsData)
{
    IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    auto* CurrentWorld = GEditor->GetEditorWorldContext().World();
    check(CurrentWorld != nullptr);

    //CreateSpeedTreeMaterial7

    TArray<UMaterialInstance*> MaterialInstances;
    for (auto&& MaterialData : MaterialsData)
        MaterialInstances.Add(CreatePlantMaterialInstance(MaterialData, Plant.name));

    for (int32 i = 0; i < Plant.variations.Num(); ++i)
    {
        auto* FoliageType = CreateFoliageType(Plant, i, MaterialInstances);
        OmnigenAsset->FoliageTypes.Add(FoliageType);

        auto&& Variation = Plant.variations[i];
        auto&& ZeroLOD = Variation.LODs[Omnigen::ELOD::Zero];

        // Instances
        TMap<AInstancedFoliageActor*, TArray<const FFoliageInstance*>> InstancesToAdd;
        TArray<FFoliageInstance> FoliageInstances;
        FoliageInstances.Reserve(ZeroLOD.instanceTransforms.Num()); // Reserve 

        for (auto&& trs : ZeroLOD.instanceTransforms)
        {
            FFoliageInstance FoliageInstance;
            FoliageInstance.Location = FVector(trs.translation);
            FoliageInstance.Rotation = FRotator(FQuat4d(FVector3d(trs.rotationAxis), trs.rotationAngleRad));
            FoliageInstance.DrawScale3D = trs.scale;

            FoliageInstances.Add(FoliageInstance);
            auto* IFA = AInstancedFoliageActor::Get(CurrentWorld, true, CurrentWorld->GetCurrentLevel(), FoliageInstance.Location);
            InstancesToAdd.FindOrAdd(IFA).Add(&FoliageInstances[FoliageInstances.Num() - 1]);
        }

        for (const auto& Pair : InstancesToAdd)
        {
            FFoliageInfo* TypeInfo = nullptr;
            Pair.Key->AddFoliageType(FoliageType, &TypeInfo);
            TypeInfo->AddInstances(FoliageType, Pair.Value);
            TypeInfo->Refresh(true, false);
        }
    }
}

UFoliageType* UOmnigenImporter::CreateFoliageType(const Omnigen::Plant& Plant, int VariationIdx, const TArray<UMaterialInstance*>& MaterialInstances)
{
    UFoliageType* FoliageType = nullptr;
    auto* CurrentWorld = GEditor->GetEditorWorldContext().World();

    auto&& Variation = Plant.variations[VariationIdx];
    auto&& ZeroLOD = Variation.LODs[Omnigen::ELOD::Zero];

    // New foliage type
    MeshGenSettings GenSettings;
    GenSettings.UseNanite = true;
    GenSettings.LumenMeshCards = 4;
    GenSettings.LightmapResolution = 4;
	GenSettings.Name = FString::Printf(TEXT("%s%i"), *Plant.name, VariationIdx);
	GenSettings.Path = FString::Printf(TEXT("Foliage/%s/Variation%i"), *Plant.name, VariationIdx);

    UStaticMesh* ISM = GenerateStaticMesh(GeneratePlantMeshDescription(ZeroLOD, MaterialInstances), MaterialInstances, GenSettings);
	OmnigenAsset->FoliageMeshes.Add(ISM);

    if (CurrentWorld->IsPartitionedWorld())
    {
        // Must create as assets in this case
        FString AssetName = FString::Printf(TEXT("%s%iFoliageType"), *Plant.name, VariationIdx);
		auto* InstancedStaticMeshFoliageType = static_cast<UFoliageType_InstancedStaticMesh*>(CreateAssetWithPackage(AssetName, GenSettings.Path, UFoliageType_InstancedStaticMesh::StaticClass(), NewObject<UFoliageType_OmnigenFactory>()));

		InstancedStaticMeshFoliageType->SetStaticMesh(ISM);
        if (Plant.layer == Omnigen::EBiomeLayer::Floor)
            InstancedStaticMeshFoliageType->CastShadow = false;

		InstancedStaticMeshFoliageType->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(InstancedStaticMeshFoliageType);
        FoliageType = InstancedStaticMeshFoliageType;
    }
    else
    {
        // Use the existing (pre-world partition) Foliage API to add the foliage mesh.
        AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetDefault(CurrentWorld);
        IFA->AddMesh(ISM, &FoliageType);
    }

    return FoliageType;
}