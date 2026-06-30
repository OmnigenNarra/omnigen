#pragma once
#include "Utils/GeometryData.h"
#include "Scene/OmnigenDrawable.h"
#include "../StageGenerationBase.h"
#include "../FeatureGeneration/ClusterMeshMarker.h"

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::TerrainFinalization>
    {
    public:
        static void initialize();
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize() {};

    private:
        // Rearrange block meshes into renderable chunks
        static bool bake();

        static bool divideWorldMeshIntoChunks();
    };
}