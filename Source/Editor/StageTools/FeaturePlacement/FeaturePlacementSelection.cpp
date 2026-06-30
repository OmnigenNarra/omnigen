#include "stdafx.h"
#include "FeaturePlacementSelection.h"
#include "../StageTools.h"
#include "Editor/StageTools/Common/DrawUtils.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "../TerrainClassification/BlockTypeMarker.h"

namespace Design
{
    bool CellSelection::findOnScene(QMap<EFeaturePlacementSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto&& tools = getStageTools<EGenerationStage::FeaturePlacement>();
        if (tools->selectedTool != EFeaturePlacementToolType::Select)
            return false;

        auto cellSection = SelectionMgrBase::findObjectUnderCursor<DBlockTypeMarker>(screenData);
        if (!cellSection)
            return false;

        int cell = gBlockTypeMarker->painter.trianglesToCells[cellSection->primitive];
        auto&& cellNodes = DrawUtils::getCellsUnderBrush(cell, tools->selectSize * tools->selectScale);

        std::unordered_set<int> foundCellIds;
        for (auto&& node : cellNodes)
            foundCellIds.insert(node->data);

        (*output)[EFeaturePlacementSelection::Cell] = foundCellIds;
        return true;
    }

    void CellSelection::hoverUpdate(const std::any& data, bool isLive)
    {
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();
        auto&& tools = getStageTools<EGenerationStage::FeaturePlacement>();

        std::unordered_set<int> cellsToUpdate;

        auto&& states = gBlockTypeMarker->painter.cellStates;
        if (!isLive || tools->selectedTool != EFeaturePlacementToolType::Select)
        {
            for (auto&& cellId : hoveredCells)
                states[cellId] = states[cellId] & ~FCellStates::Hovered;

            cellsToUpdate = hoveredCells;
            hoveredCells.clear();
        }
        else
        {
            auto&& newHoveredCells = std::any_cast<std::unordered_set<int>>(data);

            for(auto&& cellId : hoveredCells)
                if (!newHoveredCells.contains(cellId))
                    states[cellId] = states[cellId] & ~FCellStates::Hovered;

            for (auto&& cellId : newHoveredCells)
                states[cellId] = states[cellId] | FCellStates::Hovered;

            cellsToUpdate.insert(hoveredCells.begin(), hoveredCells.end());
            cellsToUpdate.insert(newHoveredCells.begin(), newHoveredCells.end());
            hoveredCells = newHoveredCells;
        }

        updateCellMarkerSelection(cellsToUpdate);
    }

    QMenu* CellSelection::requestContextMenu(const std::any& data)
    {
        auto&& tools = getStageTools<EGenerationStage::FeaturePlacement>();

        QMenu* menu = new QMenu(Omnigen::get());

        menu->addAction(tools->actions[EFeaturePlacementAction::AutoGenerateSelection]);

        menu->addAction(tools->actions[EFeaturePlacementAction::CreateMetaClusters]);
        menu->addAction(tools->actions[EFeaturePlacementAction::RemoveMetaClusterCells]);

        menu->addAction(tools->actions[EFeaturePlacementAction::CreateClusters]);
        menu->addAction(tools->actions[EFeaturePlacementAction::RemoveClusterCells]);

        return menu;
    }

    void CellSelection::getData(const SelectionBase* obj, QSet<DataType>* data)
    {
        for (auto&& cellId : static_cast<const CellSelection*>(obj)->cellIds)
            (*data) += cellId;
    }

    std::vector<QSharedPointer<SelectionBase>> CellSelection::createFromData(const QSet<DataType>& inCells)
    {
        auto sel = QSharedPointer<CellSelection>::create();
        sel->cellIds = std::unordered_set<int>(inCells.begin(), inCells.end());
        sel->select();
        return { sel };
    }

    CellSelection::Selection(const std::any& data)
        : cellIds(std::any_cast<std::unordered_set<int>>(data) )
    {
        if (!bSubtract)
            select();
    }

    void CellSelection::update(const std::any& newData, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        CellSelection* target = currentSelections->empty() ? this : static_cast<CellSelection*>(currentSelections->at(0).get());
        auto&& newCells = std::any_cast<std::unordered_set<int>>(newData);

        deselect();
        for (auto&& cellId : newCells)
            cellIds.insert(cellId);

        if (!bSubtract)
        {
            if (target == this)
            {
                select();
            }
            else
            {
                target->cellIds.insert(cellIds.begin(), cellIds.end());
                target->select();
            }
        }
        else
        {
            if (target != this)
            {
                for(auto&& cellId : newCells)
                    target->cellIds.erase(cellId);
                target->select();
            }
        }
    }

    void CellSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract || bAppend)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D CellSelection::getPosition() const
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();
        auto&& heightData = Generation::Data::get()->getDEM()->heightData;

        GVector2D center = cells[*cellIds.begin()]->getCenter(); 
        return QVector3D(center.x, heightData.sample(center), center.z);
    }

    void CellSelection::select() const
    {
        auto&& states = gBlockTypeMarker->painter.cellStates;

        for (auto&& cellId : cellIds)
            states[cellId] = states[cellId] | FCellStates::Selected;

        updateCellMarkerSelection(cellIds);
    }

    void CellSelection::deselect() const
    {
        auto&& states = gBlockTypeMarker->painter.cellStates;

        for (auto&& cellId : cellIds)
            states[cellId] = states[cellId] & ~FCellStates::Selected;

        updateCellMarkerSelection(cellIds);
    }

    void CellSelection::updateCellMarkerSelection(const std::unordered_set<int>& cellIds)
    {
        auto&& blockTypeMap = Generation::Data::get()->getBlockTypeMap();

        auto&& isSubtract = isKeyDown(VK_CONTROL);
        auto&& isAppend = isKeyDown(VK_SHIFT);

        for (auto&& cellId : cellIds)
        {
            auto blockColor = Generation::ETerrainBlockConstexpr::UseIn<EAC::GetBlockColor>(blockTypeMap[cellId]);
            FCellStates states = gBlockTypeMarker->painter.cellStates[cellId];
            auto&& color = gBlockTypeMarker->painter.cellColors[cellId];

            if (!!(states & FCellStates::Selected) && !!(states & FCellStates::Hovered) && isSubtract)
                color = blockColor * 1.15f;
            else if (!!(states & FCellStates::Selected))
                color = blockColor * 1.35f;
            else if (!!(states & FCellStates::Hovered) && !isSubtract)
                color = blockColor * 1.15f;
            else
                color = blockColor;
        }

        if (!cellIds.empty())
            gBlockTypeMarker->painter.bNeedsBufferUpdate = true;
    }
}
