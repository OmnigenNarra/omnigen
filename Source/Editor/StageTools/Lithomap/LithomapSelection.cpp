#include "stdafx.h"
#include "LithomapSelection.h"
#include "Scene/Generation/Stages/Lithomap/LithomapMarker.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"

namespace Design
{
    bool LithomapCellSelection::findOnScene(QMap<ELithomapSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto cellSection = SelectionMgrBase::findObjectUnderCursor<DLithomapMarker>(screenData);
        if (cellSection)
        {
            (*output)[ELithomapSelection::Cell] = gLithomapMarker->painter.trianglesToCells[cellSection->primitive];
            return true;
        }

        return false;
    }

    void LithomapCellSelection::hoverUpdate(const std::any& data, bool isLive)
    {
    }

    QMenu* LithomapCellSelection::requestContextMenu(const std::any& data)
    {
        QMenu* menu = new QMenu(Omnigen::get());
        return menu;
    }

    void LithomapCellSelection::getData(const SelectionBase* obj, QSet<DataType>* data)
    {
        for (auto&& cellId : static_cast<const LithomapCellSelection*>(obj)->cellIds)
            (*data) += cellId;
    }

    std::vector<QSharedPointer<SelectionBase>> LithomapCellSelection::createFromData(const QSet<DataType>& inCells)
    {
        auto sel = QSharedPointer<LithomapCellSelection>::create();
        sel->cellIds = std::unordered_set<int>(inCells.begin(), inCells.end());
        sel->select();
        return { sel };
    }

    LithomapCellSelection::Selection(const std::any& data)
        : cellIds(std::any_cast<std::unordered_set<int>>(data))
    {
        if (!bSubtract)
            select();
    }

    void LithomapCellSelection::update(const std::any& newData, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        LithomapCellSelection* target = currentSelections->empty() ? this : static_cast<LithomapCellSelection*>(currentSelections->at(0).get());
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
                for (auto&& cellId : newCells)
                    target->cellIds.erase(cellId);
                target->select();
            }
        }
    }

    void LithomapCellSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract || bAppend)
            return;

        currentSelections->emplace_back(sharedFromThis());
    }

    QVector3D LithomapCellSelection::getPosition() const
    {
        auto&& cells = Generation::Data::get()->getTerrainCells()->getCells();
        auto&& heightData = Generation::Data::get()->getDEM()->heightData;

        GVector2D center = cells[*cellIds.begin()]->getCenter();
        return QVector3D(center.x, heightData.sample(center), center.z);
    }

    void LithomapCellSelection::select() const
    {
    }

    void LithomapCellSelection::deselect() const
    {
    }
}