#include "stdafx.h"
#include "TerrainFinalizationSelection.h"

#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"

namespace Design
{
    Selection<EPIESelection, EPIESelection::SpawnSelector>::Selection(const std::any& inHandle)
        : spawnLocation(std::any_cast<DataType>(inHandle))
    {
        Selection<EPIESelection, EPIESelection::SpawnSelector>::select();
    }

    bool Selection<EPIESelection, EPIESelection::SpawnSelector>::findOnScene(QMap<EPIESelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto&& omnigen = Omnigen::get();
        if (!Generation::Data::get() || omnigen->isGenerating())
            return false;

        auto&& finTools = getStageTools<EGenerationStage::TerrainFinalization>();
        auto chunkTriangle = finTools->findChunkTriangleUnderCursor();
        if (!chunkTriangle)
            return false;

        if (GetKeyState(VK_RBUTTON) < 0)
            return false;

        auto&& geometry = chunkTriangle->chunk->getActiveGeometry<TerrainMeshVertex>();
        auto&& vertices = geometry->vertices;
        auto&& triangles = geometry->indices;
        auto&& p0 = vertices[triangles[chunkTriangle->triangleIdx * 3 + 0]];
        auto&& p1 = vertices[triangles[chunkTriangle->triangleIdx * 3 + 1]];
        auto&& p2 = vertices[triangles[chunkTriangle->triangleIdx * 3 + 2]];
        auto triangleCenter = (p0.position + p1.position + p2.position) / 3.0;

        (*output)[EPIESelection::SpawnSelector] = triangleCenter;
        return true;
    }

    QMenu* Selection<EPIESelection, EPIESelection::SpawnSelector>::requestContextMenu(const std::any& data)
    {
        QMenu* menu = new QMenu(Omnigen::get());
        return menu;
    }

    void Selection<EPIESelection, EPIESelection::SpawnSelector>::getData(const SelectionBase* obj, QSet<DataType>* data)
    {
        (*data) += static_cast<const PIESelection*>(obj)->spawnLocation;
    }

    std::vector<QSharedPointer<SelectionBase>> Selection<EPIESelection, EPIESelection::SpawnSelector>::createFromData(
        const QSet<QVector3D>& inHandles)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& pt : inHandles)
        {
            auto sel = QSharedPointer<PIESelection>::create();
            sel->spawnLocation = pt;
            sel->select();
            results << sel;
        }

        return results;
    }

    void Selection<EPIESelection, EPIESelection::SpawnSelector>::update(const std::any& newSquare,
        std::vector<QSharedPointer<SelectionBase>>* currentSelections,
        const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
    }

    void Selection<EPIESelection, EPIESelection::SpawnSelector>::save(
        std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        currentSelections->emplace_back(sharedFromThis());
    }

    void Selection<EPIESelection, EPIESelection::SpawnSelector>::select() const
    {
        debugMarker = Generation::Data::get()->createMarker<DLineMarker, true>(spawnLocation, 10'000, QVector4D(0, 0, 1, 1));
    }

    void Selection<EPIESelection, EPIESelection::SpawnSelector>::deselect() const
    {
        Generation::Data::get()->clearSingleExactMarker<DLineMarker>(debugMarker->getGuid());
    }

    QVector3D Selection<EPIESelection, EPIESelection::SpawnSelector>::getPosition() const
    {
        return spawnLocation;
    }

    QSharedPointer<OmnigenPropertyListBase> Selection<EPIESelection, EPIESelection::SpawnSelector>::makePropertyList()
    {
        return SelectionBase::makePropertyList();
    }

}
