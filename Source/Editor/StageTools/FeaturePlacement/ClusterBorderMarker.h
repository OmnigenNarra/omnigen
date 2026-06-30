#pragma once
#include "Scene/Generation/Common/Markers/BatchingLineMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"

struct ClusterBorderPainter : LinePainter
{
    virtual bool shouldDraw() const override
    {
        auto stage = Generation::Data::get()->getGenerationStage();
        return stage == EGenerationStage::FeaturePlacement;
    };
};

struct ClusterBorderBatchParams : LineBatchParams
{
    using LineBatchParams::VertexType;
    using LineBatchParams::PainterType;
    using LineBatchParams::LineBatchParams;
};
using DClusterBorderMarker = DBatchingMarker<ClusterBorderBatchParams>;
inline auto& gClusterBorderMarker = gBatchingMarkerInstance<ClusterBorderBatchParams>;
inline auto& gClusterBorderMarkerGuard = gBatchingMarkerInstanceGuard<ClusterBorderBatchParams>;