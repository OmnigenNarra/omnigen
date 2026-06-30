#include "stdafx.h"
#include "OmniSatScan.h"
#include "Omnigen.h"
#include "Utils/jwt/jwt.hpp"
#include "Editor/StageTools/Layout/LayoutSelection.h"

#include <QQuickWidget>
#include <QQuickItem>
#include <QScreen>
#include <QPainter>

OmniSatScanWindow* OmniSatScanWindow::instance;

OmniSatScanWindow* OmniSatScanWindow::get(QWidget* parent)
{
    if (!instance)
        instance = new OmniSatScanWindow(parent);

    return instance;
}

OmniSatScanWindow::OmniSatScanWindow(QWidget* parent)
    : QFramelessWindow(parent, true)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Omnigen Satellite Scanner");

    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    resize(750, 600);

    // Move window to middle of screen
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    QSize windowSize = size();
    int height = (screenGeometry.height() / 2) - (windowSize.height() / 2);
    int width = (screenGeometry.width() / 2) - (windowSize.width() / 2);
    move(width, height);

    auto* mainWinget = new OmniSatScan(this);

    setContent(mainWinget);

    connect(this, &QObject::destroyed, this, [this]() {instance = nullptr; });
}

OmniSatScan::OmniSatScan(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    ui.placeForMap->setSource(QUrl::fromLocalFile("Resources/Map.qml"));
    QObject* obj = qobject_cast<QObject*>(ui.placeForMap->rootObject());

    if (!obj)
    {
        OmniLog(ELoggingLevel::Error) <<= "Map Widget could not be loaded - aborting.";
        return;
    }

    manager = new QNetworkAccessManager(this);

    QObject::connect(obj, SIGNAL(sendCoords(QVariant, QVariant)), this, SLOT(getCoords(QVariant, QVariant)));
    QObject::connect(obj, SIGNAL(sendMsg(QString)), this, SLOT(getQMLMessage(QString)));

    if (earthEngine())
        OmniLog(ELoggingLevel::Info) <<= "Earth Engine successfully queried.";
}

void OmniSatScan::setDebugText(const QString& text)
{
    ui.eeData->setText(text);
}

void OmniSatScan::getCoords(QVariant topLeftCoord, QVariant botRightCoord)
{
    topLeftCoordinate = topLeftCoord.value<QGeoCoordinate>();
    bottomRightCoordinate = botRightCoord.value<QGeoCoordinate>();
    auto topRightCoordinate = QGeoCoordinate(topLeftCoordinate.latitude(), bottomRightCoordinate.longitude());
    auto bottomLeftCoordinate = QGeoCoordinate(bottomRightCoordinate.latitude(), topLeftCoordinate.longitude());

    ui.topLValue->setText("(" + QString::number(topLeftCoordinate.latitude()) + " , " + QString::number(topLeftCoordinate.longitude()) + ")");
    ui.topRValue->setText("(" + QString::number(topRightCoordinate.latitude()) + " , " + QString::number(topRightCoordinate.longitude()) + ")");
    ui.bottomLValue->setText("(" + QString::number(bottomLeftCoordinate.latitude()) + " , " + QString::number(bottomLeftCoordinate.longitude()) + ")");
    ui.bottomRValue->setText("(" + QString::number(bottomRightCoordinate.latitude()) + " , " + QString::number(bottomRightCoordinate.longitude()) + ")");

    if (!bGDALfinished)
    { 
        ui.eeData->setText(QString("Previous request not fully computed, new request terminated."));
        return;
    }

    if (topLeftCoordinate.isValid() && bottomRightCoordinate.isValid() && !accessToken.isEmpty())
        areaFragmentation(topLeftCoordinate, bottomRightCoordinate);
}

void OmniSatScan::getAccessToken()
{
    QByteArray response = authReply->readAll();
    QJsonDocument accessTokenJson = QJsonDocument::fromJson(response);
    accessToken = accessTokenJson.object().value("access_token").toString();
}

void OmniSatScan::getDEMData()
{
    bGDALfinished = false;

    QByteArray response = postReply->readAll();
    QString filePath = QString("Output/Earth Engine/");

    QDir directory;
    if (!directory.exists(filePath))
        directory.mkpath(filePath);

    QFile file(QString(filePath + "DEMRaster.tif"));
    file.open(QIODevice::WriteOnly);
    file.write(response);
    file.close();

    auto selectedArea = Design::LayoutSelectionMgr::get()->getSelection<Design::ELayoutSelection::Grid>();
    for (auto&& it = selectedArea.begin(); it != selectedArea.end();)
    {
        if (Generation::Data::get()->getDomainAtSquare((*it), EDomainType::Terrain))
            it = selectedArea.erase(it);
        else
            ++it;
    }

    if (selectedArea.empty())
    {
        OmniLog(ELoggingLevel::Warn) <<= "No squares were selected, aborting generation.";
        return;
    }
    else if (selectedArea.size() < 60)
        OmniLog(ELoggingLevel::Warn) <<= "Very few squares selected, some landforms might not be properly represented";

    dataAnalysis = QSharedPointer<SatScanAnalysis>::create(SatScanAnalysis(ui.copyCheckBox->isChecked(), fRangeToHeightFactor, selectedArea));

    // Ridgeline Raster
    QFile finalfile(QString(filePath + "FinalRaster.tif"));
    finalfile.open(QIODevice::ReadOnly);
    QPixmap finalPxmap;
    finalPxmap.load(QString(filePath + "FinalRaster.tif"));
    ui.mapLabel->setPixmap(finalPxmap);
    finalfile.close();

    if(ui.copyCheckBox->isChecked())
    {
        // Request DEM with slight margin for isohypse generation
        auto topLeftLatitude = topLeftCoordinate.latitude() - ((bottomRightCoordinate.latitude() - topLeftCoordinate.latitude()) * 0.05);
        auto topLeftLongitude = topLeftCoordinate.longitude() - ((bottomRightCoordinate.longitude() - topLeftCoordinate.longitude()) * 0.05);

        auto bottomRightLatitude = bottomRightCoordinate.latitude() + ((bottomRightCoordinate.latitude() - topLeftCoordinate.latitude()) * 0.05);
        auto bottomRightLongitude = bottomRightCoordinate.longitude() + ((bottomRightCoordinate.longitude() - topLeftCoordinate.longitude()) * 0.05);

        QGeoCoordinate topLeftWithMargin = { topLeftLatitude, topLeftLongitude };
        QGeoCoordinate bottomRightWithMargin = { bottomRightLatitude, bottomRightLongitude };

        makePostRequestDEM(topLeftWithMargin, bottomRightWithMargin, true);
    }
    else
    {
        makeGetRequestBiome(fragmentationMap[{0, 0}], fragmentationMap[{1, 1}]);
    }
}

void OmniSatScan::getBiomeData()
{
    auto reply = (QString)postReply->readAll();
    QJsonDocument jsonResponse = QJsonDocument::fromJson(reply.toUtf8());

    QJsonObject jsonObj = jsonResponse.object();

    // GeoJSON received:
    // 
    //type (will always be "FeatureCollection")
    //features [
    //  type (always "Feature")
    //  geometry (polygon of the region)
    //  properties (consists of 14 properties https://developers.google.com/earth-engine/datasets/catalog/RESOLVE_ECOREGIONS_2017)
    //  ]
    //nextPage - A token to retrieve the next page of results

    auto featuresArray = jsonObj["features"].toArray();

    QString debugData = "";

    for (auto&& feature : featuresArray)
    {
        auto featureProperties = feature.toObject()["properties"].toObject().toVariantMap();
        for (auto it = featureProperties.keyValueBegin(); it != featureProperties.keyValueEnd(); ++it)
        {
            OmniLog(ELoggingLevel::Info) << (*it).first << ": " <<= (*it).second.toString();
            if ((*it).first == "ECO_NAME")
                debugData += (*it).second.toString() + '\n';
        }
    }

    ui.eeBiome->setText(debugData);

    clearData();
    bGDALfinished = true;
}

void OmniSatScan::getQMLMessage(QString msg)
{
    OmniLog(ELoggingLevel::Info) <<= msg;
}

void OmniSatScan::getDEMmarginData()
{
    QByteArray response = postReply->readAll();
    QString filePath = QString("Output/Earth Engine/");

    QDir directory;
    if (!directory.exists(filePath))
        directory.mkpath(filePath);

    QFile file(QString(filePath + "DEMMarginRaster.tif"));
    file.open(QIODevice::WriteOnly);
    file.write(response);
    file.close();

    //dataAnalysis->createIsohypsesOutOfDEM();

    makeGetRequestBiome(fragmentationMap[{0, 0}], fragmentationMap[{1, 1}]);
}

bool OmniSatScan::earthEngine()
{
    QUrl requestUrlHeader("https://oauth2.googleapis.com/token");
    QNetworkRequest request(requestUrlHeader);
    request.setSslConfiguration(QSslConfiguration::defaultConfiguration());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    if (!createJWT())
        return false;

    QUrlQuery requestUrlQuery;
    requestUrlQuery.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer");
    requestUrlQuery.addQueryItem("assertion", signedToken);

    if (requestUrlHeader.isValid())
    {
        authReply = manager->post(request, requestUrlQuery.toString(QUrl::FullyEncoded).toUtf8());
        connect(authReply, &QNetworkReply::finished, this, &OmniSatScan::getAccessToken);
        return true;
    }

    OmniLog(ELoggingLevel::Error) <<= "Invalid Url while trying to access Earth Engine.";
    return false;
}

bool OmniSatScan::createJWT()
{
    unsigned long secondsSinceEpoch = std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1);
    unsigned long expTime = secondsSinceEpoch + 3600;

    //Service Account Private Key to use for the algorithm
    auto key = "-----BEGIN PRIVATE KEY-----\n\
        MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCrQYtb3jA06zK/\n\
        UMxWQy3gZmsbvOr1EfRJ7ZDhZHfztRFUrjOXZBuqCkh1dNaCq6xnCDa3KCYIErXE\n\
        J66DzMcCDYO86LoDEdFR4M3UZjq7o1OUlfUK2dUqgmZGyGNKqsghvkneYkEBLbGR\n\
        DOY2NPxAXus5Vx0gblKIJXtd5rATY5MPCAJK+j7DUdM90psiel4zXZBN9w8DKW+g\n\
        QQ7HnibrOH56qrwM8aMFPXic7Ljwu4+dLP/u9sM/c3NaJ2NZZ77M3tanWYyvUBtT\n\
        ntNFhuoZjD/GOYJy5XHednUlXpjNGUIlUpVOXkuCE3h8hiMj7JVmNXtn4wHZUEVe\n\
        brt5adjvAgMBAAECggEASSUoGBdo4JlaZfNlKyzrUc58ze6dLgo6nD7PJC2svBco\n\
        rVHwMCeuVyyoMR6zpkEb1C/ias1HnSgcoYjPVXTnxP0vuMZv+HKqRD3vu9nkGROT\n\
        1cFM1ZMfpFXoyC+7lP8nlp33X/f4SsAQ+OKZCysLWJUSA74s7xafwo27yOoIZ2br\n\
        jRG3chL0c1wKKwwEazqB6OoOtFJsNJQduqrrIe93uGxvB4cNic0r0SmeeDAV+x9F\n\
        uIRSYCOd48/AFUKHvPONCsk3d7gokQQlECXTfYPzQnGWpLKZFKHZpx97n9uBB1xq\n\
        M8umsI4guHHvsw+7aMo5XRnGCYcgBuUILt+f6IR9qQKBgQDekUfExFyagC84Otw/\n\
        p7iKph6apD3I3aguggh0c75GJEQfdwcHAsG0EGcQ40mA83tI7yAdnje3r1oGWYvQ\n\
        B7TFvUD96agi6iVw2YP8enKcv5P6s1tMiVusJZ1+Erss4fweiU3CdYEJxr1lT+bN\n\
        d/sup4QcRdvwB+K/KoQsqQYWCQKBgQDE+xuRiPQSfd+DTPxC23M0ZUQfCfd1oXBj\n\
        ErzHAWFHNr2AmhbG5eG5NFc/ZjS+E3j/rf/oSbHvRFcWLHNqwPwwNe4K2bpRgoG8\n\
        d+SCFVL4WAAUUyoHqSKgEwWwiLPnLOPFxMzxpdHRUCAHB2elrEHc/F6oQQsl5tZT\n\
        8Z8NswF1NwKBgQCoWJVDdQ9iYaDV1Fex4kgIv3wYljCRlW9Xtk103/NFFLteXWa4\n\
        W9JuQ5XhxTlcy04uLYlKPdmBG6ZpdSP2t0154BufszPVLOwi/rQBrhCxFYt1rmJZ\n\
        cvAfNth4euyPF2PfKRXjB4QSA/xP/G7kbWNVt6mKxSCF30EYPa16JmMPGQKBgE3c\n\
        eBQ5z0gz6xKqbpg6yDnwwVGHjgI4MwVmVapP+FS+5X8c6V3rZiLf9pC/5J8xcUWX\n\
        UK1P2/6Yw9em8GRFLiocVyCA2a34aTHHLlLg/O/fUQC3ssheaapeeoRCNOJvEwWM\n\
        efoWsm4LPu2oVqbdBRfFxeC5/R8ZNKTvwBAZLr1XAoGAfBHr/iZQ4yGPpT1GvW/e\n\
        VIKg3GVtW5Fp++wNev61rj6g7mRP/9U+lnPbBaBVlcd49GhVLz01o4nH6wux7T+I\n\
        3XmyiA+2DUWbwPzg+fmi2c6t7xTjQMdiNToRzpBLzxBaczkIyxbClsNGBckBR4OU\n\
        FyNrgpIOCf/+2vv1u0nm6kY=\n-----END PRIVATE KEY-----\n";

    //Create JWT object
    jwt::jwt_object token{ 
        jwt::params::algorithm("RS256"), 
        jwt::params::payload({{"iss", "omnigen@omnigen.iam.gserviceaccount.com"}}), 
        jwt::params::secret(key) };

    token.add_claim("scope", "https://www.googleapis.com/auth/earthengine.readonly");
    token.add_claim("aud", "https://oauth2.googleapis.com/token");
    token.add_claim("exp", expTime);
    token.add_claim("iat", secondsSinceEpoch);

    signedToken = QString::fromStdString(token.signature());

    return true;
}

void OmniSatScan::makePostRequestDEM(const QGeoCoordinate& tl, const QGeoCoordinate& br, bool marginRequest)
{
    QUrl requestUrlHeader("https://earthengine.googleapis.com/v1alpha/projects/earthengine-public/assets/CGIAR/SRTM90_V4:getPixels");

    QNetworkRequest request(requestUrlHeader);
    request.setSslConfiguration(QSslConfiguration::defaultConfiguration());

    // Access Token header
    QString headerData = "Bearer " + accessToken;
    QUrl headerUrl = "Bearer " + accessToken;
    request.setRawHeader(QByteArray("Authorization"), headerData.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Visualization JSON
    QJsonObject visOptions;

    // Visualization JSON - Height Ranges [m] - This range of values will be mapped to 0-255 (black to white) in the resulting image
    QJsonObject doubleRange;
    int maxHeight = 5000;
    doubleRange.insert("min", QJsonValue::fromVariant(0));
    doubleRange.insert("max", QJsonValue::fromVariant(maxHeight));
    fRangeToHeightFactor = static_cast<float>(maxHeight) / 255.0f;

    QJsonArray rangesArray;
    rangesArray.push_back(doubleRange);
    visOptions.insert("ranges", rangesArray);

    // Coordinates(decimal) order: longitude, latitude, altitude(optional) 
    QJsonArray firstCoord;
    firstCoord.push_back(tl.longitude());
    firstCoord.push_back(tl.latitude());
    QJsonArray secondCoord;
    secondCoord.push_back(br.longitude());
    secondCoord.push_back(br.latitude());

    QJsonArray coordinates;
    coordinates.push_back(firstCoord);
    coordinates.push_back(secondCoord);

    // GeoJSON Geometry Object (RFC 7946)
    QJsonObject geometry;
    geometry.insert("type", "MultiPoint");
    geometry.insert("coordinates", coordinates);

    QJsonObject urlBody;
    urlBody.insert("fileFormat", "GEO_TIFF");
    urlBody.insert("region", geometry);
    urlBody.insert("bandIds", "elevation");
    urlBody.insert("visualizationOptions", visOptions);

    postReply = manager->post(request, QJsonDocument(urlBody).toJson(QJsonDocument::JsonFormat::Compact));

    if(!marginRequest)
        connect(postReply, &QNetworkReply::finished, this, &OmniSatScan::getDEMData);
    else
        connect(postReply, &QNetworkReply::finished, this, &OmniSatScan::getDEMmarginData);
}

void OmniSatScan::makeGetRequestBiome(const QGeoCoordinate& tl, const QGeoCoordinate& br)
{
    QUrl requestUrlHeader("https://earthengine.googleapis.com/v1alpha/projects/earthengine-public/assets/RESOLVE/ECOREGIONS/2017:listFeatures");

    // Region to get data from
    QJsonArray firstCoord;
    firstCoord.push_back(tl.longitude());
    firstCoord.push_back(tl.latitude());
    QJsonArray secondCoord;
    secondCoord.push_back(br.longitude());
    secondCoord.push_back(br.latitude());

    QJsonArray coordinates;
    coordinates.push_back(firstCoord);
    coordinates.push_back(secondCoord);

    // GeoJSON Geometry Object (RFC 7946)
    QJsonObject geometry;
    geometry.insert("type", "MultiPoint");
    geometry.insert("coordinates", coordinates);

    QUrlQuery query;
    query.addQueryItem("region", QJsonDocument(geometry).toJson(QJsonDocument::JsonFormat::Compact));
    requestUrlHeader.setQuery(query);

    QNetworkRequest request(requestUrlHeader);
    request.setSslConfiguration(QSslConfiguration::defaultConfiguration());

    // Access Token header
    QString headerData = "Bearer " + accessToken;
    QUrl headerUrl = "Bearer " + accessToken;
    request.setRawHeader(QByteArray("Authorization"), headerData.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    postReply = manager->get(request);

    connect(postReply, &QNetworkReply::finished, this, &OmniSatScan::getBiomeData);
}

void OmniSatScan::areaFragmentation(const QGeoCoordinate& tl, const QGeoCoordinate& br)
{
    // Maximum request fragment dimension (in any direction)
    // For SRTM Digital Elevation Data (CGIAR/SRTM90_V4) = 8 degrees
    double fragDim = 8;

    auto tllo = tl.longitude();
    auto tlla = tl.latitude();
    auto brlo = br.longitude();
    auto brla = br.latitude();
    auto areaH = (tllo > brlo) ? (tllo - brlo) : (brlo - tllo);
    auto areaW = (tlla > brla) ? (tlla - brla) : (brla - tlla);

    int fragCountX = 1;
    int fragCountY = 1;

    // If requested area is larger than fragDim, divide it into equal pieces (for ease of merging)
    while (true)
    {
        if ((areaH / fragCountX) < fragDim)
            break;

        fragCountX++;
    }

    while (true)
    {
        if ((areaW / fragCountY) < fragDim)
            break;

        fragCountY++;
    }

    // Fixed chunk dimensions 
    auto fragX = areaH / fragCountX;
    auto fragY = areaW / fragCountY;

    // Calculate and insert all necessary coords
    for (int x = 0; x <= fragCountX; ++x)
        for (int y = 0; y <= fragCountY; ++y)
        {
            if (fragmentationMap.value({ x, y }, QGeoCoordinate()) == QGeoCoordinate())
            {
                fragmentationMap.insert({ x , y }, QGeoCoordinate(
                    tlla > brla ? (tlla - (y * fragY)) : (tlla + (y * fragY)),
                    tllo > brlo ? (tllo - (x * fragX)) : (tllo + (x * fragX))));
            }
        }

    fragmentedPost(0, 0);
}

bool OmniSatScan::fragmentedPost(int x, int y)
{
    // Valid area is constructed from coords at positions (n, m) - (n+1, m+1)
    if (fragmentationMap.value({ x + 1, y + 1 }, QGeoCoordinate()) != QGeoCoordinate())
        {
            makePostRequestDEM(fragmentationMap.value({ x, y }), fragmentationMap.value({ x + 1, y + 1 }));
            fragmentPos = { x, y };

            // Save final size for merging
            if (finalImageSize.first < (x + 1))
                finalImageSize.first = (x + 1);

            if(finalImageSize.second < (y + 1))
                finalImageSize.second = (y + 1);

            return true;
        }

    return false;
}

void OmniSatScan::mergeBufferedImages()
{
    ui.mapLabel->clear();

    // Skip process if only 1 image received
    if (bufferedImages.size() == 1)
    {
        finalize(bufferedImages.first());
        return;
    }

    QPainter picasso;

    int iw = bufferedImages.first().width();
    int ih = bufferedImages.first().height();

    QPixmap finalImage(QSize(iw * finalImageSize.first, ih * finalImageSize.second));
    finalImage.fill(Qt::transparent);

    picasso.begin(&finalImage);

    for (auto&& imageFragment : bufferedImages)
    {
        auto iPos = bufferedImages.key(imageFragment);
        picasso.drawPixmap((iPos.first * iw), (iPos.second * ih), iw, ih, imageFragment);
    }

    finalize(finalImage);
}

void OmniSatScan::finalize(const QPixmap& finalImage)
{
    ui.mapLabel->setPixmap(finalImage);
    bufferedImages.clear();
}

void OmniSatScan::clearData()
{
    fragmentationMap.clear();
    fragmentPos = { 0, 0 };
}
