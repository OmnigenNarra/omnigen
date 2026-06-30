#pragma once
#include "Scene/Generation/Common/Markers/BatchingCellMarker.h"

struct LithomapBatchParams : CellBatchParams {};
struct LithomapPainter : CellPainter {};
using DLithomapMarker = DBatchingMarker<LithomapBatchParams>;
inline auto& gLithomapMarker = gBatchingMarkerInstance<LithomapBatchParams>;

template<>
struct BatchedSection<LithomapBatchParams> : BatchedSection<CellBatchParams>
{
    using BatchedSection<CellBatchParams>::BatchedSection;
};