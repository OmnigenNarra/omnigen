#pragma once
#include <QDialog>
#include "Data/Assets/Texture/AssetTexture.h"

namespace Ui
{
    class AssetCompilerDialog;
    class FramelessDialog;
}

using TextureComplilationDesc = std::map<ETextureComponentOut, std::array<ETextureComponentIn, 4>>;

// This dialog serves to load texture components (diffuse, normal, displacement, etc) 
// and compile them to the final shader input format.
// Some components may be merged.
class QAssetCompilerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QAssetCompilerDialog(const QSharedPointer<OmnigenAsset_Texture>& tex, const TextureComplilationDesc& inDesc, QWidget* parent = nullptr);
    void setComponent(ETextureComponentIn comp, const QImage& tex);
    void compile();
    const auto& getTextureName() const { return texName; };

private:
    void loadTexture(ETextureComponentIn comp, QLabel* preview);
    std::array<int, 2> getDimensions() const;

    Ui::AssetCompilerDialog* ui;
    Ui::FramelessDialog* framelessDialogUi;

    QSharedPointer<OmnigenAsset_Texture> texture;
    TextureComplilationDesc desc;
    std::map<ETextureComponentIn, QImage> components;
    QString texName;
    bool bNameShortened = false;
};