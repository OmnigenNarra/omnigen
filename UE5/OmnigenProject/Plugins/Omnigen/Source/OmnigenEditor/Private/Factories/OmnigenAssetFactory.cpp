// Fill out your copyright notice in the Description page of Project Settings.


#include "Factories/OmnigenAssetFactory.h"
#include "OmnigenAsset.h"
#include "Misc/FileHelper.h"
#include "Containers/UnrealString.h"
#include "UObject/UObjectGlobals.h"
#include "AssetViewUtils.h"

#include "OmnigenImporter.h"
#include "Engine/Texture2DArray.h"
#include "Engine/StaticMeshActor.h"
#include "UnrealEd/Public/ObjectTools.h"
#include "UnrealEd/Public/FileHelpers.h"


UOmnigenAssetFactory::UOmnigenAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Formats.Add(FString(TEXT("oef;")) + NSLOCTEXT("UOmnigenAssetFactory", "OmnigenExportFormat", "Omnigen Export Format").ToString());
	SupportedClass = UOmnigenAsset::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bAutomatedReimport = false;
}

UObject* UOmnigenAssetFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	const FString Filepath = FPaths::ConvertRelativePathToFull(Filename);
	const FString PackagePath = InParent->GetPathName();
	
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, TEXT("OEF"));
	const uint64 StartTime = FPlatformTime::Cycles64();

	UOmnigenAsset* OmnigenAsset = nullptr;
	FString TextString;
	OmnigenAsset = NewObject<UOmnigenAsset>(InParent, InClass, InName, Flags);
	OmnigenAsset->Path = Filename;

    OmnigenAsset->World = CreateWorldForAsset();
	
	UOmnigenImporter* Importer = NewObject<UOmnigenImporter>();
	Importer->LoadAssetFromFile(OmnigenAsset, Filename, false);

	bOutOperationCanceled = false;

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, OmnigenAsset);

	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
	//SendAnalytics(FMath::RoundToInt(ElapsedSeconds), bImported, Filename);

	// Log time spent to import incoming file in minutes and seconds
	const int ElapsedMin = int(ElapsedSeconds / 60.f);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;

	UE_LOG(LogTemp, Log, TEXT("Imported %s in [%d min %.3f s]"), *Filepath, ElapsedMin, ElapsedSeconds);

	return OmnigenAsset;
}

bool UOmnigenAssetFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	auto* OmnigenAsset = dynamic_cast<UOmnigenAsset*>(Obj);
	if (!OmnigenAsset)
		return false;

    IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	return FileManager.FileExists(*OmnigenAsset->Path);
}

EReimportResult::Type UOmnigenAssetFactory::Reimport(UObject* Obj)
{
	if (!Obj || !Obj->IsA(UOmnigenAsset::StaticClass()))
	{
		return EReimportResult::Failed;
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetReimport(Obj);
	UOmnigenAsset* OmnigenAsset = Cast<UOmnigenAsset>(Obj);

	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	if (!FileManager.FileExists(*OmnigenAsset->Path))
	{
		return EReimportResult::Failed;
	}

    // Make a snapshot of existing asset references

#define MakeDataCopy(Member) auto Member##Old = OmnigenAsset->##Member
    MakeDataCopy(TerrainMeshes);
    MakeDataCopy(TerrainMaterialInstances);
    MakeDataCopy(FoliageMeshes);
    MakeDataCopy(FoliageMaterialInstances);
    MakeDataCopy(FoliageTypes);
    MakeDataCopy(FoliageTextures);
    MakeDataCopy(WaterMeshes);
    auto RockTextureDataOld = OmnigenAsset->RockTextureData;
    auto CoverTextureDataOld = OmnigenAsset->CoverTextureData;

    OmnigenAsset->ClearForReimport();

	// Import again, will replace references to previous versions
	UOmnigenImporter* Importer = NewObject<UOmnigenImporter>();
    Importer->LoadAssetFromFile(OmnigenAsset, OmnigenAsset->Path, true);

    // Remove all previous assets that weren't replaced
    TArray<UObject*> AssetsToDelete;

    // Terrain data
    for (auto&& Asset : TerrainMeshesOld)
        if (!OmnigenAsset->TerrainMeshes.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

    for (auto&& Asset : TerrainMaterialInstancesOld)
        if (!OmnigenAsset->TerrainMaterialInstances.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

    for (auto&& [Comp, Asset] : RockTextureDataOld.ComponentArrays)
        if (!OmnigenAsset->RockTextureData.ComponentArrays.Contains(Comp) || OmnigenAsset->RockTextureData.ComponentArrays[Comp] != Asset.LoadSynchronous())
            AssetsToDelete.Add(Asset.Get());

    for (auto&& [Comp, Asset] : CoverTextureDataOld.ComponentArrays)
        if (!OmnigenAsset->CoverTextureData.ComponentArrays.Contains(Comp) || OmnigenAsset->CoverTextureData.ComponentArrays[Comp] != Asset.LoadSynchronous())
            AssetsToDelete.Add(Asset.Get());

    // Foliage data
    for (auto&& Asset : FoliageMeshesOld)
        if (!OmnigenAsset->FoliageMeshes.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

    for (auto&& Asset : FoliageMaterialInstancesOld)
        if (!OmnigenAsset->FoliageMaterialInstances.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

    for (auto&& Asset : FoliageTypesOld)
        if (!OmnigenAsset->FoliageTypes.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

    for (auto&& Asset : FoliageTexturesOld)
        if (!OmnigenAsset->FoliageTextures.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

    // Water data
    for (auto&& Asset : WaterMeshesOld)
        if (!OmnigenAsset->WaterMeshes.Contains(Asset.LoadSynchronous()))
            AssetsToDelete.Add(Asset.Get());

	ObjectTools::ForceDeleteObjects(AssetsToDelete, false);

	return EReimportResult::Succeeded;
}

void UOmnigenAssetFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UOmnigenAsset* OmnigenAsset = Cast<UOmnigenAsset>(Obj);
	OmnigenAsset->Path.Reset();

	if (!NewReimportPaths.IsEmpty())
		OmnigenAsset->Path = NewReimportPaths[0];
}

UWorld* UOmnigenAssetFactory::CreateWorldForAsset()
{
    // Init World
    GEditor->NewMap(true);

    // Init Level

    // New map screen returned a non-empty TemplateName, so the user has selected to begin from a template map
    bool TemplateFound = false;

    // Search all template map folders for a match with TemplateName
    const bool bIncludeReadOnlyRoots = true;
    const FString TemplateName = TEXT("/Engine/Maps/Templates/OpenWorld");
    if (FPackageName::IsValidLongPackageName(TemplateName, bIncludeReadOnlyRoots))
    {
        const FString MapPackageFilename = FPackageName::LongPackageNameToFilename(TemplateName, FPackageName::GetMapPackageExtension());
        if (FPaths::FileExists(MapPackageFilename))
        {
            // File found because the size check came back non-zero
            TemplateFound = true;

            // If there are any unsaved changes to the current level, see if the user wants to save those first.
            if (FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/true, /*bSaveMapPackages*/true, /*bSaveContentPackages*/false))
            {
                // Load the template map file - passes LoadAsTemplate==true making the
                // level load into an untitled package that won't save over the template
                FEditorFileUtils::LoadMap(*MapPackageFilename, /*bLoadAsTemplate=*/true);
            }
        }
    }

    if (!TemplateFound)
    {
        UE_LOG(LogTemp, Warning, TEXT("Couldn't find template map package %s"), *TemplateName);
        GEditor->CreateNewMapForEditing();
    }

    return GWorld;
}
