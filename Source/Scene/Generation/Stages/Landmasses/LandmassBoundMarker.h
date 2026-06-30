#pragma once
#include "../ContourLines/ContourLines.h"

class DShorelineMarker;

// Holds terrain world bounds
class DLandmassBound : public DIsohypseBound
{
public:
    DLandmassBound(const std::vector<QVector3D>& inControlPoints);

    virtual bool shouldDraw(int vIdx) const override { return false; }

    static bool generateAll(bool optimize = true);
    static QSet<GPoint> getContinentInside(const GPoint& pureTerrainSquare);

private:
    DLandmassBound() = default;
    FRIEND_OMNIBIN(DLandmassBound)
};

inline void omniSave(const DLandmassBound& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DLineMarker&>(object);
}

inline void omniLoad(DLandmassBound& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DLineMarker&>(object);
}