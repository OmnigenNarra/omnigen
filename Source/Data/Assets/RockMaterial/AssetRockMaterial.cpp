#include "stdafx.h"
#include "AssetRockMaterial.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Omnigen.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"
#include "Editor/Dialogs/AssetCompiler/AssetCompilerDialog.h"
#include "Editor/Dialogs/AssetCompiler/MultiAssetCompiler.h"
#include "Editor/Sections/PropertySystem/Fields/CheckboxField.h"
#include "../Assets.h"

OmnigenAsset_RockMaterial::OmnigenAsset()
{
    type = EAsset::RockMaterial;
}

QSharedPointer<OmnigenPropertyListBase> OmnigenAsset_RockMaterial::makePropertyList()
{
    auto props = QSharedPointer<OmnigenPropertyListBase>::create(id);

    auto* texturePickerButton = new QPushButton("Texture Picker");
    QObject::connect(texturePickerButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            std::vector<ComponentDesc> descs(4);
            for (int i = 0; i < 4; ++i)
            {
                descs[i].material = &materials[i];
                descs[i].desc =
                {
                    { ETextureComponentOut::DiffuseHeight, { ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::Displacement }},
                    { ETextureComponentOut::Normal, filled_array<4>(ETextureComponentIn::Normal) },
                    { ETextureComponentOut::AOR, { ETextureComponentIn::AmbientOcclusion, ETextureComponentIn::Roughness, ETextureComponentIn::AmbientOcclusion, ETextureComponentIn::AmbientOcclusion } }
                };
            }

            descs[0].label = "Solid Rock";
            descs[1].label = "Gravel";
            descs[2].label = "Soil";
            descs[3].label = "Cliff";

            auto&& popup = new QMultiAssetCompilerMainWindow(descs, [this]() { EAssetConstexpr::UseIn<EAC::SaveAsset>(type, sharedFromThis()); }, Omnigen::get());
            popup->show();
        });

    qint64 myId = id;
    props->addField(QSharedPointer<TOmnigenField<bool, CheckBoxField<bool>>>::create(
        "Enabled",
        [myId]() { return QOmnigenAssetMgrSection::isAssetEnabled(myId); },
        [myId](auto&& newVal) { QOmnigenAssetMgrSection::setAssetEnabled(myId, newVal); return true; }
    ));

    props->addField(QSharedPointer<TOmnigenField<int>>::create(
        "Rock Hardness",
        [this]() { return hardness; },
        [this](const int& inputValue) { hardness = inputValue; return true; }
    ));

    props->addField(QSharedPointer<TOmnigenField<int>>::create(
        "Rock Minimum Size",
        [this]() { return minSize; },
        [this](const int& inputValue) { minSize = inputValue; return true; }
    ));

    QWidget* controlSection = new QWidget();
    controlSection->setLayout(new QVBoxLayout());
    controlSection->layout()->addWidget(texturePickerButton);

    props->setControlSection(controlSection);

    return props;
}

std::vector<QSharedPointer<OmnigenAssetBase>> OmnigenAsset_RockMaterial::newAsset()
{
    auto asset = QSharedPointer<OmnigenAsset<EAsset::RockMaterial>>::create();
    asset->makeUniqueName();

    return { asset };
}

void OmnigenAsset_RockMaterial::makeUniqueName()
{
    name = getUniqueName("RockMaterial");
}

void omniSave(const OmnigenAsset_RockMaterial& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const OmnigenAssetBase&>(object);
    omniBin << object.hardness;
    omniBin << object.minSize;
    omniBin << object.materials;
}

void omniLoad(OmnigenAsset_RockMaterial& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<OmnigenAssetBase&>(object);
    omniBin >> object.hardness;
    omniBin >> object.minSize;
    omniBin >> object.materials;
}