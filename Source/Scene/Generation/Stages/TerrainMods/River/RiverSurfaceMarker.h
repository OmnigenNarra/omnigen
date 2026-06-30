#include "RiverNurbsMarker.h"

class DRiverSurfaceMarker : public DNurbsMarker
{
public:
    DRiverSurfaceMarker() = default;
    DRiverSurfaceMarker(const SerializableNurbs<3, float>::Input& surface, int rows, int cols);

    IMPLEMENT_SHOULD_DRAW();

private:
    FRIEND_OMNIBIN(DRiverSurfaceMarker);
};

void omniSave(const DRiverSurfaceMarker& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DRiverSurfaceMarker& object, OmniBin<std::ios::in>& omniBin);