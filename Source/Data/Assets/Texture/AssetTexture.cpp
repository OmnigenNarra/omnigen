#include "stdafx.h"
#include "AssetTexture.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Editor/Sections/PropertySystem/Fields/TextureBrowserField.h"
#include "Editor/Dialogs/AssetCompiler/AssetCompilerDialog.h"
#include "../Assets.h"

OmnigenAsset_Texture::OmnigenAsset()
{
    type = EAsset::Texture;
}

const Texture* OmnigenAsset_Texture::operator()(ETextureComponentOut tc) const
{
    auto it = outputs.find(tc);
    return it != outputs.end()
        ? &it->second
        : nullptr;
}

std::array<int, 2> OmnigenAsset_Texture::getDimensions() const
{
    Q_ASSERT(!outputs.empty());
    auto&& [key, comp] = *outputs.begin();
    return { comp.getData().extent(0).x, comp.getData().extent(0).y };
}

void OmnigenAsset_Texture::setUnitsPerPixel(float inUUP)
{
    unitsPerPixel = inUUP;
}

void OmnigenAsset_Texture::setMaxDisplacement(float inMaxDisplacement)
{
    maxDisplacement = inMaxDisplacement;
}

void OmnigenAsset_Texture::makeUniqueName()
{
    name = getUniqueName("Texture");
}

QSharedPointer<OmnigenPropertyListBase> OmnigenAsset_Texture::makePropertyList()
{
    auto props = QSharedPointer<OmnigenPropertyListBase>::create(id);

    for (auto&& [key, comp] : outputs)
    {
        props->addField(QSharedPointer<TOmnigenField<QImage, TextureBrowserField<QImage>>>::create(
            magic_enum::enum_name(key).data(),
            [this, &comp]() { return comp.getPreview(); },
            [](const QImage&) { return true; }
        ));
    }

    props->addField(QSharedPointer<TOmnigenField<float>>::create(
        "Units per pixel",
        [this]() { return unitsPerPixel; },
        [this](const float& inScale) { setUnitsPerPixel(inScale); return true; }
    ));

    props->addField(QSharedPointer<TOmnigenField<float>>::create(
        "Max displacement",
        [this]() { return maxDisplacement; },
        [this](const float& inMD) { setMaxDisplacement(inMD); return true; }
    ));

    auto* compileButton = new QPushButton("Compile");
    QObject::connect(compileButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            static const TextureComplilationDesc desc =
            {
                {ETextureComponentOut::DiffuseHeight, {ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::DiffuseAlpha, ETextureComponentIn::Displacement}},
                {ETextureComponentOut::Normal, filled_array<4>(ETextureComponentIn::Normal) }
            };

            QAssetCompilerDialog compiler(sharedFromThis().staticCast<OmnigenAsset<EAsset::Texture>>(), desc);
            compiler.exec();
        });

    auto* saveButton = new QPushButton("Save");
    QObject::connect(saveButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            EAssetConstexpr::UseIn<EAC::SaveAsset>(type, sharedFromThis());
        });

    QWidget* controlSection = new QWidget();
    controlSection->setLayout(new QVBoxLayout());
    controlSection->layout()->addWidget(compileButton);
    controlSection->layout()->addWidget(saveButton);

    props->setControlSection(controlSection);

    return props;
}

std::vector<QSharedPointer<OmnigenAssetBase>> OmnigenAsset_Texture::newAsset()
{
    auto asset = QSharedPointer<OmnigenAsset<EAsset::Texture>>::create();
    asset->makeUniqueName();

    return { asset };
}

void omniSave(const OmnigenAsset_Texture& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const OmnigenAssetBase&>(object);
    omniBin << object.outputs;
    omniBin << object.unitsPerPixel;
    omniBin << object.maxDisplacement;
}

void omniLoad(OmnigenAsset_Texture& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<OmnigenAssetBase&>(object);
    omniBin >> object.outputs;
    omniBin >> object.unitsPerPixel;
    omniBin >> object.maxDisplacement;
}