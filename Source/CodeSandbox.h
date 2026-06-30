#pragma once
#include "Utils/OmniBin/OmniBinQt.h"
#include <QFileDialog>
#include "Scene/Generation/Common/Markers/MarkerDrawable.h"

// Basically do whatever the heck you want here. Good place for testing new ideas.

struct BinTest
{
    void init();
    void save();
    void load();

    QString test0;
    std::vector<int> test;
    QMap<QString, int> test2;
    std::tuple<int, QString, double> test3;
    QMap<std::tuple<int, int>, QSharedPointer<float>> test4;
    QScopedPointer<QString> test5;
    QSet<QString> test6;

    static constexpr int TEST_VOLUME = 10000;

    QMap<int, int> mapTest;
    QHash<int, int> hashTest;
    std::array<int, TEST_VOLUME> arrayTest;

    std::array<QMap<int, int>, TEST_VOLUME> miniMapTest;
    std::array<QHash<int, int>, TEST_VOLUME> miniHashTest;
    std::array<std::array<int, 2>, TEST_VOLUME> miniArrayTest;
};

void coroTest();

void convexHullTest();