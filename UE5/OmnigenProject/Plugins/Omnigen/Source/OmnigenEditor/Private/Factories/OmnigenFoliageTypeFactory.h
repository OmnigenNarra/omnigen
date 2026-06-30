#pragma once
#include "Factories/Factory.h"
#include "OmnigenFoliageTypeFactory.generated.h"

UCLASS()
class UFoliageType_OmnigenFactory : public UFactory
{
    GENERATED_UCLASS_BODY()

    // UFactory interface
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual FString GetDefaultNewAssetName() const override
    {
        return TEXT("NewInstancedStaticMeshFoliage");
    }
    // End of UFactory interface
};