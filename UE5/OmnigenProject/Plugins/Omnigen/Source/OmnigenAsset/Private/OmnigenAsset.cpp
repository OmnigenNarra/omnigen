#include "../Public/OmnigenAsset.h"

void UOmnigenAsset::ClearForReimport()
{
    // Destroy all previous actors
    for (auto&& Actor : Actors)
        Actor.LoadSynchronous()->K2_DestroyActor();

    // Clear all references
    Actors.Empty();

    TerrainMeshes.Empty();
    TerrainMaterialInstances.Empty();

    RockTextureData = {};
    CoverTextureData = {};

    FoliageMeshes.Empty();
    FoliageMaterialInstances.Empty();
    FoliageTypes.Empty();
    FoliageTextures.Empty();

    WaterMeshes.Empty();

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}
