#include "stdafx.h"
#include "UrbanLayoutSelection.h"

#include "UrbanHandleDrawable.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"
#include "Editor/Sections/PropertySystem/Fields/CheckBoxField.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanSuggestion.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanUtils.h"

namespace Design
{
    SuggestionSelection::Selection(const std::any& inHandle)
        : handlePtr(std::any_cast<DataType>(inHandle)), suggestionPtr(handlePtr->ownedSuggestion)
    {
        SuggestionSelection::select();
    }

    bool SuggestionSelection::findOnScene(
        QMap<EUrbanLayoutSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData)
    {
        std::optional<QSharedPointer<DUrbanHandle>> result;

        auto* camera = OmnigenCameraMgr::get()->getCameraForActiveViewport();
        float minDist = FLT_MAX;

        for (auto&& suggestion : Generation::Data::get()->getUrbanSuggestions())
        {
            if (suggestion.isNull())
                continue;

            auto&& handle = suggestion->getHandle();

            if (!OmnigenCameraMgr::get()->isSpriteHit(handle->getPosition(), handle->getSpriteSize(), x, y))
                continue;

            // Sprite found! Check if it's the closest one to the camera.
            float d = distance(handle->getPosition(), camera->getPosition());
            if (d < minDist)
            {
                result = handle;
                minDist = d;
            }
        }

        if (result)
        {
            (*output)[EUrbanLayoutSelection::SuggestionHandle] = *result;
            return true;
        }

        return false;
    }

    QMenu* SuggestionSelection::requestContextMenu(
        const std::any& data)
    {
        QMenu* menu = new QMenu(Omnigen::get());
        return menu;
    }

    void SuggestionSelection::getData(const SelectionBase* obj,
        QSet<DataType>* data)
    {
        (*data) += static_cast<const SuggestionSelection*>(obj)->handlePtr;
    }

    std::vector<QSharedPointer<SelectionBase>> SuggestionSelection
        ::createFromData(const QSet<QSharedPointer<DUrbanHandle>>& inHandles)
    {
        std::vector<QSharedPointer<SelectionBase>> results;

        for (auto&& handle : inHandles)
        {
            auto sel = QSharedPointer<SuggestionSelection>::create();
            sel->handlePtr = handle;
            sel->select();
            results << sel;
        }

        return results;
    }

    void SuggestionSelection::update(const std::any& newSquare,
        std::vector<QSharedPointer<SelectionBase>>* currentSelections,
        const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot)
    {
    }

    void SuggestionSelection::save(
        std::vector<QSharedPointer<SelectionBase>>* currentSelections)
    {
        currentSelections->emplace_back(sharedFromThis());
    }

    void SuggestionSelection::select() const
    {
        if (!suggestionPtr)
            suggestionPtr = handlePtr->ownedSuggestion;

        suggestionPtr->setIsSelected(true);
    }

    void SuggestionSelection::deselect() const
    {
        suggestionPtr->setIsSelected(false);
        suggestionPtr = nullptr;
    }

    QVector3D SuggestionSelection::getPosition() const
    {
        return suggestionPtr->getCenterPoint3D();
    }

    QSharedPointer<OmnigenPropertyListBase> SuggestionSelection::makePropertyList()
    {
        qint64 guid = suggestionPtr->getGuid();
        auto props = QSharedPointer<OmnigenPropertyListBase>::create(guid, sharedFromThis());
        auto getOwner = [guid]() { return Generation::Data::get()->findUrbanSuggestionByGuid(guid); };

        props->addField(QSharedPointer<TOmnigenField<bool, CheckBoxField<bool>>>::create(
            "Should Generate",
            [getOwner]()
            {
                return getOwner()->getShouldGenerate();
            },
            [getOwner](auto&& newVal)
            {
                auto suggestion = getOwner();

                //if (History::GetContext()->IsUndoingOrRedoing())
                    //Design::RidgesSelectionMgr::get()->setSelection<Design::ERidgesSelection::Ridge>({ ridge });

                suggestion->setShouldGenerate(newVal);
                return true;
            }));

        auto sizeOptions = gatherEnumsForComboField<EUrbanSize>();
        const auto maxSiteSize = getOwner()->getMaxAreaSize();

        std::vector<bool> sizeOptionsEnabled(sizeOptions.size(), true);
        for (auto i = 0; i < sizeOptions.size(); i++)
            if (maxSiteSize < sizeOptions[i])
                sizeOptionsEnabled[i] = false;


        props->addField(QSharedPointer<TOmnigenField<EUrbanSize, ComboFieldEdit<EUrbanSize>>>::create(
            "Town Size",
            [getOwner]()
            {
                return getOwner()->getAreaSize();
            },
            [getOwner](auto&& newSize)
            {
                auto suggestion = getOwner();

                //if (History::GetContext()->IsUndoingOrRedoing())
                    //Design::RidgesSelectionMgr::get()->setSelection<Design::ERidgesSelection::Ridge>({ ridge });

                suggestion->setAreaSize(newSize);
                return true;
            }, 
            [sizeOptions, sizeOptionsEnabled]()
            {
                return new ComboFieldEdit(sizeOptions, sizeOptionsEnabled);
            }));

        props->addField(QSharedPointer<TOmnigenField<bool, CheckBoxField<bool>>>::create(
            "Generate Perimeter Primary Roads",
            [getOwner]()
            {
                return getOwner()->getGenPerimeterRoads();
            },
            [getOwner](auto&& newVal)
            {
                auto suggestion = getOwner();

                //if (History::GetContext()->IsUndoingOrRedoing())
                    //Design::RidgesSelectionMgr::get()->setSelection<Design::ERidgesSelection::Ridge>({ ridge });

                suggestion->setGenPerimeterRoads(newVal);
                return true;
            }));


        return props;
    }
}
