#include "stdafx.h"
#include "Texture.h"
#include <gli/core/load.inl>
#include <gli/save_dds.hpp>
#include "Utils/magic_enum.hpp"

const std::map<ETextureComponentOut, nvtt::Format> compressionMap =
{
    {ETextureComponentOut::DiffuseHeight, nvtt::Format::Format_BC7},
    {ETextureComponentOut::Normal, nvtt::Format::Format_DXT5},
    {ETextureComponentOut::AOR, nvtt::Format::Format_DXT1},
};

void Texture::TextureErrorHandler::error(nvtt::Error e)
{
    std::string errorMessage;

    switch (e)
    {
    case nvtt::Error_InvalidInput:
        errorMessage = "The input to the function was invalid (for instance, a negative size).";
        break;
    case nvtt::Error_UnsupportedFeature:
        errorMessage = "Unsupported feature.";
        break;
    case nvtt::Error_CudaError:
        errorMessage = "CUDA reported an error during an operation.";
        break;
    case nvtt::Error_FileOpen:
        errorMessage = "I/O error attempting to open the given file.";
        break;
    case nvtt::Error_FileWrite:
        errorMessage = "I/O error attempting to write to the given file.";
        break;
    case nvtt::Error_UnsupportedOutputFormat:
        errorMessage = "The chosen container does not support the requested format (for instance, attempting to store BC7 data in a DDS file without the DX10 header.)";
        break;
    case nvtt::Error_Unknown:
    case nvtt::Error_Count:
        errorMessage = "Unknown Error.";
        break;
    }

    Q_ASSERT_X(false, "Texture::Set", errorMessage.c_str());
}

void Texture::set(ETextureComponentOut target, const QImage& inData)
{
    Q_ASSERT(compressionMap.size() == magic_enum::enum_count<ETextureComponentOut>());

    QImage base = inData.convertToFormat(QImage::Format_RGBA8888);
    preview = base.scaled(QSize(64, 64));
    std::string tmpName = std::to_string(base.cacheKey()) + ".dds";

    {
        nvtt::Surface image;
        if (!image.setImage(nvtt::InputFormat_BGRA_8UB, base.width(), base.height(), 1, base.bits()))
            Q_ASSERT(false);

        image.swizzle(2, 1, 0, 3);

        // Compute the number of mips.
        const int numMipmaps = image.countMipmaps();

        // Compressing & mip creation
        nvtt::CompressionOptions compressionOptions;
        compressionOptions.setFormat(compressionMap.at(target));

        nvtt::OutputOptions outputOptions;
        outputOptions.setFileName(tmpName.data());

        TextureErrorHandler newErrorHandler;
        outputOptions.setErrorHandler(&newErrorHandler);

        nvtt::Context context(true);

        // Write the DDS header.
        // Errors handled @TextureErrorHandler::error
        context.outputHeader(image, numMipmaps, compressionOptions, outputOptions);

        for (int mip = 0; mip < numMipmaps; ++mip)
        {
            // Compress this image and write its data.
            if (!context.compress(image, 0 /* face */, mip, compressionOptions, outputOptions))
                Q_ASSERT(false);

            if (mip == numMipmaps - 1)
                break;

            // Prepare the next mip:

            // Convert to linear premultiplied alpha. Note that toLinearFromSrgb()
            // will clamp HDR images; consider e.g. toLinear(2.2f) instead.
            image.toLinearFromSrgb();
            image.premultiplyAlpha();

            // Resize the image to the next mipmap size.
            // NVTT has several mipmapping filters; Box is the lowest-quality, but
            // also the fastest to use.
            image.buildNextMipmap(nvtt::MipmapFilter_Box);
            // For general image resizing. use image.resize().

            // Convert back to unpremultiplied sRGB.
            image.demultiplyAlpha();
            image.toSrgb();
        }
    }

    data = gli::texture2d(gli::load(tmpName.data()));
    Q_ASSERT(!data.empty());

    QFile(toQString(tmpName)).remove();
}

// Source: https://github.com/g-truc/gli/blob/master/manual.md
GLuint Texture::loadIntoGL() const
{
    auto& Texture = data;
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();

    gli::gl GL(gli::gl::PROFILE_GL33);
    gli::gl::format const Format = GL.translate(Texture.format(), Texture.swizzles());
    GLenum Target = GL.translate(Texture.target());

    GLuint TextureName = 0;
    glGenTextures(1, &TextureName);
    glBindTexture(Target, TextureName);
    glTexParameteri(Target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(Target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(Texture.levels() - 1));
    glTexParameteri(Target, GL_TEXTURE_SWIZZLE_R, Format.Swizzles[0]);
    glTexParameteri(Target, GL_TEXTURE_SWIZZLE_G, Format.Swizzles[1]);
    glTexParameteri(Target, GL_TEXTURE_SWIZZLE_B, Format.Swizzles[2]);
    glTexParameteri(Target, GL_TEXTURE_SWIZZLE_A, Format.Swizzles[3]);

    glm::tvec3<GLsizei> const Extent(static_cast<const gli::texture&>(Texture).extent());
    GLsizei const FaceTotal = static_cast<GLsizei>(Texture.layers() * Texture.faces());

    switch (Texture.target())
    {
    case gli::TARGET_1D:
        Q_ASSERT(false);
        break;
    case gli::TARGET_1D_ARRAY:
    case gli::TARGET_2D:
    case gli::TARGET_CUBE:
        gl->glTexStorage2D(
            Target, static_cast<GLint>(Texture.levels()), Format.Internal,
            Extent.x, Texture.target() == gli::TARGET_2D ? Extent.y : FaceTotal);
        break;
    case gli::TARGET_2D_ARRAY:
    case gli::TARGET_3D:
    case gli::TARGET_CUBE_ARRAY:
        gl->glTexStorage3D(
            Target, static_cast<GLint>(Texture.levels()), Format.Internal,
            Extent.x, Extent.y,
            Texture.target() == gli::TARGET_3D ? Extent.z : FaceTotal);
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    for (std::size_t Layer = 0; Layer < Texture.layers(); ++Layer)
        for (std::size_t Face = 0; Face < Texture.faces(); ++Face)
            for (std::size_t Level = 0; Level < Texture.levels(); ++Level)
            {
                GLsizei const LayerGL = static_cast<GLsizei>(Layer);
                glm::tvec3<GLsizei> Extent(static_cast<const gli::texture&>(Texture).extent(Level));
                Target = gli::is_target_cube(Texture.target())
                    ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + Face)
                    : Target;

                switch (Texture.target())
                {
                case gli::TARGET_1D_ARRAY:
                case gli::TARGET_2D:
                case gli::TARGET_CUBE:
                    if (gli::is_compressed(Texture.format()))
                        gl->glCompressedTexSubImage2D(
                            Target, static_cast<GLint>(Level),
                            0, 0,
                            Extent.x,
                            Texture.target() == gli::TARGET_1D_ARRAY ? LayerGL : Extent.y,
                            Format.Internal, static_cast<GLsizei>(Texture.size(Level)),
                            Texture.data(Layer, Face, Level));
                    else
                        gl->glTexSubImage2D(
                            Target, static_cast<GLint>(Level),
                            0, 0,
                            Extent.x,
                            Texture.target() == gli::TARGET_1D_ARRAY ? LayerGL : Extent.y,
                            Format.External, Format.Type,
                            Texture.data(Layer, Face, Level));
                    break;
                case gli::TARGET_2D_ARRAY:
                case gli::TARGET_3D:
                case gli::TARGET_CUBE_ARRAY:
                    if (gli::is_compressed(Texture.format()))
                        gl->glCompressedTexSubImage3D(
                            Target, static_cast<GLint>(Level),
                            0, 0, 0,
                            Extent.x, Extent.y,
                            Texture.target() == gli::TARGET_3D ? Extent.z : LayerGL,
                            Format.Internal, static_cast<GLsizei>(Texture.size(Level)),
                            Texture.data(Layer, Face, Level));
                    else
                        gl->glTexSubImage3D(
                            Target, static_cast<GLint>(Level),
                            0, 0, 0,
                            Extent.x, Extent.y,
                            Texture.target() == gli::TARGET_3D ? Extent.z : LayerGL,
                            Format.External, Format.Type,
                            Texture.data(Layer, Face, Level));
                    break;
                default: 
                    Q_ASSERT(false); 
                    break;
                }
            }
    return TextureName;
}

GLuint Texture::initGLArray(int numSlots) const
{
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();

    gli::gl GL(gli::gl::PROFILE_GL33);
    gli::gl::format const Format = GL.translate(data.format(), data.swizzles());

    GLuint TextureName = 0;
    glGenTextures(1, &TextureName);
    glBindTexture(GL_TEXTURE_2D_ARRAY, TextureName);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(data.levels() - 1));
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_R, Format.Swizzles[0]);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_G, Format.Swizzles[1]);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_B, Format.Swizzles[2]);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_A, Format.Swizzles[3]);

    auto extent = data.extent();
    gl->glTexStorage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(data.levels()), Format.Internal, extent.x, extent.y, numSlots);

    return TextureName;
}

void Texture::loadIntoGLArray(GLuint texId, int slot) const
{
    auto& Texture = data;
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();

    Q_ASSERT(data.faces() == 1);

    gli::gl GL(gli::gl::PROFILE_GL33);
    gli::gl::format const Format = GL.translate(Texture.format(), Texture.swizzles());

    for (std::size_t Level = 0; Level < Texture.levels(); ++Level)
    {
        glm::tvec3<GLsizei> Extent(static_cast<const gli::texture&>(Texture).extent(Level));

        gl->glCompressedTexSubImage3D(
            GL_TEXTURE_2D_ARRAY, static_cast<GLint>(Level),
            0, 0, slot,
            static_cast<GLint>(Extent.x), static_cast<GLint>(Extent.y), 1,
            Format.Internal, static_cast<GLsizei>(Texture.size(Level)),
            Texture.data(0, 0, Level));
    }
}

const Texture* Material::operator()(ETextureComponentOut tc) const
{
    auto it = outputs.find(tc);
    return it != outputs.end()
        ? &it->second
        : nullptr;
}

std::array<int, 2> Material::getDimensions() const
{
    Q_ASSERT(!outputs.empty());
    auto&& [key, comp] = *outputs.begin();
    return { comp.getData().extent(0).x, comp.getData().extent(0).y };
}

void omniSave(const Texture& object, OmniBin<std::ios::out>& omniBin)
{
    std::vector<char> buf;
    gli::save_dds(object.data, buf);

    omniBin << buf;
    omniBin << object.preview;
}

void omniLoad(Texture& object, OmniBin<std::ios::in>& omniBin)
{
    std::vector<char> buf;
    omniBin >> buf;
    omniBin >> object.preview;

    object.data = gli::texture2d(gli::load_dds(buf.data(), buf.size()));
}

void omniSave(const Material& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.tileSize;
    omniBin << object.maxDisplacement;
    omniBin << object.id;
    omniBin << object.outputs;
}

void omniLoad(Material& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.tileSize;
    omniBin >> object.maxDisplacement;
    omniBin >> object.id;
    omniBin >> object.outputs;
}