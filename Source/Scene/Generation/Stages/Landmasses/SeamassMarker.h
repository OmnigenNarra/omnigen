#pragma once
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Scene/Generation/Stages/Landmasses/LandmassMarker.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"

class DShorelineMarker;
class DLandmassMarker;
class DSeamassMarker;

class DSeamassMarker : public DPolygonWithHolesMarker
{
public:
    using DPolygonWithHolesMarker::DPolygonWithHolesMarker;

    static void generateSeamassMarkers(std::vector<QSharedPointer<DLandmassMarker>> landmasses);

    IMPLEMENT_SHOULD_DRAW();

protected:

    static std::tuple<std::vector<QVector3D>, std::vector<QSharedPointer<DShorelineMarker>>> createSeamassPolygon(const std::vector<QSharedPointer<DShorelineMarker>>& shorelines);

    FRIEND_OMNIBIN(DSeamassMarker);
};

void omniSave(const DSeamassMarker& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DSeamassMarker& object, OmniBin<std::ios::in>& omniBin);