#include "stdafx.h"
#include "OmniBinQt.h"

void omniSave(const QString& object, OmniBin<std::ios::out>& omniBin)
{
    QByteArray BA = object.toUtf8();
    omniSave(BA.size(), omniBin);
    omniBin.stream.write(reinterpret_cast<const char*>(BA.data()), BA.size());
}

void omniLoad(QString& object, OmniBin<std::ios::in>& omniBin)
{
    QByteArray BA;
    int s;
    omniLoad(s, omniBin);
    BA.resize(s);

    omniBin.stream.read(reinterpret_cast<char*>(BA.data()), BA.size());
    object = QString::fromUtf8(BA);
}

void omniSave(const QImage& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.width();
    omniBin << object.height();
    omniBin.stream.write(reinterpret_cast<const char*>(object.bits()), 4 * object.width() * object.height());
}

void omniLoad(QImage& object, OmniBin<std::ios::in>& omniBin)
{
    int width, height;
    omniBin >> width;
    omniBin >> height;

    QImage img(width, height, QImage::Format_RGBA8888);
    omniBin.stream.read(reinterpret_cast<char*>(img.bits()), 4 * width * height);

    object = std::move(img);
}

void omniSave(const QByteArray& object, OmniBin<std::ios::out>& omniBin)
{
    omniSave(object.size(), omniBin);
    omniBin.stream.write(reinterpret_cast<const char*>(object.data()), object.size());
}

void omniLoad(QByteArray& object, OmniBin<std::ios::in>& omniBin)
{
    int size;
    omniLoad(size, omniBin);

    object.resize(size);
    omniBin.stream.read(reinterpret_cast<char*>(object.data()), object.size());
}