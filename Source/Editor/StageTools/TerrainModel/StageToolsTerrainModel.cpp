#include "stdafx.h"
#include "StageToolsTerrainModel.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Omnigen.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Source/Scene/Generation/Common/Markers/LineMarker.h"
#include "Utils/PlatformMisc.h"
#include "../SelectionMgrBase.h"
#include "../StageTools.h"

#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

namespace Design
{
    Generation::DEM* StageTools<EGenerationStage::TerrainModel>::getDem() const
    {
        return Generation::Data::get()->getDEM().get();
    }

    StageTools<EGenerationStage::TerrainModel>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::TerrainModel>::getSelectionMgr() const
    {
        return StageToolsBase::getSelectionMgr();
    }

    void StageTools<EGenerationStage::TerrainModel>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* outline = omnigen->getOutline();

        // Temporary dummy widget - will most likely be replaced with tool settings bar
        // For some reason the toolbar will not be shown without a second widget passed in the fillSection()
        auto* dummyWidget = new OutlineTreeView();
        auto* toolbar = createOutlineToolbar();

        outline->fillSection({ toolbar, dummyWidget });

        toolbar->show();

        toolMode = EToolMode::None;

        // Ticker for constant point edit while holding LMB
        connect(&ticker, &QTimer::timeout, this, &StageTools<EGenerationStage::TerrainModel>::editPoints);
        ticker.setInterval(16);

        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void StageTools<EGenerationStage::TerrainModel>::unbind()
    {
        StageToolsBase::unbind();

        if (brushMarker)
            clearBrush();

        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }
    }

    void StageTools<EGenerationStage::TerrainModel>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();
        writer << genData->getDEM();

        writer << demPointNodes;
    }

    void StageTools<EGenerationStage::TerrainModel>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();

        QSharedPointer<Generation::DEM> dem;
        reader >> dem;

        genData->setDEM(dem);
        genData->initializeQueuedMarkers();

        reader >> demPointNodes;
    }

    void StageTools<EGenerationStage::TerrainModel>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::TerrainModel>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::TerrainModel>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::TerrainModel>::modifyNode);
    }

    void StageTools<EGenerationStage::TerrainModel>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::TerrainModel>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        updateParentNodes();
        disconnectNodes();
    }

    void StageTools<EGenerationStage::TerrainModel>::clearNodes()
    {
        demPointNodes.clear();
    }

    void StageTools<EGenerationStage::TerrainModel>::cleanNodesState()
    {
        std::erase_if(demPointNodes, [](auto& kv) { return !kv.second->getPoint(); });

        for (auto&& [index, demPointNode] : demPointNodes)
        {
            demPointNode->setCreatedOnCurrentStage(false);
            demPointNode->clearSnapshot();
        }
    }

    void StageTools<EGenerationStage::TerrainModel>::updateParentNodes()
    {
    }

    void StageTools<EGenerationStage::TerrainModel>::loadSnapshotData()
    {
        disconnectNodes();
        auto&& dem = Generation::Data::get()->getDEM();
        
        // DEM can only be modified during stage, not changed or deleted
        for (auto&& [point, demPointNode] : demPointNodes)
            if (auto&& snapshotDemPoint = demPointNode->getSnapshot())
            {
                auto index = dem->heightData.idx(point);
                dem->heightData.getGeometryRW()->vertices[index] = snapshotDemPoint->heightData;
                dem->levelData.getGeometryRW()->vertices[index] = snapshotDemPoint->levelData;
                dem->verticalDisplacementXCoords.getGeometryRW()->vertices[index] = snapshotDemPoint->verticalDisplacementXCoords;
                demPointNode->clearSnapshot();
            }

        dem->heightData.update();
        dem->levelData.update();
        dem->verticalDisplacementXCoords.update();

        connectNodes();
    }

    bool StageTools<EGenerationStage::TerrainModel>::validatePipeline()
    {
        for (auto&& [index, demPointNode] : demPointNodes)
        {
            // removed landmass
            if (!demPointNode->getPoint())
                return false;
            // added landmass
            else if (demPointNode->isCreatedOnCurrentStage())
                return false;
            // modified landmass
            else if (demPointNode->getSnapshot())
                return false;
        }

        return true;
    }

    void StageTools<EGenerationStage::TerrainModel>::updatePipeline()
    {
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        
        auto&& diagram = Generation::Data::get()->getTerrainCells();

        std::vector<GVector2D> cellCentersToInvalidate;

        for (auto&& [index, demPointNode] : demPointNodes)
        {
            if (demPointNode->getSnapshot() || demPointNode->isCreatedOnCurrentStage() && demPointNode->getCellNode())
            {
                cellCentersToInvalidate << *demPointNode->getCellNode();
            }
        }

        auto&& cellUpdate = QSharedPointer<Voronoi::CellUpdate>::create(cellCentersToInvalidate);
        emit Editable::aboutToBeModified(cellUpdate);

        if (!Generation::Data::get()->getBlockTypeMap().empty())
            for (auto&& cellCenter : cellCentersToInvalidate)
                Generation::Data::get()->setBlockTypeForCell(diagram->getCellIndexFromCenter(cellCenter), Generation::ETerrainBlock::Last);

        emit Editable::modified(cellUpdate);

        lithomapStageTools->updatePipeline();
        cleanNodesState();
        updateParentNodes();
    }

    void StageTools<EGenerationStage::TerrainModel>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        if (auto&& demUpdateInfo = object.dynamicCast<Generation::DEMUpdateInfo>())
        {
            for (auto&& demPoint : demUpdateInfo->points)
            {
                if (!demPointNodes.contains(demPoint))
                    demPointNodes[demPoint] = QSharedPointer<DEMPointNode>::create(demPoint);
                else if (!demPointNodes[demPoint]->getPoint())
                    demPointNodes[demPoint]->setPoint(demPoint);
            }
        }
    }

    void StageTools<EGenerationStage::TerrainModel>::removeNode(QSharedPointer<Editable> object)
    {
        if (auto&& demUpdateInfo = object.dynamicCast<Generation::DEMUpdateInfo>())
        {
            for (auto&& demPoint : demUpdateInfo->points)
                if (demPointNodes.contains(demPoint))
                {
                    if (demPointNodes[demPoint]->isCreatedOnCurrentStage())
                        demPointNodes.erase(demPoint);
                    else
                    {
                        if (!demPointNodes[demPoint]->getSnapshot())
                            demPointNodes[demPoint]->makeSnapshot();

                        demPointNodes[demPoint]->nullifyPoint();
                    }
                }
        }
    }

    void StageTools<EGenerationStage::TerrainModel>::modifyNode(QSharedPointer<Editable> object)
    {
        if (auto&& demUpdateInfo = object.dynamicCast<Generation::DEMUpdateInfo>())
        {
            // use parallel only here as it does not modify map + is used while editing
            tbb::parallel_for(0, int(demUpdateInfo->points.size()), [&](int i)
                {
                    GVector2D demPoint = demUpdateInfo->points[i];
                    if (demPointNodes.contains(demPoint) && !demPointNodes[demPoint]->isCreatedOnCurrentStage() && !demPointNodes[demPoint]->getSnapshot())
                        demPointNodes[demPoint]->makeSnapshot();
                });
        }
    }

    bool StageTools<EGenerationStage::TerrainModel>::eventFilter(QObject* obj, QEvent* event)
    {
        if (toolMode == EToolMode::None)
            return false;

        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            // Constantly edit DEM while holding LMB
            if (mEvent->button() == Qt::LeftButton)
            {
                pointModification.clear();
                ticker.start();
            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            updateBrush();
        }
        else if (ticker.isActive() && mEvent->type() == QEvent::MouseButtonRelease)
        {
            ticker.stop();
            registerDemChange(pointModification);
        }

        return false;
    }

    std::optional<std::array<GPoint, 4>> StageTools<EGenerationStage::TerrainModel>::findDemQuadUnderCursor()
    {
        auto demData = SelectionMgrBase::findObjectUnderCursor<DDemMarker>();
        if (demData)
        {
            auto&& indices = demData->object->getActiveBaseGeometry()->indices;
            IndexType i0 = indices[demData->primitive * 4 + 0];
            IndexType i1 = indices[demData->primitive * 4 + 1];
            IndexType i2 = indices[demData->primitive * 4 + 2];
            IndexType i3 = indices[demData->primitive * 4 + 3];

            auto* dem = getDem();
            auto p0 = dem->heightData.fromIdx(i0);
            auto p1 = dem->heightData.fromIdx(i1);
            auto p2 = dem->heightData.fromIdx(i2);
            auto p3 = dem->heightData.fromIdx(i3);

            return std::array{ p0, p1, p2, p3 };
        }

        return {};
    }

    void StageTools<EGenerationStage::TerrainModel>::editPoints()
    {
        if (!brushOrigin)
            return;

        auto* dem = getDem();
        if(!dem->heightData.wasEdited())
            dem->heightData.setEditedStatus(true);

        switch (toolMode)
        {
        case EToolMode::Sculpt: return sculpt();
        case EToolMode::Smooth: return smooth();
        case EToolMode::Flatten: return flatten();
        }
    }

    void StageTools<EGenerationStage::TerrainModel>::updatePointsHeight(Generation::DEM* dem, const std::unordered_map<IndexType, float>& pointsToUpdate)
    {
        for (auto&& [i, deltaH] : pointsToUpdate)
            pointModification[i] += deltaH;

        dem->addHeightToPoints(pointsToUpdate);
    }

    void StageTools<EGenerationStage::TerrainModel>::updatePointsNormal(Generation::DEM* dem, const std::unordered_map<IndexType, QVector3D>& pointsToUpdate)
    {
        for (auto&& [i, deltaH] : pointsToUpdate)
            pointModification[i];

        dem->updatePointsNormal(pointsToUpdate);
    }

    void StageTools<EGenerationStage::TerrainModel>::computePointsNormal(Generation::DEM* dem, std::unordered_map<IndexType, QVector3D>* pointsToUpdate, int px, int pz)
    {
        (*pointsToUpdate)[dem->heightData.idx(px, pz)] = dem->heightData.computeNormal(px, pz);

        if (px > 0)
            (*pointsToUpdate)[dem->heightData.idx(px - 1, pz)] = dem->heightData.computeNormal(px - 1, pz);

        if (pz > 0)
            (*pointsToUpdate)[dem->heightData.idx(px, pz - 1)] = dem->heightData.computeNormal(px, pz - 1);

        if (px < dem->heightData.getSize().x - 1)
            (*pointsToUpdate)[dem->heightData.idx(px + 1, pz)] = dem->heightData.computeNormal(px + 1, pz);

        if (pz < dem->heightData.getSize().z - 1)
            (*pointsToUpdate)[dem->heightData.idx(px, pz + 1)] = dem->heightData.computeNormal(px, pz + 1);
    }

    void StageTools<EGenerationStage::TerrainModel>::sculpt()
    {
        auto* dem = getDem();
        float movementDirection = brushStrength * (isKeyDown(VK_SHIFT) ? -30.0f : 30.0f);

        auto pointsToEdit = findPointsUnderBrush();
        auto&& originPoint = dem->heightData.getPoint(*brushOrigin);

        std::unordered_map<IndexType, float> pointsHeightToUpdate;
        std::unordered_map<IndexType, QVector3D> pointsNormalToUpdate;

        for (auto&& point : pointsToEdit)
        {
            auto&& pt = dem->heightData.getPoint(point.x, point.z);
            float movement = movementDirection * (1 - (GVector2D(pt.x(), pt.z()).dist(GVector2D(originPoint.x(), originPoint.z())) / (brushSize * 1000)));

            pointsHeightToUpdate[dem->heightData.idx(point.x, point.z)] = movement;
        }

        for (auto&& point : pointsToEdit)
            computePointsNormal(dem, &pointsNormalToUpdate, point.x, point.z);

        updatePointsHeight(dem, pointsHeightToUpdate);
        updatePointsNormal(dem, pointsNormalToUpdate);
        dem->heightData.update(pointsToEdit);
    }

    void StageTools<EGenerationStage::TerrainModel>::smooth()
    {
        auto* dem = getDem();
        auto pointsToEdit = findPointsUnderBrush();
        auto&& originPoint = dem->heightData.getPoint(*brushOrigin);

        // Calculate average height of the outer circle
        float outerCircleSum = 0;
        int outerCirclePoints = 0;

        for (auto&& point : pointsToEdit)
        {
            auto&& pt = dem->heightData.getPoint(point.x, point.z);
            if (GVector2D(pt.x(), pt.z()).dist(GVector2D(originPoint.x(), originPoint.z())) > (brushSize * 1000) * 0.7)
            {
                outerCirclePoints++;
                outerCircleSum += pt.y();
            }
        }

        float outerCircleAvgHeight = outerCircleSum / outerCirclePoints;

        std::unordered_map<IndexType, float> pointsHeightToUpdate;
        std::unordered_map<IndexType, QVector3D> pointsNormalToUpdate;

        // Edit points to slowly reach the average height of the outer brush circle
        for (auto&& point : pointsToEdit)
        {
            auto&& pt = dem->heightData.getPoint(point.x, point.z);
            float movement = (outerCircleAvgHeight - pt.y()) * 0.1 * (brushStrength / 5.0f) * (1 - (GVector2D(pt.x(), pt.z()).dist(GVector2D(originPoint.x(), originPoint.z())) / (brushSize * 1000)));

            pointsHeightToUpdate[dem->heightData.idx(point.x, point.z)] = movement;
        }

        for (auto&& point : pointsToEdit)
            computePointsNormal(dem, &pointsNormalToUpdate, point.x, point.z);

        updatePointsHeight(dem, pointsHeightToUpdate);
        updatePointsNormal(dem, pointsNormalToUpdate);
        dem->heightData.update(pointsToEdit);
    }

    void StageTools<EGenerationStage::TerrainModel>::flatten()
    {
        auto* dem = getDem();
        auto pointsToEdit = findPointsUnderBrush();
        
        auto&& originPoint = dem->heightData.getPoint(*brushOrigin);

        std::unordered_map<IndexType, float> pointsHeightToUpdate;
        std::unordered_map<IndexType, QVector3D> pointsNormalToUpdate;

        // Edit points to reach the brush central point height
        for (auto&& point : pointsToEdit)
        {
            auto&& pt = dem->heightData.getPoint(point);
            float movement = (originPoint.y() - pt.y()) * 0.5 * (brushStrength / 5.0f) * (1 - (GVector2D(pt.x(), pt.z()).dist(GVector2D(originPoint.x(), originPoint.z())) / (brushSize * 1000)));

            pointsHeightToUpdate[dem->heightData.idx(point.x, point.z)] = movement;
        }

        for (auto&& point : pointsToEdit)
            computePointsNormal(dem, &pointsNormalToUpdate, point.x, point.z);

        updatePointsHeight(dem, pointsHeightToUpdate);
        updatePointsNormal(dem, pointsNormalToUpdate);
        dem->heightData.update(pointsToEdit);
    }

    std::vector<GPoint> StageTools<EGenerationStage::TerrainModel>::findPointsUnderBrush()
    {
        auto* dem = getDem();
        int brushRadius = brushSize * 1000;
        
        auto&& spacing = dem->heightData.getGridSpacing();
        auto&& demSize = dem->heightData.getSize();
        auto originPoint = dem->heightData.getPoint(*brushOrigin);

        int radius = std::round(brushRadius / spacing);

        std::vector<GPoint> pointsUnder;

        // Out of a square area with sides equal 2 * radius, find all points that are close enough to the circle origin
        GVector2D vBrushOrigin(originPoint);
        for (int i = brushOrigin->x - radius; i <= brushOrigin->x + radius; i++)
            for (int j = brushOrigin->z - radius; j <= brushOrigin->z + radius; j++)
            {
                if (i < 0 || j < 0 || i > demSize.x || j > demSize.z)
                    continue;

                auto&& pt = dem->heightData.getPoint(i, j);

                if (distance(GVector2D(pt), vBrushOrigin) < brushRadius)
                    pointsUnder.emplace_back(i, j);
            }

        return pointsUnder;
    }

    void StageTools<EGenerationStage::TerrainModel>::updateBrush()
    {
        auto quad = findDemQuadUnderCursor();
        if (!quad)
        {
            brushOrigin = {};
            clearBrush();
            return;
        }

        // All operations rely on brushOrigin to be set here
        brushOrigin = quad->at(0);

        auto* dem = getDem();
        auto&& point = dem->heightData.getPoint(*brushOrigin);
        int brushS = brushSize * 1000;
        std::vector<QVector3D> circlePoints;

        const int circleDetail = 24;
        for (int i = 0; i <= circleDetail; i++)
        {
            auto angle = ((std::numbers::pi * 2) / circleDetail) * i;

            GVector2D nP(point.x() + (brushS * std::cosf(angle)), point.z() + (brushS * std::sinf(angle)));

            QVector3D newPoint = { nP.x, dem->heightData.sample(nP) + 20, nP.z };
            circlePoints.emplace_back(newPoint);
        }

        if (!brushMarker)
            brushMarker = Generation::Data::get()->createMarker<DLineMarker, true>(circlePoints, QVector4D(0.2, 1, 0.2, 0.8), true);
        else
            brushMarker->movePoints(circlePoints);
    }

    void StageTools<EGenerationStage::TerrainModel>::clearBrush()
    {
        if (brushMarker)
        {
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushMarker->getGuid());
            brushMarker = nullptr;
        }
    }

    bool StageTools<EGenerationStage::TerrainModel>::registerDemChange(const std::unordered_map<IndexType, float>& changeMap)
    {
        History::GetContext()->Push(std::string(magic_enum::enum_name(toolMode)), hBind(this, &StageTools<EGenerationStage::TerrainModel>::registerDemChange), hBind(this, &StageTools<EGenerationStage::TerrainModel>::registerDemChange_Undo), changeMap);
        HistoryPushController historyController;

        // This status update is for a case where the stage is advanced (thus recalculating Ihs and setting the status to false), then reverted and redo called
        auto* dem = getDem();
        dem->heightData.setEditedStatus(true);

        std::unordered_map<IndexType, float> pointsHeightToUpdate;
        std::unordered_map<IndexType, QVector3D> pointsNormalToUpdate;

        if (History::GetContext()->IsRedoing())
        {
            for (auto&& [index, delta] : changeMap)
            {
                pointsHeightToUpdate[index] = delta;
            }

            for (auto&& [index, delta] : changeMap)
            {
                GPoint p = dem->heightData.fromIdx(index);
                pointsNormalToUpdate[index] = dem->heightData.computeNormal(p.x, p.z);
            }
        }
        
        dem->addHeightToPoints(pointsHeightToUpdate);
        dem->updatePointsNormal(pointsNormalToUpdate);
        dem->heightData.update();
        return true;
    }

    bool StageTools<EGenerationStage::TerrainModel>::registerDemChange_Undo(const std::unordered_map<IndexType, float>& changeMap)
    {
        // This status update is for a case where the stage is advanced (thus recalculating Ihs and setting the status to false), then reverted and undo called
        auto* dem = getDem();
        dem->heightData.setEditedStatus(true);

        std::unordered_map<IndexType, float> pointsHeightToUpdate;
        std::unordered_map<IndexType, QVector3D> pointsNormalToUpdate;

        for (auto&& [index, delta] : changeMap)
            pointsHeightToUpdate[index] = -delta;

        for (auto&& [index, delta] : changeMap)
        {
            GPoint p = dem->heightData.fromIdx(index);
            pointsNormalToUpdate[index] = dem->heightData.computeNormal(p.x, p.z);
        }

        dem->addHeightToPoints(pointsHeightToUpdate);
        dem->updatePointsNormal(pointsNormalToUpdate);
        dem->heightData.update();
        return true;
    }
}