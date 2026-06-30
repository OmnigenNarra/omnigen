// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EditorReimportHandler.h"
#include "Factories/Factory.h"

#include "OmnigenAssetFactory.generated.h"

/**
 * 
 */
UCLASS()
class UOmnigenAssetFactory : public UFactory, public FReimportHandler
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;

	//~ End UFactory Interface

	//~ Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;

	virtual EReimportResult::Type Reimport(UObject* Obj) override;

	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	//~ End FReimportHandler Interface

private:
	UWorld* CreateWorldForAsset();
};
