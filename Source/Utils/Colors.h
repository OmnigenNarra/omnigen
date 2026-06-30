#pragma once

/*
 * Static class with some utilities in regard to generating lists of colors for all kinds of purposes.
 * Atm, only supports 20 colors so color Ids referred in doc are between 0 - 19!
 * TODO: Add random RGB normalized decimal generation.
 */
class Colors
{
    inline static std::uniform_int_distribution<int> dist = std::uniform_int_distribution<int>(0, 19);
    inline static std::random_device rd{};
    inline static std::mt19937 eng = std::mt19937(rd());
    inline static std::array<QVector4D, 20> colors = {
        QVector4D{ 1.f, 0.f, 0.f, 1.f }, QVector4D{ 0.f, 1.f, 0.f, 1.f }, QVector4D{ 0.f, 0.f, 1.f, 1.f },
        QVector4D{ 1.f, 1.f, 0.f, 1.f }, QVector4D{ 1.f, 0.f, 1.f, 1.f }, QVector4D{ 0.f, 1.f, 1.f, 1.f },
        QVector4D{ 1.f, 1.f, 1.f, 1.f }, QVector4D{ 0.f, 0.f, 0.f, 1.f }, QVector4D{ 1.f, 0.5f, 0.f, 1.f },
        QVector4D{ 1.f, 0.f, 0.5f, 1.f }, QVector4D{ 0.5f, 1.f, 0.f, 1.f }, QVector4D{ 0.f, 1.f, 0.5f, 1.f },
        QVector4D{ 0.5f, 0.f, 1.f, 1.f }, QVector4D{ 0.f, 0.5f, 1.f, 1.f }, QVector4D{ 0.5f, 0.5f, 0.5f, 1.f },
        QVector4D{ 0.792f, 0.007f, 0.792f, 1.f }, QVector4D{ 0.007f, 0.792f, 0.749f, 1.f }, QVector4D{ 0.654f, 0.741f, 0.078f, 1.f },
        QVector4D{ 0.674f, 0.541f, 0.407f, 1.f }, QVector4D{ 0.576f, 0.407f, 0.674f, 1.f }
    };

public:

    // Retrieve a preset color
    static const QVector4D& getColorById(const int id);

    // Get a random color
    // @presetsOnly : pick from preset colors instead of going full random
    static QVector4D random(bool fullRange = true);

    //Convert a 0 - 255 rgb to 0 - 1.
    static QVector4D normalizeRGB(const QVector4D& val);
    //Convert a 0 - 1 rgb to 0 - 255.
    static QVector4D revertRGBNormalization(const QVector4D& val);

    //Colors list
    inline static const QVector4D& red = colors[0];
    inline static const QVector4D& green = colors[1];
    inline static const QVector4D& blue = colors[2];
    inline static const QVector4D& yellow = colors[3];
    inline static const QVector4D& purple = colors[4];
    inline static const QVector4D& cyan = colors[5];
    inline static const QVector4D& white = colors[6];
    inline static const QVector4D& black = colors[7];
    inline static const QVector4D& orange = colors[8];
    inline static const QVector4D& rose = colors[9];
    inline static const QVector4D& chartreuse = colors[10];
    inline static const QVector4D& springGreen = colors[11];
    inline static const QVector4D& violet = colors[12];
    inline static const QVector4D& azure = colors[13];
    inline static const QVector4D& gray = colors[14];
    inline static const QVector4D& purplePizzazz = colors[15];
    inline static const QVector4D& robinEggBlue = colors[16];
    inline static const QVector4D& laRioja = colors[17];
    inline static const QVector4D& sandal = colors[18];
    inline static const QVector4D& wisteria = colors[19];
};