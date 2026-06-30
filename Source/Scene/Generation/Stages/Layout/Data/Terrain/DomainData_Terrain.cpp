#include "stdafx.h"
#include "DomainData_Terrain.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "../DomainFieldUtils.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"
#include "Editor/Sections/PropertySystem/Fields/SliderField.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"

RidgeCharacter PRidgeCharacter;

void DomainData<EDomainType::Terrain>::fillProps(QSharedPointer<OmnigenPropertyListBase> props)
{
    DomainDataBase::fillProps(props);

    // Store id in the Set() lambda.
    qint64 id = props->ownerId;

    auto landformOptions = gatherEnumsForComboField<ELandform>();

    int domainSize = getOwner(id)->getSquares().size();
    std::vector<bool> landformOptionsEnabled(landformOptions.size(), true);
    // TODO: it would be best to declare "landform shared params" like it was before variations, to cover stuff like domain size
    for (int i = 0; i < landformOptions.size(); ++i)
        if (domainSize < PLandformTypes[getDefaultVariation(landformOptions[i])].minSquares)
            landformOptionsEnabled[i] = false;

    if(maxHeight == -1)
        calculateMaxHeight(id);

    if (Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout)
    {
        props->addField(QSharedPointer<TOmnigenField<ELandform, ComboFieldEdit<ELandform>>>::create("Landform",
            [this]() { return landform; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) 
                { 
                    owner->landform = value; 
                    owner->landformVariation = owner->getDefaultVariation(value);
                    owner->allowedVariations = owner->getAvailableVariations(value);
                    owner->name = owner->makeName();
                    owner->calculateMaxHeight(id); 
                    owner->landformInstanceParams = {};
                    owner->ridgeGenParams = {};
                    DManipulationGizmo::get()->setVisible(false); 
                    return true; 
                }),
            [landformOptions, landformOptionsEnabled]() { return new ComboFieldEdit(landformOptions, landformOptionsEnabled); }
        ));

        allowedVariations = getAvailableVariations(landform);

        props->addField(QSharedPointer<TOmnigenField<ELandformVariations, ComboFieldEdit<ELandformVariations>>>::create("Landform Variation",
            [this]() { return landformVariation; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value)
                {
                    owner->landformVariation = value;
                    return true;
                }),
            [this]() { return new ComboFieldEdit(allowedVariations); }
                ));

        auto tablelandOptions = gatherEnumsForComboField<ETableLand>();
        std::vector<bool> tablelandOptionsEnabled(tablelandOptions.size(), true);

        props->addField(QSharedPointer<TOmnigenField<ETableLand, ComboFieldEdit<ETableLand>>>::create("Tableland",
            [this]() { return tableland; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) { owner->tableland = value; owner->name = owner->makeName(); DManipulationGizmo::get()->setVisible(false); return true; }),
            [tablelandOptions, tablelandOptionsEnabled]() { return new ComboFieldEdit(tablelandOptions, tablelandOptionsEnabled); }
        ));

        props->addField(QSharedPointer<TOmnigenField<ERivers, ComboFieldEdit<ERivers>>>::create("Rivers",
            [this]() { return rivers; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [](auto owner, auto&& value) { owner->rivers = value; return true; }
                ),
            []() { return new ComboFieldEdit(gatherEnumsForComboField<ERivers>()); }
        ));

        props->addField(QSharedPointer<TOmnigenField<float>>::create("Max height",
            [this]() { return maxHeight; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) { owner->maxHeight = value; DManipulationGizmo::get()->setVisible(false); return true; })
        ));

        props->addField(QSharedPointer<TOmnigenField<int, SliderField<int>>>::create("Hills smoothness",
            [this]() { return hillsSmoothness; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [](auto owner, auto&& value) { owner->hillsSmoothness = value; return true; }
                ),
            []() { return new SliderField<int>(0, 100, 1); }
        ));

        props->addField(QSharedPointer<TOmnigenField<int, SliderField<int>>>::create("Landform Openness",
            [this]() { return landformOpenness; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [](auto owner, auto&& value) { owner->landformOpenness = value; return true; }
                ),
            []() { return new SliderField<int>(0, 10, 0); }
        ));
    }
    else if (Generation::Data::get()->getGenerationStage() == EGenerationStage::Ridges)
    {
        props->addField(QSharedPointer<TOmnigenField<ERidgeSize, ComboFieldEdit<ERidgeSize>>>::create("Size",
            [this]() { return ridgeGenParams.size; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) { owner->ridgeGenParams.size = value; return true; }),
            []() { return new ComboFieldEdit(gatherEnumsForComboField<ERidgeSize>()); }
        ));

        props->addField(QSharedPointer<TOmnigenField<ERidgeComplexity, ComboFieldEdit<ERidgeComplexity>>>::create("Main Ridge Complexity",
            [this]() { return ridgeGenParams.complexityMain; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) { owner->ridgeGenParams.complexityMain = value; return true; }),
            []() { return new ComboFieldEdit(gatherEnumsForComboField<ERidgeComplexity>()); }
        ));

        props->addField(QSharedPointer<TOmnigenField<ERidgeComplexity, ComboFieldEdit<ERidgeComplexity>>>::create("Subridge Complexity",
            [this]() { return ridgeGenParams.complexitySub; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) { owner->ridgeGenParams.complexitySub = value; return true; }),
            []() { return new ComboFieldEdit(gatherEnumsForComboField<ERidgeComplexity>()); }
        ));

        props->addField(QSharedPointer<TOmnigenField<ERidgeSpread, ComboFieldEdit<ERidgeSpread>>>::create("Spread",
            [this]() { return ridgeGenParams.spread; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) { owner->ridgeGenParams.spread = value; return true; }),
            []() { return new ComboFieldEdit(gatherEnumsForComboField<ERidgeSpread>()); }
        ));

        props->addField(QSharedPointer<TOmnigenField<float>>::create("Slope Angle",
            [this]() { return ridgeGenParams.slopeAngle; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value) 
                { 
                    if (value < owner->ridgeGenParams.ridgelineAngle)
                        owner->ridgeGenParams.ridgelineAngle = value;

                    owner->ridgeGenParams.slopeAngle = value; 
                    return true; 
                })
        ));

        props->addField(QSharedPointer<TOmnigenField<float>>::create("Ridgeline Angle",
            [this]() { return ridgeGenParams.ridgelineAngle; },
            createDomainSetFieldLambda<DomainData>(
                [id]() { return getOwner(id); },
                [id](auto owner, auto&& value)
                {
                    if (value > owner->ridgeGenParams.slopeAngle)
                    {
                        QMessageBox(QMessageBox::Icon::Critical,
                            QString::fromStdString("Error"),
                            QString::fromStdString("Ridgeline angle cannot be higher than slope angle."), QMessageBox::StandardButton::Ok).exec();
                        return false;
                    }

                    owner->ridgeGenParams.ridgelineAngle = value; 
                    return true;
                })
        ));
    }
}

QString DomainData<EDomainType::Terrain>::makeName(bool isInitial /*= false*/)
{
    auto domains = Generation::Data::get()->getAllDomains<EDomainType::Terrain>();

    int domainCount = isInitial ? 1 : 0;
    for (auto&& [handle, domain] : domains)
        if (domain->getData<EDomainType::Terrain>()->landform == landform)
        {
            if (landform == ELandform::Tablelands && domain->getData<EDomainType::Terrain>()->tableland == tableland)
                domainCount++;
            else if (landform != ELandform::Tablelands)
                domainCount++;
        }

    QString name = (landform == ELandform::Tablelands ? QString::fromStdString(std::string(magic_enum::enum_name(tableland))) + " "  : "");
    name += QString::fromStdString(std::string(magic_enum::enum_name(landform)));
    name += " Terrain";
    name += " " + QString::number(domainCount);

    return name;
}

void DomainData<EDomainType::Terrain>::calculateMaxHeight(qint64 ownerId)
{
    int domainSize = getOwner(ownerId)->getSquares().size();
    maxHeight = Landform::computeMaxRidgeHeight(landform, domainSize) * 100.0f;
}

ELandformVariations DomainData<EDomainType::Terrain>::getDefaultVariation(ELandform lf)
{
    for (auto variation : magic_enum::enum_values<ELandformVariations>())
    {
        auto landformName = QString::fromStdString(std::string(magic_enum::enum_name<ELandform>(lf)));
        auto name = QString::fromStdString(std::string(magic_enum::enum_name<ELandformVariations>(variation)));

        if (name.contains(landformName, Qt::CaseSensitivity::CaseSensitive))
        {
            return variation;
        }
    }

    Q_ASSERT(false);
    return ELandformVariations();
}

std::vector<ELandformVariations> DomainData<EDomainType::Terrain>::getAvailableVariations(ELandform lf)
{
    std::vector<ELandformVariations> availableVariations;

    for (auto variation : magic_enum::enum_values<ELandformVariations>())
    {
        auto landformName = QString::fromStdString(std::string(magic_enum::enum_name<ELandform>(lf)));
        auto name = QString::fromStdString(std::string(magic_enum::enum_name<ELandformVariations>(variation)));

        if (name.contains(landformName, Qt::CaseSensitivity::CaseSensitive))
        {
            // TODO: most likely the best way would be to just change the name of Plains to Smooth Plains or something like this to avoid this check
            if (lf == ELandform::Plains && name.contains("Rugged", Qt::CaseSensitivity::CaseSensitive))
                continue;

            availableVariations.emplace_back(variation);
        }
    }

    return availableVariations;
}
