#include "stdafx.h"
#include "RiverSurfaceMarker.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/OmnigenGenerationData.h"

DRiverSurfaceMarker::DRiverSurfaceMarker(const SerializableNurbs<3, float>::Input& surface, int rows, int cols)
    : DNurbsMarker(surface, rows, cols, QVector4D(0, 0.5, 1, 0.35), 0.0f, ERenderType::Filled)
{
}

void omniSave(const DRiverSurfaceMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DNurbsMarker&>(object);
}

void omniLoad(DRiverSurfaceMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DNurbsMarker&>(object);
}
