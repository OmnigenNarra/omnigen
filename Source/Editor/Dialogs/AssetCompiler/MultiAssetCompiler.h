#pragma once
#include <QDialog>
#include "Editor/Framework/Style/FramelessWindow/FramelessWindow.h"
#include "Data/Assets/AssetBase.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"

using MaterialTextureComplilationDesc = std::map<ETextureComponentOut, std::array<ETextureComponentIn, 4>>;

struct ComponentDesc
{
    QString label;
    MaterialTextureComplilationDesc desc;
    Material* material;
};

class QMultiAssetCompilerMainWindow : public QFramelessWindow
{
    Q_OBJECT

public:
    QMultiAssetCompilerMainWindow(const std::vector<ComponentDesc>& descs, std::function<void()> inOnSave, QWidget* parent = nullptr);

private:
    std::function<void()> onSave;
};

class QTextureCompilerWindow : public QFramelessWindow
{
    Q_OBJECT

public:
    QTextureCompilerWindow(Material* mc, MaterialTextureComplilationDesc inDesc, QWidget* parent = nullptr, const QString& windowName = "Window");

    void setComponent(ETextureComponentIn comp, const QImage& tex);
    void compile();
    const auto& getTextureName() const { return texName; };

signals:
    void materialCompiled(Material* mc);

private:
    void loadTexture(ETextureComponentIn comp, QLabel* preview);
    std::array<int, 2> getDimensions() const;

    Material* texture;
    MaterialTextureComplilationDesc desc;

    std::map<ETextureComponentIn, QImage> components;
    QString texName;
    bool bNameShortened = false;
};