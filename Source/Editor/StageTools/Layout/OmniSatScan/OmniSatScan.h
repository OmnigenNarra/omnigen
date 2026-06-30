#pragma once

#include <QWidget>
#include "ui_OmniSatScan.h"
#include <QMainWindow>
#include <QtQml>
#include <QNetworkAccessManager>
#include <QGeoCoordinate>
#include "Editor/Framework/Style/FramelessWindow/FramelessWindow.h"
#include "SatScanAnalysis.h"

class OmniSatScanWindow : public QFramelessWindow
{
    Q_OBJECT

public:
    static
        OmniSatScanWindow* get(QWidget* parent);

private:
    OmniSatScanWindow(QWidget* parent = Q_NULLPTR);

    static OmniSatScanWindow* instance;
};

class OmniSatScan : public QWidget
{
    Q_OBJECT

public:
    OmniSatScan(QWidget* parent = Q_NULLPTR);

    void setDebugText(const QString& text);

private slots:
    void getCoords(QVariant topLeftCoord, QVariant botRightCoord);
    void getAccessToken();
    void getDEMData();
    void getBiomeData();
    void getQMLMessage(QString msg);
    void getDEMmarginData();

private:
    Ui::OmniSatScan ui;
    bool earthEngine();
    bool createJWT();

    void makePostRequestDEM(const QGeoCoordinate& tl, const QGeoCoordinate& br, bool marginRequest = false);
    void makeGetRequestBiome(const QGeoCoordinate& tl, const QGeoCoordinate& br);

    void areaFragmentation(const QGeoCoordinate& tl, const QGeoCoordinate& br);
    bool fragmentedPost(int x, int y);
    void mergeBufferedImages();
    void finalize(const QPixmap& finalImage);
    void clearData();

    // Gdal
    bool bGDALfinished = true;
    float fRangeToHeightFactor = 0.0f; // Elevation Change for DEM (0 - 255) - based on the range set in makePostRequest()

    // Network
    QJsonDocument serviceKey;
    QNetworkAccessManager* manager;
    QNetworkReply* authReply;
    QNetworkReply* getReply;
    QNetworkReply* postReply;
    QString signedToken = "";
    QString accessToken = "";

    // Request Coordinates
    QGeoCoordinate topLeftCoordinate;
    QGeoCoordinate bottomRightCoordinate;

    // Image fragmentation
    QPair<int, int> fragmentPos;
    QPair<int, int> finalImageSize = {0, 0};
    QMap<QPair<int, int>, QGeoCoordinate> fragmentationMap;
    QMap<QPair<int, int>, QPixmap> bufferedImages;

    QSharedPointer<SatScanAnalysis> dataAnalysis;

    friend class SatScanLandforms;
};
