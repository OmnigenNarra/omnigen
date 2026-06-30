#include "stdafx.h"
#include "CoverAssetRockPicker.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"

QCoverAssetRockPicker::QCoverAssetRockPicker(QSharedPointer<OmnigenAsset<EAsset::SoilMaterial>> coverAsset, QWidget* parent /*= nullptr*/)
    : QFramelessWindow(parent, true)
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
    auto* contentLayout = new QVBoxLayout(mainWidget);

    auto&& rockAssets = QOmnigenAssetMgrSection::getAssets<EAsset::RockMaterial>();
    for (auto&& [id, asset] : rockAssets)
    {
        auto* checkbox = new QCheckBox(asset->name);
        checkbox->setCheckState(
            coverAsset->getAllowedRockMaterials().contains(id) 
            ? Qt::CheckState::Checked 
            : Qt::CheckState::Unchecked
        );

        QObject::connect(checkbox, &QCheckBox::stateChanged, this, [id, coverAsset](int state)
            {
                coverAsset->setAllowedRockMaterial(id, bool(state == Qt::CheckState::Checked));
            });

        contentLayout->addWidget(checkbox);
    }

    resize(0, 0);
    setContent(mainWidget);
}
