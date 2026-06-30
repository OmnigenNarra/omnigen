#include "stdafx.h"
#include "StageGeneration_TerrainModel.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "DigitalElevationModel.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/Stages/ContourLines/StageGeneration_ContourLines.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdal_alg.h>

namespace Generation
{
    void StageGen<EGenerationStage::TerrainModel>::initialize()
    {
        if (!Generation::Data::get()->getDEM())
            DEM::initialize();
    }

    // Generate a Digital Elevation Model based on Contour Lines.
    // This step is pure data transformation, no new data is generated here.
    bool StageGen<EGenerationStage::TerrainModel>::autoGen()
    {
        Data::get()->getDEM()->loadFromIHs();
        return true;
    }

    void StageGen<EGenerationStage::TerrainModel>::clear()
    {
        DEM::clear();
        Generation::Data::get()->clearExactMarkers<DDemMarker>();
    }

    void StageGen<EGenerationStage::TerrainModel>::finalize()
    {
        auto&& heightData = Generation::Data::get()->getDEM()->heightData;
        if (heightData.wasEdited())
        {
            auto&& contourLinesStageTools = getStageTools<EGenerationStage::ContourLines>();
            contourLinesStageTools->connectNodes();

            clearAllBatches<IsohypseBatchParams>();
            float margin = 3;
            auto gdalBuffer = heightData.createGDALbuffer(margin);

            auto gridSpacing = heightData.getGridSpacing();
            auto offset = heightData.getOffset();

            int rows = heightData.getSize().z + (margin * 2) + 1;
            int cols = heightData.getSize().x + (margin * 2) + 1;

            StageGen<EGenerationStage::ContourLines>::createIsohypsesOutOfDEM(gdalBuffer, margin, offset, rows, cols, gridSpacing, gridSpacing);
            heightData.setEditedStatus(false);

            contourLinesStageTools->aboutToExitStage(1);
        }
    }
}