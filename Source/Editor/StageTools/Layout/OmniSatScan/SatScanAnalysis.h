#pragma once
#include "Scene/Generation/Stages/Ridges/MountainData.h"
#include "Editor/StageTools/Layout/OmniSatScan/SatScanLandforms.h"
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"

class MountainData;

enum class EElevation
{
    Lower,
    Equal,
    Higher
};

enum class EGeoForm
{
    Other = 0,
    Ridge = 100,
    Peak = 200
};

class SatScanAnalysis
{
public:
    SatScanAnalysis(bool copyFromWorld, float demHeightFactor, const QSet<GPoint>& availableSquares);

private:
    // Returns points around the midPt, with [0] being bottommost, rest going anti-clockwise. Might return null if points are out of raster array 
    // Default values of cellDistance and circleDetail are advised for point search for ridgeline detection (yield same results as Grass Gis with radius 3)
    std::optional<std::vector<int>> findPoints(int midPtIdx, int nCols, int nRows, int cellDistance = 4, int circleDetail = 8);
    // Similar to findPoints, but has a range limitation, and neighboring points merge for further but more specific point search with average direction vector
    // Due to the nature of the ridge point raster, a single ridge line can consist of multiple points, and as such merging them to get the proper direction is required
    std::optional<std::vector<int>> directionalFindPoints(int midPtIdx, int cellDistance, int circleDetail, float allowedAngle, const GVector2D& searchVector, const std::vector<float>& ridgeRaster);

    EGeoForm geoFormAssignment(int midPt, const std::vector<int>& ridgeRaster, const std::vector<float>& demRaster);
    void ridgelineSearch(int rootPeakIdx, int searchDistance, int circleDetail, float allowedAngle, const std::vector<float>& ridgeRaster, const std::vector<float>& demRaster);
    GVector2D rasterIdxToPos(int pixelIdx) { return GVector2D(pixelIdx % nCols, pixelIdx / nCols); };
    int posToRasterIdx(const GVector2D& pos) { return (pos.z * nCols) + pos.x; };

    // Main search function, with angle restriction
    std::optional<std::vector<int>> searchForRidgePoints(int parentIdx, int searchDistance, int circleDetail, float allowedAngle, const std::vector<float>& ridgeRaster, const std::vector<float>& demRaster);
    // Auxiliary search function, to be used after main search
    std::optional<std::vector<int>> searchForSubridges(int searchDistance, int circleDetail, const std::vector<float>& ridgeRaster, const std::vector<float>& demRaster);

    // Checks against points saved in ridgePtMap if distance to pos is withing allowed distance 
    // parentIdx == -1 is for peak point checks, otherwise it is required to skip parent point in checks while adding new points
    bool checkIfUnoccupied(GVector2D pos, int allowedDistance, int parentIdx = -1);

    // Returns the index of peak from sortedPeakMap if point (idx) is closer than allowedDistance (dist > 6 might result in wrong results due to peak sorting)
    std::optional<int> peakCheck(int idx, int allowedDistance);

    // Ridgeline analysis
    void analyzeRidgelines();
    std::vector<QSet<GPoint>> getRidgelineArea(const QSet<GPoint>& availableSquares);
    std::vector<int> getRasterAreaForRidgeline(const QSet<GPoint>& ridgelineArea);
    void computeScaleAndOffset(const QSet<GPoint>& availableSquares);

    void analyzeMainAndInnerValleyProfiles(MountainData* mountain);
    // To avoid unnecessary repeating, all outer valley profiles are computed at once
    void analyzeOuterValleyAndSlopeProfile();

    std::pair<ESlopeCurve, ESlopeCurve> analyzeSlopeProfilePair(const std::vector<QVector3D>& shape);
    ESlopeCurve analyzeSlopeProfile(const std::vector<QVector3D>& shape);

    std::optional<std::vector<QVector3D>> getDEMShapeBetweenPoints(const GVector2D& firstPoint, const GVector2D& secondPoint);
    EValleyProfile getValleyProfile(const std::vector<QVector3D>& shapeBetweenPoints);
    EValleyProfile getMainRidgeProfile(const MountainData& mountain);
    bool checkIntersection(const Segment2D& potentialProfile, const std::vector<MountainData>& mountainsToCheckAgainst);

    void setDomainParameters(const RidgelineDomain& domainRequestData);

    std::vector<int> vSortedPeaks;
    std::vector<MountainData> vMountains;
    int nCols = 0;
    int nRows = 0;

    std::vector<float> gdalDEM; // GDAL DEM - elevation is in range 0 - 255
    float fHeightMultiplier = 0.0f; // Based on the range set in makePostRequest() - DEM height * fHeightMultiplier = real world meters
    float fHeightAuxiliaryMultiplier = 30.0f; // real world meters * fHeightAuxiliaryMultiplier = height in Omnigen viewport

    // DEM - grid scale
    float fOffsetX, fOffsetZ, fScaleX, fScaleZ;
    // DEM -- geo transform
    double aTargetDEMGeoTransform[6];

    bool bCopyFromWorld = false;
    bool bNoRidgeline = false;

    friend class OmniSatScan;
};