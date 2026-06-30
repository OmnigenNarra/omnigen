#pragma once
#include "Editor/Framework/Style/FramelessWindow/FramelessWindow.h"
#include "Data/Assets/SoilMaterial/AssetSoilMaterial.h"

class QCoverAssetRockPicker : public QFramelessWindow
{
    Q_OBJECT

public:
    QCoverAssetRockPicker(QSharedPointer<OmnigenAsset<EAsset::SoilMaterial>> coverAsset, QWidget* parent = nullptr);
};