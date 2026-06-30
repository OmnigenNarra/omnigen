#include "stdafx.h"
#include "Colors.h"

const QVector4D& Colors::getColorById(const int colorId)
{
    Q_ASSERT((colorId >= 0 && colorId < colors.size()));
    return colors[colorId];
}

QVector4D Colors::random(bool fullRange)
{
    if (fullRange)
    {
        static std::uniform_real_distribution<float> channelDist(0.0f, 1.0f);
        return { channelDist(eng), channelDist(eng), channelDist(eng), 1.0f };
    }
    else
    {
        return colors[dist(eng)];
    }
}

QVector4D Colors::normalizeRGB(const QVector4D& val)
{
    return QVector4D(val.x() / 255, val.y() / 255, val.z() / 255, val.w() / 255);
}

QVector4D Colors::revertRGBNormalization(const QVector4D& val)
{
    return QVector4D(val.x() * 255, val.y() * 255, val.z() * 255, val.w() * 255);
}