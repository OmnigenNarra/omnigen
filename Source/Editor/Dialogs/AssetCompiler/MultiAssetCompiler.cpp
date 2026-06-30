#include "stdafx.h"
#include "MultiAssetCompiler.h"
#include "Editor/Sections/PropertySystem/Fields/TextureBrowserField.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"

#include <tbb/parallel_for.h>

QMultiAssetCompilerMainWindow::QMultiAssetCompilerMainWindow(const std::vector<ComponentDesc>& descs, std::function<void()> inOnSave, QWidget* parent)
    : QFramelessWindow(parent, true)
    , onSave(std::move(inOnSave))
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Rock Material Components");

    QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    // Move window to the middle of the screen
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    QSize windowSize = size();
    int height = (screenGeometry.height() / 2) - (windowSize.height() / 2);
    int width = (screenGeometry.width() / 2) - (windowSize.width() / 2);
    move(width, height);

    // Content
    auto* mainWidget = new QWidget();
    auto* contentLayout = new QGridLayout(mainWidget);
    static const QSize previewSize = { 200, 200 };

    for (int i = 0; i < descs.size(); ++i)
    {
        auto* button = new QPushButton(descs[i].label);
        auto* preview = new QLabel();
        auto* materialPtr = descs[i].material;
        auto desc = descs[i].desc;

        QObject::connect(button, &QPushButton::clicked, this, [this, button, materialPtr, desc, preview]()
            {
                auto&& popupWindow = new QTextureCompilerWindow(materialPtr, desc, this, button->text());
                popupWindow->show();

                QObject::connect(popupWindow, &QTextureCompilerWindow::materialCompiled, this, [this, preview](Material* mc)
                    {
                        preview->setPixmap(QPixmap::fromImage(mc->outputs.begin()->second.getPreview().scaled(previewSize)));
                    });
            });

        preview->setFixedSize(previewSize);

        // Load preview image is exists
        if (!materialPtr->outputs.empty())
            preview->setPixmap(QPixmap::fromImage(materialPtr->outputs.begin()->second.getPreview().scaled(previewSize)));

        contentLayout->addWidget(preview, 0, i);
        contentLayout->addWidget(button, 1, i);
    }

    auto* saveButton = new QPushButton("Save");
    contentLayout->addWidget(saveButton, 2, 0, 1, descs.size());

    QObject::connect(saveButton, &QPushButton::clicked, this, [this]()
        {
            onSave();
            close();
        });

    resize(0, 0);
    setContent(mainWidget);
}

QTextureCompilerWindow::QTextureCompilerWindow(Material* mc, MaterialTextureComplilationDesc inDesc, QWidget* parent, const QString& windowName)
    : QFramelessWindow(parent, true)
    , texture(mc)
    , desc(std::move(inDesc))
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(windowName);

    QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    // Move window to the middle of the screen
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    QSize windowSize = size();
    int height = (screenGeometry.height() / 2) - (windowSize.height() / 2);
    int width = (screenGeometry.width() / 2) - (windowSize.width() / 2);
    move(width, height);

    auto* mainWidget = new QWidget();
    auto* contentLayout = new QGridLayout(mainWidget);

    std::set<ETextureComponentIn> requiredComponents;
    for (auto&& [output, componentsArray] : desc)
        for (ETextureComponentIn in : componentsArray)
            requiredComponents.insert(in);

    
    for (int componentIdx = 0; ETextureComponentIn in : requiredComponents)
    {
        QLabel* preview = new QLabel();
        preview->setFixedSize(100, 100);

        auto* button = new QPushButton(toQString(in));
        connect(button, &QToolButton::clicked, this, [this, preview, in]()
            {
                loadTexture(in, preview);
            });

        contentLayout->addWidget(preview, 0, componentIdx);
        contentLayout->addWidget(button, 1, componentIdx);

        ++componentIdx;
    }

    auto* compileButton = new QPushButton("Compile");

    QLabel* tileSizeLabel = new QLabel("Tile Size");
    QLineEdit* tileSizeBox = new QLineEdit(QString::number(mc->tileSize));

    QLabel* maxDisplacementLabel = new QLabel("Maximum Displacement");
    QLineEdit* maxDisplacementBox = new QLineEdit(QString::number(mc->maxDisplacement));

    contentLayout->addWidget(tileSizeLabel, 2, 0, 1, 2);
    contentLayout->addWidget(tileSizeBox, 2, 2);
    contentLayout->addWidget(maxDisplacementLabel, 3, 0, 1, 2);
    contentLayout->addWidget(maxDisplacementBox, 3, 2);

    contentLayout->addWidget(compileButton, 4, 0, 1, requiredComponents.size());

    connect(compileButton, &QToolButton::clicked, this, [this, mc]()
        {
            compile();
            emit materialCompiled(mc);
            close();
        });

    connect(tileSizeBox, &QLineEdit::editingFinished, this, [this, tileSizeBox, mc]()
        {
            mc->tileSize = tileSizeBox->text().toFloat();
        });

    connect(maxDisplacementBox, &QLineEdit::editingFinished, this, [this, maxDisplacementBox, mc]()
        {
            mc->maxDisplacement = maxDisplacementBox->text().toFloat();
        });

    resize(0,0);
    setContent(mainWidget);
}

void QTextureCompilerWindow::setComponent(ETextureComponentIn comp, const QImage& tex)
{
    components[comp] = tex.convertToFormat(QImage::Format::Format_RGBA8888);
}

void QTextureCompilerWindow::loadTexture(ETextureComponentIn comp, QLabel* preview)
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
    setComponent(comp, QImage(newTexture).mirrored());

    static QHash<QImageData*, QImage> cachedPreviews;
    if (!cachedPreviews.contains(component.data_ptr()))
        cachedPreviews[component.data_ptr()] = component.scaledToHeight(100);

    preview->setPixmap(QPixmap::fromImage(cachedPreviews[component.data_ptr()]));
}

std::array<int, 2> QTextureCompilerWindow::getDimensions() const
{
    Q_ASSERT(!components.empty());
    auto&& mainTex = components.begin()->second;

    return { mainTex.width(), mainTex.height() };
}

void QTextureCompilerWindow::compile()
{
    auto [w, h] = getDimensions();

    auto channelFill = [&](QImage* dest, const std::map<int, QImage*>& sources)
    {
        Q_ASSERT_X(dest->width() == w && dest->height() == h, "dst", "size");
        Q_ASSERT_X(dest->format() == QImage::Format::Format_RGBA8888, "dts", "format");
        for (auto&& [key, img] : sources)
        {
            Q_ASSERT_X(img->width() == w && img->height() == h, "src", "size");
            Q_ASSERT_X(img->format() == QImage::Format::Format_RGBA8888, "src", "format");
        }

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

    texture->id = makeGuid();
}
