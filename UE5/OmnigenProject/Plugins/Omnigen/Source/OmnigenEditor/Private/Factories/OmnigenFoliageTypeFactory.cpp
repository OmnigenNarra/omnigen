#include "OmnigenFoliageTypeFactory.h"
#include "Foliage/Public/FoliageType_InstancedStaticMesh.h"

UFoliageType_OmnigenFactory::UFoliageType_OmnigenFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UFoliageType_InstancedStaticMesh::StaticClass();
}

UObject* UFoliageType_OmnigenFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<UFoliageType_InstancedStaticMesh>(InParent, Class, Name, Flags);
}