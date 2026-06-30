#include "stdafx.h"
#include "AssetMgrSection.h"
#include "Data/Assets/Assets.h"
#include "Utils/Widgets/FlowLayout.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertyList.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Omnigen.h"
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Utils/Crawler/FileCrawler.h"
#include <tbb/parallel_for.h>

QAssetTile::QAssetTile(const QSharedPointer<OmnigenAssetBase>& inAsset)
    : nameWidget(new QLineEdit(inAsset->name))
    , asset(inAsset)
{
    setFrameStyle(QFrame::Panel | QFrame::Raised);
    setLineWidth(3);

    setSizePolicy({ QSizePolicy::Fixed, QSizePolicy::Fixed });
    setFixedSize(tileSize, tileSize);

    auto* layout = new QVBoxLayout();
    setLayout(layout);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    auto label = new QLabel;
    label->setPixmap(QPixmap::fromImage(QImage(100, 80, QImage::Format_Alpha8)));
    label->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(label);

    nameWidget->setContentsMargins(0, 0, 0, 0);
    nameWidget->setReadOnly(true);
    connect(nameWidget, &QLineEdit::editingFinished, this, [&]()
        {
            asset->name = nameWidget->text();
            EAssetConstexpr::UseIn<EAC::SaveAsset>(asset->type, asset);
            nameWidget->clearFocus();
            nameWidget->setText(asset->name);
        });

    layout->addWidget(nameWidget);
}

void QAssetTile::setIsSelected(bool b)
{
    bSelected = b;
    nameWidget->setReadOnly(!bSelected);

    if (bSelected)
        setStyleSheet("QFrame {background-color: cyan ;}");
    else
        setStyleSheet("");
}

void QAssetTile::mousePressEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::MouseButton::LeftButton))
    {
        Omnigen::get()->getAssetsSection()->forceLoadAssets(asset->type, { asset->id });
        emit selected(asset->id);
    }

    return QFrame::mousePressEvent(event);
}

void QAssetTile::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MouseButton::RightButton)
        emit contextMenuRequested(event, asset->id);

    return QFrame::mouseReleaseEvent(event);
}

QOmnigenAssetMgrSection* QOmnigenAssetMgrSection::get()
{
    return Omnigen::get()->getAssetsSection();
}

QOmnigenAssetMgrSection::QOmnigenAssetMgrSection(QWidget* parent)
    : QWidget(parent)
{
    setLayout(new QVBoxLayout);
    setSizePolicy({ QSizePolicy::Expanding, QSizePolicy::Expanding });
    resize(850, 850);

    tabElement = new QTabWidget(this);
    tabElement->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    tabElement->tabBar()->setDocumentMode(true);

    tabLayout = new QVBoxLayout(tabElement);
    tabLayout->setSizeConstraint(QLayout::SizeConstraint::SetNoConstraint);
    tabLayout->setContentsMargins(0, 0, 0, 0);

    for (size_t i = 0; i < magic_enum::enum_count<EAsset>() - 1; i++)
    {
        auto assetType = static_cast<EAsset>(i);
        auto&& asset_name = magic_enum::enum_name<EAsset>(assetType);

        auto&& tabWindowElement = new QWidget();
        auto&& flowLayout = new FlowLayout();
        tabWindowElement->setLayout(flowLayout);

        tabElement->addTab(tabWindowElement, QString::fromStdString(std::string(asset_name)));
        flowLayouts[i] = flowLayout;
    }

    layout()->addWidget(tabElement);

    QObject::connect(tabElement, &QTabWidget::currentChanged, Omnigen::get(), [this](int i)
        {
            rebuildLayout(static_cast<EAsset>(i));
        });

    assetLoaderTimer.setInterval(1000);
    connect(&assetLoaderTimer, &QTimer::timeout, this, &QOmnigenAssetMgrSection::assetLoaderTick);
}

void QOmnigenAssetMgrSection::createNewAsset()
{
    auto assetTypeId = tabElement->currentIndex();
    auto assetType = static_cast<EAsset>(assetTypeId);

    // This uses vector as there's a possibility to reuse textures between Urban assets
    auto newAssets = EAssetConstexpr::UseIn<EAC::CreateAsset>(assetType);
    addAssets(newAssets);
}

void QOmnigenAssetMgrSection::duplicateAsset(const OmnigenAssetBase& asset, QString targetDir) const
{
    if (!targetDir.endsWith('/'))
        targetDir += '/';

    QString fullDataPath = Omnigen::get()->getProjectDir().absolutePath() + "/" + asset.dataPath;

    bool ok = QFile::copy(fullDataPath, targetDir + asset.name + ".oas");
    //Q_ASSERT(ok); // might trigger when already present from previous export

    QString fullMetaPath = fullDataPath;
    fullMetaPath.replace(".oas", ".meta");
    ok = QFile::copy(fullMetaPath, targetDir + asset.name + ".meta");
    //Q_ASSERT(ok); // might trigger when already present from previous export
}

void QOmnigenAssetMgrSection::addAssets(const std::vector<QSharedPointer<OmnigenAssetBase>>& newAssets)
{
    for (auto&& newAsset : newAssets)
    {
        auto* tile = makeAssetTile(newAsset);

        assets[newAsset->type][newAsset->id] = newAsset.staticCast<OmnigenAssetBase>();
        assetTiles[newAsset->type][newAsset->id] = tile;
        flowLayouts[magic_enum::enum_integer<EAsset>(newAsset->type)]->addWidget(tile);
    }
}

void QOmnigenAssetMgrSection::clear()
{
    rebuildLayout();
}

void QOmnigenAssetMgrSection::loadMetadata()
{
    auto loadAssetMetadata = [&](const std::string& path)
    {
        QString qPath = QString::fromStdString(path);
        if (!qPath.endsWith(".meta"))
            return;

        OmniBin<std::ios::in> reader(path);

        EAsset type;
        reader >> type;

        // Get asset meta
        auto asset = EAssetConstexpr::UseIn<EAC::InitializeAssetFromMetadata>(type, reader, std::move(qPath));

        assets[type][asset->id] = asset;
    };

    FileCrawler::Run(loadAssetMetadata, gAssetsPath.toStdString());
    assetLoaderTimer.start();
}

void QOmnigenAssetMgrSection::assetLoaderTick()
{
    if (Omnigen::get()->isGenerating() || isForceLoading)
        return;

    // Load first unloaded asset
    auto loadOneAsset = [&]()
    {
        for (auto&& [type, assetMap] : assets)
            for (auto&& [id, asset] : assetMap)
                if (isAssetEnabled(id) && !asset->isLoaded)
                {
                    auto* loadingTask = EAssetConstexpr::UseIn<EAC::CreateLoadAssetTask>(asset->type, asset);
                    tbb::task::enqueue(*loadingTask);
                    return;
                }
    };
    
    // Create tile for first asset without one
    auto createTileForOneAsset = [&]()
    {
        for (auto&& [type, assetMap] : assets)
        {
            for (auto&& [id, asset] : assetMap)
            {
                if (!assetTiles[type].contains(id))
                {
                    auto* tile = makeAssetTile(asset);
                    flowLayouts[magic_enum::enum_integer<EAsset>(type)]->addWidget(tile);
                    assetTiles[type][id] = tile;
                    return;
                }
            }
        }
    };

    loadOneAsset();
    createTileForOneAsset();
}

void QOmnigenAssetMgrSection::forceLoadAssets(EAsset type, const std::vector<qint64>& ids)
{
	static std::mutex guard;
	std::scoped_lock lock(guard);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    isForceLoading = true;

    tbb::parallel_for(0, int(ids.size()), [&](int i)
        {
            auto&& asset = assets[type][ids[i]];
            if (!asset->isLoaded)
                EAssetConstexpr::UseIn<EAC::LoadAsset>(type, asset);
        });

    isForceLoading = false;
    QApplication::restoreOverrideCursor();
}

void QOmnigenAssetMgrSection::forceLoadAssets(const std::vector<AssetMeta>& meta)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    isForceLoading = true;

    tbb::parallel_for(0, int(meta.size()), [&](int i)
        {
            auto [type, id] = meta[i];
            auto&& asset = assets[type][id];
            if (!asset->isLoaded)
                EAssetConstexpr::UseIn<EAC::LoadAsset>(type, asset);
        });

    isForceLoading = false;
    QApplication::restoreOverrideCursor();
}

void QOmnigenAssetMgrSection::setAssetEnabled(qint64 id, bool enabled)
{
    if (enabled)
        disabledAssets.erase(id);
    else
        disabledAssets.insert(id);
}

bool QOmnigenAssetMgrSection::isAssetEnabled(qint64 id)
{
    return !disabledAssets.contains(id);
}

void QOmnigenAssetMgrSection::syncDisabledAssets()
{
    std::unordered_set<qint64> assetsToForget;
    for (auto id : disabledAssets)
    {
        bool found = false;
        for (auto&& [type, typedAssets] : assets)
            if (found = typedAssets.contains(id); found)
                break;

        if (!found)
            assetsToForget.insert(id);
    }

    for (auto id : assetsToForget)
        disabledAssets.erase(id);
}

void QOmnigenAssetMgrSection::rebuildLayout(EAsset assetType /*= EAsset::Last*/)
{
    auto rebuild = [this](size_t i)
    {
        clearLayout(flowLayouts[i]);
        flowLayouts[i]->addWidget(makeAddAssetButton());

        auto&& assetType = static_cast<EAsset>(i);
        for (auto&& [id, asset] : assets[assetType])
        {
            auto&& tile = assetTiles[assetType][id];

            tile = makeAssetTile(asset);
            flowLayouts[i]->addWidget(tile);
        }
    };

    if (assetType != EAsset::Last)
        rebuild(magic_enum::enum_integer<EAsset>(assetType));
    else
        for (size_t i = 0; i < magic_enum::enum_count<EAsset>() - 1; i++)
            rebuild(i);
}

QWidget* QOmnigenAssetMgrSection::makeAddAssetButton()
{
    auto* button = new QPushButton("+");
    button->setSizePolicy({ QSizePolicy::Fixed, QSizePolicy::Fixed });
    button->setFixedSize(QAssetTile::tileSize, QAssetTile::tileSize);

    connect(button, &QPushButton::clicked, this, &QOmnigenAssetMgrSection::createNewAsset);
    return button;
}

QAssetTile* QOmnigenAssetMgrSection::makeAssetTile(const QSharedPointer<OmnigenAssetBase>& inAsset)
{
    auto* tile = new QAssetTile(inAsset);
    connect(tile, &QAssetTile::selected, this, &QOmnigenAssetMgrSection::selectAsset);
    connect(tile, &QAssetTile::contextMenuRequested, this, &QOmnigenAssetMgrSection::createContextMenu);
    return tile;
}

void QOmnigenAssetMgrSection::selectAsset(qint64 id)
{
    Design::getSelectionMgr()->clearSelection();

    for (auto&& [type, tiles] : assetTiles)
    {
        for (auto&& [tid, tile] : tiles)
        {
            if (tid == id)
            {
                Omnigen::get()->getProperties()->set(tile->getAsset()->makePropertyList());
                tile->setIsSelected(true);
            }
            else
            {
                tile->setIsSelected(false);
            }
        }
    }
}

void QOmnigenAssetMgrSection::createContextMenu(QMouseEvent* event, qint64 id)
{
    auto assetType = static_cast<EAsset>(tabElement->currentIndex());
    Q_ASSERT(assetTiles[assetType].contains(id));
    auto* tile = assetTiles[assetType][id];

    QMenu* contextMenu = new QMenu(tile);

    QAction* deleteAssets = new QAction(QString("Delete %1").arg(tile->getAsset()->name), this);
    connect(deleteAssets, &QAction::triggered, this, [this, id, assetType]()
        {
            removeAsset(assetType, id);
        });

    contextMenu->addAction(deleteAssets);
    contextMenu->exec(event->globalPos());
}

void QOmnigenAssetMgrSection::removeAsset(EAsset type, qint64 id)
{
    auto&& assetToRemove = assets[type][id];

    // Clear asset files
    if (QString oldPath = gAssetsPath + assetToRemove->name + ".oas"; QFile::exists(oldPath))
        QFile::remove(oldPath);

    if (QString oldMetaPath = gAssetsPath + assetToRemove->name + ".meta"; QFile::exists(oldMetaPath))
        QFile::remove(oldMetaPath);

    // Remove asset data
    assets[type].erase(id);
    assetTiles[type].erase(id);

    // Refresh UI
    rebuildLayout(type);

    if (auto propertyId = Omnigen::get()->getProperties()->getPropertyOwner(); propertyId == id)
        Omnigen::get()->getProperties()->clear();
}
