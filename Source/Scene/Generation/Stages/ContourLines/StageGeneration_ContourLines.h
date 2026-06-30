#pragma once
#include "../StageGenerationBase.h"
#include "IsohypseData.h"
#include "Utils/CircularVectorView.h"

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::ContourLines>
    {
    public:
        static void initialize();
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize() {};
        static void createIsohypsesOutOfDEM(const std::vector<float>& bufferDEM, int demMargin, GPoint offset, int rows, int cols, float xSpacing, float zSpacing, bool satScan = false);

    private:
        // GDAL isohypse out of DEM creation
        static void sortIsohypses(const std::unordered_multimap<float, std::vector<QVector3D>>& allIsohypses, float heightScale);
        static float descendantAngleDeviationFromNominal(const CircularVectorView<std::vector, QVector3D>& sourcePoints, const QVector3D& descendantPoint, int idx);
    };
}