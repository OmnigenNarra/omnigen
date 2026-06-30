#include "stdafx.h"
#include "AssetSoilMaterial.h"
#include "Omnigen.h"
#include "Editor/Dialogs/AssetCompiler/MultiAssetCompiler.h"
#include "Editor/Sections/PropertySystem/Fields/RangeField.h"
#include "Editor/Sections/PropertySystem/Fields/CheckboxField.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Dialogs/CoverAssetRockPicker.h"
#include "../Assets.h"

OmnigenAsset_SoilMaterial::OmnigenAsset()
{
    type = EAsset::SoilMaterial;
}

QSharedPointer<OmnigenPropertyListBase> OmnigenAsset_SoilMaterial::makePropertyList()
{
    auto props = QSharedPointer<OmnigenPropertyListBase>::create(id);
    props->isWide = true;

    auto* texturePickerButton = new QPushButton("Texture Picker");
    QObject::connect(texturePickerButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            std::vector<ComponentDesc> descs(1);
            auto&& desc = descs[0];
            desc.label = "Default";
            desc.material = &materials[0];
            desc.desc = 
            {
                { ETextureComponentOut::DiffuseHeight, { ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::Displacement }},
                { ETextureComponentOut::Normal, filled_array<4>(ETextureComponentIn::Normal) } 
            };

            auto&& compiler = new QMultiAssetCompilerMainWindow(descs, [this]() { EAssetConstexpr::UseIn<EAC::SaveAsset>(type, sharedFromThis()); }, Omnigen::get());
            compiler->show();
        });

    auto* rockPickerButton = new QPushButton("Rock Picker");
    QObject::connect(rockPickerButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            auto&& picker = new QCoverAssetRockPicker(sharedFromThis().staticCast<OmnigenAsset<EAsset::SoilMaterial>>(), Omnigen::get());
            picker->show();
        });

    qint64 myId = id;
    props->addField(QSharedPointer<TOmnigenField<bool, CheckBoxField<bool>>>::create(
        "Enabled",
        [myId]() { return QOmnigenAssetMgrSection::isAssetEnabled(myId); },
        [myId](auto&& newVal) { QOmnigenAssetMgrSection::setAssetEnabled(myId, newVal); return true; }
    ));

    props->addField(QSharedPointer<TOmnigenField<std::array<ETemperature, 2>, RangeField<std::array<ETemperature, 2>>>>::create(
        "Temperature range",
        [this]() { return temperatureRange; },
        [this](auto&& values) { temperatureRange = values; return true; },
        []() {return new RangeField<std::array<ETemperature, 2>>(ETemperature::Polar, ETemperature::Tropical); }
    ));

    props->addField(QSharedPointer<TOmnigenField<std::array<EHumidity, 2>, RangeField<std::array<EHumidity, 2>>>>::create(
        "Humidity range",
        [this]() { return humidityRange; },
        [this](auto&& values) { humidityRange = values; return true; },
        []() {return new RangeField<std::array<EHumidity, 2>>(EHumidity::Desert, EHumidity::Wet); }
    ));

    QWidget* controlSection = new QWidget();
    controlSection->setLayout(new QVBoxLayout());
    controlSection->layout()->addWidget(texturePickerButton);
    controlSection->layout()->addWidget(rockPickerButton);

    props->setControlSection(controlSection);

    return props;
}

std::vector<QSharedPointer<OmnigenAssetBase>> OmnigenAsset_SoilMaterial::newAsset()
{
    auto asset = QSharedPointer<OmnigenAsset<EAsset::SoilMaterial>>::create();
    asset->makeUniqueName();

    return { asset };
}

void OmnigenAsset_SoilMaterial::setAllowedRockMaterial(qint64 id, bool allowed)
{
    if (allowed)
        rockMaterials.insert(id);
    else
        rockMaterials.erase(id);
}

void OmnigenAsset_SoilMaterial::makeUniqueName()
{
    name = getUniqueName("Soil Material");
}

void omniSave(const OmnigenAsset_SoilMaterial& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const OmnigenAssetBase&>(object);
    omniBin << object.materials;
    omniBin << object.temperatureRange;
    omniBin << object.humidityRange;
    omniBin << object.rockMaterials;
}

void omniLoad(OmnigenAsset_SoilMaterial& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<OmnigenAssetBase&>(object);
    omniBin >> object.materials;
    omniBin >> object.temperatureRange;
    omniBin >> object.humidityRange;
    omniBin >> object.rockMaterials;
}