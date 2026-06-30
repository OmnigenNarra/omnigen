#pragma once
#include "Scene/Generation/Common/Markers/BatchingCellMarker.h"

struct BlockTypePainter : CellPainter
{
    virtual bool shouldDraw() const override 
    { 
        auto stage = Generation::Data::get()->getGenerationStage();
        return (stage == EGenerationStage::TerrainClassification) || (stage == EGenerationStage::FeaturePlacement);
    };
};

struct BlockTypeBatchParams : CellBatchParams
{
    using VertexType = CellVertex;
    using PainterType = BlockTypePainter;
};
using DBlockTypeMarker = DBatchingMarker<BlockTypeBatchParams>;
inline auto& gBlockTypeMarker = gBatchingMarkerInstance<BlockTypeBatchParams>;
inline auto& gBlockTypeMarkerGuard = gBatchingMarkerInstanceGuard<BlockTypeBatchParams>;

template<>
struct BatchedSection<BlockTypeBatchParams> : BatchedSection<CellBatchParams> 
{
    using BatchedSection<CellBatchParams>::BatchedSection;
};