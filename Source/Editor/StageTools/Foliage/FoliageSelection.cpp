#include "stdafx.h"
#include "FoliageSelection.h"
#include "Scene/Generation/Stages/Foliage/PlantDrawable.h"
#include "Data/Assets/Plant/AssetPlant.h"

namespace Design
{
    bool FoliageInstanceSelection::findOnScene(QMap<EFoliageSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        auto plant = SelectionMgrBase::findObjectUnderCursor<Generation::DPlant>(screenData);
        if (plant)
        {
            (*output)[EFoliageSelection::Instance] = FoliageInstance(plant->object, plant->instance);
            return true;
        }

        return false;
    }

    void FoliageInstanceSelection::hoverUpdate(const std::any& data, bool isLive)
    {
    }

    QMenu* FoliageInstanceSelection::requestContextMenu(const std::any& data)
    {
        QMenu* menu = new QMenu(Omnigen::get());
        return menu;
    }

    void FoliageInstanceSelection::getData(const SelectionBase* obj, QSet<FoliageInstance>* data)
    {
        (*data) += static_cast<const FoliageInstanceSelection*>(obj)->instances;
    }

    std::vector<QSharedPointer<SelectionBase>> FoliageInstanceSelection::createFromData(const QSet<FoliageInstance>& inInstances)
    {
        auto sel = QSharedPointer<FoliageInstanceSelection>::create();

        sel->instances = inInstances;
        sel->select();

        return { sel };
    }

    FoliageInstanceSelection::Selection(const std::any& inInstances)
        : instances({ std::any_cast<FoliageInstance>(inInstances) })
    {
        if (!bSubtract)
            select();
    }

    QSharedPointer<OmnigenPropertyListBase> FoliageInstanceSelection::makePropertyList()
    {
        return {};
    }

    void FoliageInstanceSelection::update(const std::any& newInstance, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
        if (!bSubtract)
            return;

        instances.insert(std::any_cast<FoliageInstance>(newInstance));
    }

    void FoliageInstanceSelection::save(std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        if (bSubtract)
            return;

        currentSelections->emplace_back(sharedFromThis());

        // Debug draw bounding box
        for (auto&& instance : instances)
        {
            auto&& geom = instance.plant->getActiveInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();
            auto&& transform = geom->getInstanceTransform(instance.instanceIdx);
            auto bbox = instance.plant->getBoundingBox().transformed(transform);
            bbox.show();
            Generation::Data::get()->initializeQueuedMarkers();
        }
    }

    QVector3D FoliageInstanceSelection::getPosition() const
    {
        QVector3D pos;
        for (auto&& instance : instances)
        {
            auto&& geom = instance.plant->getActiveInstancedGeometry<MeshAssetVertex, MeshAssetInstanceData>();
            auto&& transform = geom->getInstanceTransform(instance.instanceIdx);
            pos += QVector3D(transform(0, 3), transform(1, 3), transform(2, 3));
        }

        pos /= instances.size();
        return pos;
    }

    void FoliageInstanceSelection::select() const
    {
    }

    void FoliageInstanceSelection::deselect() const
    {
    }
}