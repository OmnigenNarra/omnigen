#pragma once
#include <QImage>
#include "Utils/OmniBin/OmniBinQt.h"
#include <gli/texture2d.hpp>
#include <nvtt/nvtt.h>

class QAssetCompilerDialog;
class QTextureCompilerWindow;

// User inputs for the asset compiler
enum class ETextureComponentIn
{
    DiffuseAlpha,
    Normal,
    Displacement,
    AmbientOcclusion,
    Roughness
};

// Shader inputs, what is actually stored in Assets
enum class ETextureComponentOut
{
    DiffuseHeight,
    Normal,
    AOR // Ambient Occlusion + Roughness, unused
};

struct Texture
{
    const auto& getData() const { return data; };
    const auto& getPreview() const { return preview; };

    // Will compress to DXT5 (could use variance based on usage)
    void set(ETextureComponentOut target, const QImage& inData);

    // Creates GPU resource, returns texture id
    GLuint loadIntoGL() const;

    // Creates GPU resource, returns array id
    GLuint initGLArray(int numSlots) const;
    void loadIntoGLArray(GLuint texId, int slot) const;

private:
    struct TextureErrorHandler : public nvtt::ErrorHandler
    {
        virtual void error(nvtt::Error e) override;
    };

    gli::texture2d data;
    QImage preview; // intended to use in QAssetTile

    FRIEND_OMNIBIN(Texture);
};

struct Material
{
    const Texture* operator()(ETextureComponentOut tc) const;
    std::array<int, 2> getDimensions() const;

    float tileSize = 800;

    TODO("Displacement is obsolete")
    float maxDisplacement = 10.0;

    qint64 id;

    std::map<ETextureComponentOut, Texture> outputs;

private:
    FRIEND_OMNIBIN(Material);
};

void omniSave(const Texture& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Texture& object, OmniBin<std::ios::in>& omniBin);

void omniSave(const Material& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Material& object, OmniBin<std::ios::in>& omniBin);