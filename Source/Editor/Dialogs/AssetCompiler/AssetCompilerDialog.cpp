#include "stdafx.h"
#include "AssetCompilerDialog.h"
#include "Omnigen.h"

#include <noise/noise.h>
#include <tbb/parallel_for.h>

#include "ui_AssetCompilerDialog.h"
#include "ui_FramelessDialog.h"

QAssetCompilerDialog::QAssetCompilerDialog(const QSharedPointer<OmnigenAsset_Texture>& tex, const TextureComplilationDesc& inDesc, QWidget* parent /*= nullptr*/)
    : QDialog(parent)
    , ui(new Ui::AssetCompilerDialog)
    , framelessDialogUi(new Ui::FramelessDialog)
    , texture(tex)
    , desc(inDesc)
{
    // Layout
    framelessDialogUi->setupUi(this);
    layout()->setContentsMargins(1, 1, 1, 1);
    setWindowFlags(Qt::FramelessWindowHint);
    ui->setupUi(framelessDialogUi->windowContent);

    // Window controls
    framelessDialogUi->titleText->setText("Asset Compiler");
    connect(ui->exitControls, &QDialogButtonBox::accepted, this, [&]() { compile(); close(); });
    connect(ui->exitControls, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(framelessDialogUi->closeButton, &QPushButton::clicked, this, &QDialog::close);

    std::unordered_set<ETextureComponentIn> neededComps;
    for (auto&& [out, ins] : desc)
        for (auto in : ins)
            neededComps.insert(in);

    // TODO: Change to procedural UI generation

    if (neededComps.contains(ETextureComponentIn::DiffuseAlpha))
    {
        connect(ui->browseDiffuseAlpha, &QToolButton::clicked, this, [&]()
            {
                loadTexture(ETextureComponentIn::DiffuseAlpha, ui->diffuseAlphaPreview);
            });
    }
    else
    {
        ui->browseDiffuseAlpha->hide();
    }

    if (neededComps.contains(ETextureComponentIn::Normal))
    {
        connect(ui->browseNormal, &QToolButton::clicked, this, [&]()
            {
                loadTexture(ETextureComponentIn::Normal, ui->normalPreview);
            });
    }
    else
    {
        ui->browseNormal->hide();
    }

    if (neededComps.contains(ETextureComponentIn::Displacement))
    {
        connect(ui->browseDisplacement, &QToolButton::clicked, this, [&]()
            {
                loadTexture(ETextureComponentIn::Displacement, ui->displacementPreview);
            });
    }
    else
    {
        ui->browseDisplacement->hide();
    }

    if (neededComps.contains(ETextureComponentIn::AmbientOcclusion))
    {
        connect(ui->browseAO, &QToolButton::clicked, this, [&]()
            {
                loadTexture(ETextureComponentIn::AmbientOcclusion, ui->AOPreview);
            });
    }
    else
    {
        ui->browseAO->hide();
    }

    if (neededComps.contains(ETextureComponentIn::Roughness))
    {
        connect(ui->browseRoughness, &QToolButton::clicked, this, [&]()
            {
                loadTexture(ETextureComponentIn::Roughness, ui->roughnessPreview);
            });
    }
    else
    {
        ui->browseRoughness->hide();
    }
}

void QAssetCompilerDialog::setComponent(ETextureComponentIn comp, const QImage& tex)
{
    components[comp] = tex;
}

void QAssetCompilerDialog::compile()
{
    auto [w, h] = getDimensions();

    auto channelFill = [&](QImage* dest, const std::map<int, QImage*>& sources)
    {
        for (auto&& [channel, src] : sources)
            tbb::parallel_for(0, w * h, [&](int i)
                {
                    // fill [dest]'s [channel] with [src]'s red channel
                    // bits() can realloc, so use constBits() and cast this shit
                    *const_cast<uchar*>(dest->constBits() + i * 4 + channel) = *(src->constBits() + i * 4);
                });
    };

    for (auto&& [out, ins] : desc)
    {
        // Initialize with first component
        QImage output = components[ins[0]];

        // Fill with others if any
        std::map<int, QImage*> secondaryComponents;
        for (int channel = 1; channel < 4; ++channel)
            if (ins[channel] != ins[0])
                secondaryComponents[channel] = &components[ins[channel]];

        channelFill(&output, secondaryComponents);

        texture->outputs[out].set(out, output);
    }
}

void QAssetCompilerDialog::loadTexture(ETextureComponentIn comp, QLabel* preview)
{
    QDir dir = Omnigen::get()->getProjectDir();

    QString newTexture = dir.relativeFilePath(QFileDialog::getOpenFileName(this, tr("Choose texture"), "", tr("Images (*.png *.jpg)")));
    if (newTexture.isEmpty())
        return;

    // Find the name
    QFileInfo info(newTexture);
    if (texName.isEmpty())
    {
        texName = info.baseName();
    }
    else if (!bNameShortened)
    {
        QString nextName = info.baseName();
        QString commonStr;
        for (int i = 0; i < std::min(texName.size(), nextName.size()); ++i)
            if (texName[i] == nextName[i])
                commonStr += texName[i];
            else
                break;

        if (commonStr.size() >= 3)
            texName = commonStr;

        bNameShortened = true;
    }

    auto& component = components[comp]; 
    component = QImage(newTexture).mirrored();

    static QHash<QImageData*, QImage> cachedPreviews;
    if (!cachedPreviews.contains(component.data_ptr()))
        cachedPreviews[component.data_ptr()] = component.scaledToHeight(100);

    preview->setPixmap(QPixmap::fromImage(cachedPreviews[component.data_ptr()]));
}

std::array<int, 2> QAssetCompilerDialog::getDimensions() const
{
    Q_ASSERT(!components.empty());
    auto&& mainTex = components.begin()->second;

    return { mainTex.width(), mainTex.height() };
}
