#include "stdafx.h"
#include "UrbanSelection.h"

#include "Scene/Generation/Common/Markers/LineMarker.h"

#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/UrbanSites/UrbanGen/UrbanSite.h"

namespace Design
{
    SiteSelection::Selection(const std::any& site)
        : sitePtr(std::any_cast<DataType>(site))
    {
        Selection<EUrbanSelection, EUrbanSelection::Site>::select();
    }

    bool SiteSelection::findOnScene(QMap<EUrbanSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        if (!Generation::Data::get())
            return false;

        auto&& terrainCells = Generation::Data::get()->getTerrainCells()->getCells();

        for (auto&& site : Generation::Data::get()->getUrbanSites())
        {
            for (int blockId : site->getAreaIds())
            {
                const float height = Generation::Data::get()->getDEM()->heightData.sample(terrainCells[blockId]->getCenter());

                if (auto point = OmnigenCameraMgr::get()->findPointInWorld(height, x, y); point)
                {
                    if (terrainCells[blockId]->contains(*point))
                    {
                        (*output)[EUrbanSelection::Site] = site;
                        return true;
                    }
                }
                else
                    return false;
            }
        }

        return false;
    }

    QMenu* SiteSelection::requestContextMenu(const std::any& data)
    {
        QMenu* menu = new QMenu(Omnigen::get());
        return menu;
    }

    void SiteSelection::getData(const SelectionBase* obj, QSet<DataType>* data)
    {
        (*data) += static_cast<const SiteSelection*>(obj)->sitePtr;
    }

    std::vector<QSharedPointer<SelectionBase>> SiteSelection::createFromData(
        const QSet<QSharedPointer<Generation::UrbanSite>>& inSites)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& site : inSites)
        {
            auto sel = QSharedPointer<SiteSelection>::create();
            sel->sitePtr = site;
            sel->select();
            results << sel;
        }

        return results;
    }

    void SiteSelection::update(const std::any& newSquare,
        std::vector<QSharedPointer<SelectionBase>>* currentSelections,
        const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {

    }

    void SiteSelection::save(
        std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        currentSelections->emplace_back(sharedFromThis());
    }

    void SiteSelection::select() const
    {
        sitePtr->setIsSelected(true);

        debugMarker = Generation::Data::get()->createMarker<DLineMarker, true>(sitePtr->getAreaPolygon().getCenter(), 10'000, QVector4D(1, 0, 0, 1));
    }

    void SiteSelection::deselect() const
    {
        sitePtr->setIsSelected(false);

        Generation::Data::get()->clearSingleExactMarker<DLineMarker>(debugMarker->getGuid());
    }

    QVector3D SiteSelection::getPosition() const
    {
        return sitePtr->getAreaPolygon().getCenter();
    }

    QSharedPointer<OmnigenPropertyListBase> Selection<EUrbanSelection, EUrbanSelection::Site>::makePropertyList()
    {
        qint64 guid = sitePtr->getGuid();
        auto props = QSharedPointer<OmnigenPropertyListBase>::create(guid, sharedFromThis());
        auto getOwner = [guid]() { return Generation::Data::get()->findUrbanSiteByGuid(guid); };

        props->addField(QSharedPointer<TOmnigenField<QString>>::create(
            "Name",
            [getOwner]()
            {
                return getOwner()->getName();
            },
            [getOwner](auto&& newName)
            {
                auto ridge = getOwner();
                ridge->setName(newName);

                return true;
            }));

        return props;
    }
}
