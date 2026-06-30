#include "stdafx.h"
#include "AssetPlant.h"
#include "Omnigen.h"
#include "Editor/Sections/PropertySystem/Fields/RangeField.h"
#include "Editor/Sections/PropertySystem/Fields/ArrayMetaField.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"
#include "Editor/Dialogs/AssetCompiler/MultiAssetCompiler.h"
#include "Utils/FbxHelpers.h"
#include "Editor/Sections/PropertySystem/Fields/CheckboxField.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Dialogs/AssetCompiler/AssetCompilerDialog.h"
#include "Utils/ConcaveHull.h"
#include <Mathematics/ConvexHull2.h>
#include "../Assets.h"

OmnigenAsset_Plant::OmnigenAsset()
{
    type = EAsset::Plant;
}

QSharedPointer<OmnigenPropertyListBase> OmnigenAsset_Plant::makePropertyList()
{
    auto props = QSharedPointer<MeshAtlasPropertyList>::create(id);
    props->isWide = true;

    qint64 myId = id;
    props->addField(QSharedPointer<TOmnigenField<bool, CheckBoxField<bool>>>::create(
        "Enabled",
        [myId]() { return QOmnigenAssetMgrSection::isAssetEnabled(myId); },
        [myId](auto&& newVal) { QOmnigenAssetMgrSection::setAssetEnabled(myId, newVal); return true; }
    ));

    props->addField(QSharedPointer<TOmnigenField<EBiomeLayer, ComboFieldEdit<EBiomeLayer>>>::create("Layer",
        [this]() { return layer; },
        [this](auto&& value) { layer = value; return true; },
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EBiomeLayer>()); }
    ));

    props->addField(QSharedPointer<TOmnigenField<EPlantSeeding, ComboFieldEdit<EPlantSeeding>>>::create("Seeding",
        [this]() { return seedingType; },
        [this](auto&& value) { seedingType = value; return true; },
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EPlantSeeding>()); }
    ));

    props->addField(QSharedPointer<TOmnigenField<float>>::create("Abundance",
        [this]() { return abundance; },
        [this](auto&& value) { abundance = std::abs(value); return true; }
    ));

    props->addField(QSharedPointer<TOmnigenField<std::array<ETemperature, 2>, RangeField<std::array<ETemperature, 2>>>>::create("Temperature range",
        [this]() { return temperatureRange; },
        [this](auto&& values) { temperatureRange = values; return true; },
        []() {return new RangeField<std::array<ETemperature, 2>>(ETemperature::Polar, ETemperature::Tropical); }
    ));

    props->addField(QSharedPointer<TOmnigenField<std::array<EHumidity, 2>, RangeField<std::array<EHumidity, 2>>>>::create("Humidity range",
        [this]() { return humidityRange; },
        [this](auto&& values) { humidityRange = values; return true; },
        []() {return new RangeField<std::array<EHumidity, 2>>(EHumidity::Desert, EHumidity::Wet); }
    ));

    props->addField(QSharedPointer<TOmnigenField<std::array<float, 3>, ArrayField<LineEditField<float>, 3>>>::create("Slope Degrees Range",
        [this]() { return slopeDegreesRange; },
        [this](auto&& value) { slopeDegreesRange = value; return true; }));

    props->addField(QSharedPointer<TOmnigenField<std::array<float, 3>, ArrayField<LineEditField<float>, 3>>>::create("Humus Factor Range",
        [this]() { return humusFactorRange; },
        [this](auto&& value) { humusFactorRange = value; return true; }));

    static const float maxScaleMult = 3.0f;
    static const float scaleStep = 0.5f / maxScaleMult;
    for (auto&& mesh : meshes)
    {
        props->addField(QSharedPointer<TOmnigenField<std::array<float, 2>, RangeField<std::array<float, 2>>>>::create("Scale range",
            [&]() { return mesh.getScaleRange(); },
            [&](auto&& values) { mesh.setScaleRange(values); return true; },
            []() {return new RangeField<std::array<float, 2>>(scaleStep, maxScaleMult, scaleStep); }
        ));
    }

    QWidget* loader = new QWidget;
    loader->setLayout(new QHBoxLayout);

    // Save atlas
    auto* saveButton = new QPushButton("Save");
    QObject::connect(saveButton, &QPushButton::clicked, Omnigen::get(), [this]()
        {
            EAssetConstexpr::UseIn<EAC::SaveAsset>(type, sharedFromThis());
        });

    // Build section
    QWidget* controlSection = new QWidget();
    controlSection->setLayout(new QVBoxLayout());
    controlSection->layout()->addWidget(loader);
    controlSection->layout()->addWidget(saveButton);

    props->setControlSection(controlSection);

    return props;
}

void OmnigenAsset_Plant::makeUniqueName()
{
    name = getUniqueName("Plant");
}

std::vector<QSharedPointer<OmnigenAssetBase>> OmnigenAsset_Plant::newAsset()
{
    auto plantAsset = QSharedPointer<OmnigenAsset_Plant>::create();

    auto variationPaths = QFileDialog::getOpenFileNames(nullptr, QObject::tr("Choose variation models"), "", QObject::tr("Mesh files (*.fbx)"));
    for (int varIdx = 0; varIdx < variationPaths.size(); ++varIdx)
    {
        auto&& vatiationPath = variationPaths[varIdx];

        FBXLoader loader;
        FBXEssentialData fbxData = loader.loadFBXFile(vatiationPath.toStdString());
        Q_ASSERT(fbxData.isLoaded);

        // Perform some tasks only once for all variations
        if (varIdx == 0)
        {
            int assetNameDivisor = vatiationPath.size() - vatiationPath.lastIndexOf('/') - 1;
            plantAsset->name = vatiationPath.right(assetNameDivisor);
            auto assetDir = vatiationPath.chopped(assetNameDivisor);

            // Materials
            static const MaterialTextureComplilationDesc desc =
            {
                { ETextureComponentOut::DiffuseHeight, filled_array<4>(ETextureComponentIn::DiffuseAlpha)},
                { ETextureComponentOut::Normal, filled_array<4>(ETextureComponentIn::Normal) }
            };

            Q_ASSERT(!fbxData.materials.empty());
            plantAsset->materials.resize(fbxData.materials.size());
            for (int i = 0; i < plantAsset->materials.size(); ++i)
            {
                QTextureCompilerWindow texCompiler(&plantAsset->materials[i], desc);

                for (auto fbxComp : std::vector{ EFBXTextureType::DiffuseColor, EFBXTextureType::NormalMap })
                {
                    auto&& texPath = fbxData.materials[i].textures[fbxComp][0].filepath;
                    auto img = QImage(assetDir + toQString(texPath)).mirrored().convertToFormat(QImage::Format::Format_RGBA8888);

                    texCompiler.setComponent(*toAssetTextureType(fbxComp), img);
                }

                texCompiler.compile();
            }
        }

        // Meshes
        MeshComponent newMeshComponent;

        Q_ASSERT(fbxData.nodes.size() <= magic_enum::enum_count<ELOD>());
        int nodeIdx = 0;
        for (auto&& node : fbxData.nodes)
        {
            if (!node.meshGeometry)
                continue;

            newMeshComponent.setGeometry(ELOD(nodeIdx++), node.meshGeometry);
        }

        plantAsset->meshes <<= newMeshComponent;
    }

    plantAsset->buildPlacementData();

    return { plantAsset };
}

// Inputs are EBiomeLayer's
float getBiomeLayerHeightFactor(int src, int dest)
{
    static std::array<float, magic_enum::enum_count<EBiomeLayer>()> layerProportions =
    {
        0.0f, // Floor, nothing below
        0.5f, // Low to Floor
        0.5f, // Middle to Low
        0.5f  // High to Middle
    };

    float f = 1.0f;
    for (int i = src; i > dest; --i)
        f *= layerProportions[i];

    return f;
}

void OmnigenAsset_Plant::buildPlacementData() const
{
    placementData.clear();
    placementData.resize(meshes.size());

    for (int meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
    {
        auto&& mesh = meshes[meshIdx];
        auto&& vertices = mesh.getGeometry().at(ELOD::Zero)->vertices;
        auto&& bbox = BoundingBox::fromPoints(vertices, [](const MeshAssetVertex& gv) { return gv.position; });

        // Init box data
        std::map<EBiomeLayer, QVector3D> placementBoxLimits;

        // Init data
        for (int l = int(layer); l >= 0; --l)
        {
            auto&& [box, hull] = placementData[meshIdx][EBiomeLayer(l)];
            auto&& ftr = placementBoxLimits[EBiomeLayer(l)];

            float actualHeight = bbox.sizes.y() - bbox.nbl.y();
            float maxY = actualHeight * getBiomeLayerHeightFactor(int(l), l);

            box.nbl = QVector3D(std::numeric_limits<float>::max(), 0.0f, std::numeric_limits<float>::max());
            ftr = QVector3D(std::numeric_limits<float>::lowest(), maxY, std::numeric_limits<float>::lowest());

            hull.reserve(vertices.size());
        }

        // Fill data
        for (auto&& [targetLayer, data] : placementData[meshIdx])
        {
            auto&& [box, hull] = data;
            auto&& nbl = box.nbl;
            auto&& ftr = placementBoxLimits[targetLayer];

            // Process points for bounding box and hull
            for (auto&& p : vertices)
            {
                auto&& point = p.position;
                if (point.y() > ftr.y())
                    continue;

                if (point.x() < nbl.x()) nbl.setX(point.x());
                if (point.z() < nbl.z()) nbl.setZ(point.z());

                if (point.x() > ftr.x()) ftr.setX(point.x());
                if (point.z() > ftr.z()) ftr.setZ(point.z());

                hull.push_back(point);
            }

            // Finalize bbox
            box.sizes = ftr - nbl;

            // Compute hull
            gte::ConvexHull2<float> convexHullBuilder;
            bool ok = convexHullBuilder(hull.size(), reinterpret_cast<const gte::Vector2<float>*>(hull.data()), 0.001f);
            Q_ASSERT(ok);

            hull = reinterpret_cast<std::vector<GVector2D>&&>(std::move(concaveman<float, 32>(reinterpret_cast<std::vector<std::array<float, 2>>&>(hull), convexHullBuilder.GetHull())));
        }
    }
}

void omniSave(const PlantPlacementData& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.box;
    omniBin << object.concaveHull;
}

void omniLoad(PlantPlacementData& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.box;
    omniBin >> object.concaveHull;
}

void omniSave(const OmnigenAsset_Plant& object, OmniBin<std::ios::out>& omniBin)
{
    object.buildPlacementData();

    omniBin << static_cast<const MeshAtlasAssetBase&>(object);
    omniBin << object.temperatureRange;
    omniBin << object.humidityRange;
    omniBin << object.layer;
    omniBin << object.seedingType;
    omniBin << object.abundance;
    omniBin << object.slopeDegreesRange;
    omniBin << object.humusFactorRange;
    omniBin << object.lightFactorRange;
    omniBin << object.placementData;
}

void omniLoad(OmnigenAsset_Plant& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<MeshAtlasAssetBase&>(object);
    omniBin >> object.temperatureRange;
    omniBin >> object.humidityRange;
    omniBin >> object.layer;
    omniBin >> object.seedingType;
    omniBin >> object.abundance;
    omniBin >> object.slopeDegreesRange;
    omniBin >> object.humusFactorRange;
    omniBin >> object.lightFactorRange;
    omniBin >> object.placementData;
}