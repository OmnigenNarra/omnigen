#pragma once
#include "OmniBin.h"
#include <QString>
#include <QMap>
#include <QHash>
#include <QPair>
#include <QImage>
#include <QByteArray>

// QString
void omniSave(const QString& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(QString& object, OmniBin<std::ios::in>& omniBin);

// QMap
template<typename K, typename V>
inline void omniSave(const QMap<K, V>& object, OmniBin<std::ios::out>& omniBin)
{
    int s = object.size();
    omniSave(s, omniBin);

    for (auto&& it = object.constKeyValueBegin(); it != object.constKeyValueEnd(); ++it)
        omniSave(*it, omniBin);
}

template<typename K, typename V>
inline void omniLoad(QMap<K, V>& object, OmniBin<std::ios::in>& omniBin)
{
    int s;
    omniLoad(s, omniBin);

    for (int i = 0; i < s; ++i)
    {
        QPair<K, V> element;
        omniLoad(element, omniBin);
        object.insert(element.first, element.second);
    }
}

// QHash
template<typename K, typename V>
inline void omniSave(const QHash<K, V>& object, OmniBin<std::ios::out>& omniBin)
{
    int s;
    omniSave(object.size(), omniBin);

    for (auto&& it = object.constKeyValueBegin(); it != object.constKeyValueEnd(); ++it)
        omniSave(*it, omniBin);
}

template<typename K, typename V>
inline void omniLoad(QHash<K, V>& object, OmniBin<std::ios::in>& omniBin)
{
    int s;
    omniLoad(s, omniBin);

    for (int i = 0; i < s; ++i)
    {
        QPair<K, V> element;
        omniLoad(element, omniBin);
        object.insert(element.first, element.second);
    }
}

// QPair
template<typename T0, typename T1>
inline void omniSave(const QPair<T0, T1>& object, OmniBin<std::ios::out>& omniBin)
{
    omniSave(const_cast<std::add_const_t<T0>&>(object.first), omniBin);
    omniSave(const_cast<std::add_const_t<T1>&>(object.second), omniBin);
}

template<typename T0, typename T1>
inline void omniLoad(QPair<T0, T1>& object, OmniBin<std::ios::in>& omniBin)
{
    omniLoad(const_cast<std::remove_const_t<T0>&>(object.first), omniBin);
    omniLoad(const_cast<std::remove_const_t<T1>&>(object.second), omniBin);
}

// QImage
void omniSave(const QImage& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(QImage& object, OmniBin<std::ios::in>& omniBin);

// QByteArray
void omniSave(const QByteArray& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(QByteArray& object, OmniBin<std::ios::in>& omniBin);

template<> constexpr bool serializeAsPOD<QVector2D> = true;
template<> constexpr bool serializeAsPOD<QVector3D> = true;
template<> constexpr bool serializeAsPOD<QVector4D> = true;

template<> constexpr bool serializeAsPOD<GVector2D> = true;