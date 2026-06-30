#include "stdafx.h"
#include "DrawUtils.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"

namespace Design::DrawUtils
{
    std::optional<QVector3D> findPoint(QPoint mousePosition)
    {
        auto&& dem = Generation::Data::get()->getDEM();
        auto&& demTools = getStageTools<EGenerationStage::TerrainModel>();

        if (auto squareHit = demTools->findDemQuadUnderCursor())
            return dem->heightData.getPoint(squareHit->at(0).x, squareHit->at(0).z);

        return {};
    }

    void clearBrush(QSharedPointer<DLineMarker>& brushMarker)
    {
        if (brushMarker)
        {
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
            brushMarker = nullptr;
        }
    }

    void drawBrushCircle(QMouseEvent* mEvent, int circleDetail, float brushRadius, QSharedPointer<DLineMarker>& brushMarker)
    {
        if (auto point = findPoint(mEvent->pos()))
        {
            auto&& dem = Generation::Data::get()->getDEM();

            std::vector<QVector3D> circlePoints;

            for (int i = 0; i <= circleDetail; i++)
            {
                auto angle = ((std::numbers::pi * 2) / circleDetail) * i;

                GVector2D nP(point->x() + (brushRadius * std::cosf(angle)), point->z() + (brushRadius * std::sinf(angle)));

                QVector3D newPoint = { nP.x, dem->heightData.sample(nP) + 100, nP.z };
                circlePoints.emplace_back(newPoint);
            }

            if (!brushMarker)
                brushMarker = Generation::Data::get()->createMarker<DLineMarker, true>(circlePoints, QVector4D(0.2, 1, 0.2, 0.8), true);
            else
                brushMarker->movePoints(circlePoints);
        }
        else if (brushMarker)
        {
            clearBrush(brushMarker);
        }
    }

    std::vector<const tml::node<float, IndexType>*> getCellsUnderBrush(int centerCell, float brushRadius)
    {
        auto&& data = Generation::Data::get();
        auto&& cells = data->getTerrainCells()->getCells();

        GVector2D point = cells[centerCell]->getCenter();
        auto&& blockTree = data->getBlockQuadTree();
        return blockTree->find_all_nearest(point.x, point.z, brushRadius);
    }
}