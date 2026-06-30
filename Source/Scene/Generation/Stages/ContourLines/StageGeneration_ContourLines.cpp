#include "stdafx.h"
#include "StageGeneration_ContourLines.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "ContourLines.h"
#include "IsohypseBatchingMarker.h"
#include "Editor/StageTools/StageTools.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdal_alg.h>

namespace Generation
{
    void StageGen<EGenerationStage::ContourLines>::initialize()
    {
        TODO("Init has to spawn IH only for ridges that do not have them");
        auto&& marker = gBatchingMarkerInstance<IsohypseBatchParams>;
        if (!marker || !marker->hasSections())
            ContourLines::prepareStack();
        else
            ContourLines::regenerateStack();
    }

    // Generate contour lines based off Ridges. This defines the general shape of the world.
    bool StageGen<EGenerationStage::ContourLines>::autoGen()
    {
        Q_ASSERT(!ContourLines::ihStack.isEmpty());
        return ContourLines::generate();
    }

    void StageGen<EGenerationStage::ContourLines>::clear()
    {
        clearAllBatches<IsohypseBatchParams>();
    }

    void StageGen<EGenerationStage::ContourLines>::createIsohypsesOutOfDEM(const std::vector<float>& bufferDEM, int demMargin, GPoint offset, int rows, int cols, float xSpacing, float zSpacing, bool satScan)
    {
        float ihHeightDifference = 10.0;

        Q_ASSERT(rows * cols == bufferDEM.size());

        auto gdalBufferDem = bufferDEM;
        float maxHeight = 0.0;
        float minHeight = 256.0;
        for (auto&& point : bufferDEM)
        {
            if (point > maxHeight)
                maxHeight = point;

            if (point < minHeight)
                minHeight = point;
        }

        // GDAL DEM only accepts values from 0 - 255, where 0 is considered as no data
        float heightFactor = maxHeight / 255.0;

        for (int i = 0; i < bufferDEM.size(); ++i)
        {
            auto&& gdalHeight = bufferDEM[i] / heightFactor;
            // To achieve full area coverege by isohypses a margin is added that is artificially lowered to the minimum value drawn - ihHeightDifference
            gdalBufferDem[i] = gdalHeight < ihHeightDifference + 1.0 ? ihHeightDifference + 1.0 : gdalHeight;
        }

        float lowestHeight = 255.0f;
        for (int i = 0; i < gdalBufferDem.size(); i++)
        {
            if (i < cols * 3
                || i / cols > rows - 4
                || i % cols == 0 || i % cols == 1 || i % cols == 2
                || i % cols == cols - 1 || i % cols == cols - 2 || i % cols == cols - 3)
            {
                if (lowestHeight > gdalBufferDem[i])
                    lowestHeight = gdalBufferDem[i];
            }
        }

        // Anything under 1.0 is considered no data for GDAL
        lowestHeight = lowestHeight < 2.0 ? 1.0 : lowestHeight - 5.0;

        // Lower the whole DEM edge the the lowest point, forcing isohypse closure
        for (int i = 0; i < gdalBufferDem.size(); i++)
        {
            if (i < cols * 3
                || i / cols > rows - 4
                || i % cols == 0 || i % cols == 1 || i % cols == 2
                || i % cols == cols - 1 || i % cols == cols - 2 || i % cols == cols - 3)
            {
                gdalBufferDem[i] = lowestHeight;
            }
        }

        QString filePath = QString("Output/Earth Engine/");

        QDir directory;
        if (!directory.exists(filePath))
            directory.mkpath(filePath);

        GDALAllRegister();
        std::string demFromOmnigen = filePath.toStdString() + "DEMFromOmnigen.tif";
        std::string omniDEMLayer = filePath.toStdString() + "OmniDEMLayer.shp";

        auto&& driverTiff = GetGDALDriverManager()->GetDriverByName("GTiff");
        auto&& shapefileDriver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");

        // Dataset
        auto&& dataset = driverTiff->Create(demFromOmnigen.c_str(), cols, rows, 1, GDT_Float32, nullptr);
        double bufferForGeoTr[6] = { 0.0, 0.0001, 0.0, 0.0, 0.0, 0.0001 };
        dataset->SetGeoTransform(bufferForGeoTr);
        dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, cols, rows, gdalBufferDem.data(), cols, rows, GDT_Float32, 0, 0);

        // Layer data
        auto&& layerDataset = shapefileDriver->Create(omniDEMLayer.c_str(), cols, rows, 1, GDT_Unknown, nullptr);
        GDALDatasetH hLayerDataset = static_cast<GDALDatasetH>(layerDataset);
        OGRLayerH ihLayer = GDALDatasetCreateLayer(hLayerDataset, "ihLayer", nullptr, OGRwkbGeometryType::wkbUnknown, nullptr);

        // Additional layer field so that `GDALContourGenerate` can add height information to each isohypse
        auto fieldDefinition = OGRFieldDefn("Elevation", OFTReal);
        auto hFieldDefinition = fieldDefinition.ToHandle(&fieldDefinition);
        int elevationFieldIdx = 0;
        OGR_L_CreateField(ihLayer, hFieldDefinition, elevationFieldIdx);

        // DEM Raster band on which contour computations will be based
        auto&& hDataset = static_cast<GDALDatasetH>(dataset);
        GDALRasterBandH ihDatasetRasterBand = GDALGetRasterBand(hDataset, 1);

        // Change the first float to increase/decrease the minimum height difference to create an IH
        GDALContourGenerate(ihDatasetRasterBand, ihHeightDifference, 0.0, 0, nullptr, 0, -9999.0, ihLayer, -1, 0, nullptr, nullptr);

        GPoint topLeft{ static_cast<int>((offset.x) * xSpacing), static_cast<int>((offset.z) * zSpacing) };

        float fOffsetX = topLeft.x;
        float fOffsetZ = topLeft.z;

        // For reasons known only to GDAL, the coordinates are misaligned by 0.5, adding this offset fixes that. Ohh and GDAL DEM (copy from world) requires the misalignment to be reversed
        float gdalsMagicOffset = satScan ? (-0.5) : 0.5;
        float markerOffset = gdalsMagicOffset + demMargin;
        std::unordered_multimap<float, std::vector<QVector3D>> allIsohypses;

        auto&& featureCount = OGR_L_GetFeatureCount(ihLayer, 1);
        for (int i = 0; i < featureCount; ++i)
        {
            auto&& feature = OGR_L_GetNextFeature(ihLayer);
            auto&& height = OGR_F_GetFieldAsDouble(feature, elevationFieldIdx);
            auto&& isohypseGeometry = OGR_F_GetGeometryRef(feature);
            auto&& isohypseCoordinates = OGR_G_ExportToJson(isohypseGeometry);

            QString jsonString(isohypseCoordinates);
            QJsonDocument jsonDoc = QJsonDocument::fromJson(isohypseCoordinates);
            QJsonObject jsonObj = jsonDoc.object();
            auto&& coorindateArray = jsonObj["coordinates"].toArray();

            std::vector<QVector3D> isohypsePoints;

            // GDAL's IHs have the first and last point identical (if they are fully closed, which is guaranteed by the margin and artificial dem lowering)
            for (int j = 0; j < coorindateArray.size(); ++j)
            {
                auto&& coordinatesPair = coorindateArray[j].toArray();
                auto&& longitude = coordinatesPair[0].toDouble();
                auto&& latitude = coordinatesPair[1].toDouble();

                auto x = static_cast<float>(((longitude - bufferForGeoTr[0]) / bufferForGeoTr[1] - markerOffset) * xSpacing) + fOffsetX;
                auto z = static_cast<float>(((latitude - bufferForGeoTr[3]) / bufferForGeoTr[5] - markerOffset) * zSpacing) + fOffsetZ;
                QVector3D point = { x, 0.0, z };

                // Skip duplicate points
                if (j > 0 && (point == isohypsePoints.back() || point == isohypsePoints.front()))
                    continue;

                isohypsePoints.emplace_back(point);
            }

            if (isohypsePoints.size() <= 1)
                continue;

            // GDAL isohypses are counterclockwise, Omnigen's are clockwise (important for river gen)
            std::ranges::reverse(isohypsePoints);

            allIsohypses.emplace(height, isohypsePoints);
        }

        GDALClose(dataset);
        GDALClose(layerDataset);
        GDALDestroyDriverManager();

        sortIsohypses(allIsohypses, heightFactor);
    }

    void StageGen<EGenerationStage::ContourLines>::sortIsohypses(const std::unordered_multimap<float, std::vector<QVector3D>>& allIsohypses, float heightScale)
    {
        std::set<float> heightMap;
        for (auto&& ih : allIsohypses)
            heightMap.emplace(ih.first);

        auto&& maxHeight = *heightMap.rbegin();
        auto&& highestIsohypses = allIsohypses.equal_range(maxHeight);
        auto&& generationData = Generation::Data::get();

        std::vector<QSharedPointer<Isohypse>> lastIsohypsesOfEachMountain;

        // The highest possible isohypses 
        for (auto&& it = highestIsohypses.first; it != highestIsohypses.second; ++it)
        {
            IHProtoData ihData;
            ihData.pts = (*it).second;
            ihData.height = maxHeight * heightScale;
            ihData.usedDomainId = Generation::Data::get()->getDomainAtSquare(((GVector2D)ihData.pts.front()).toGPoint(), EDomainType::Terrain)->getGuid();
            auto&& ih0Marker = spawnBatchedIH(ihData, 0);
            //generationData->createMarker<DLineMarker, true>((*it).second, QVector4D(0, 1, 1, 1), true, (maxHeight * fHeightMultiplier * fHeightAuxiliaryMultiplier) + 100);
            lastIsohypsesOfEachMountain.emplace_back(ih0Marker);
        }

        // For all isohypses of every saved height find source ih, or assign it as level 0 ih
        for (auto&& heightIterator = heightMap.rbegin(); heightIterator != heightMap.rend(); ++heightIterator)
        {
            // Highest ihs are already added as lvl 0 
            if (heightMap.rbegin() == heightIterator)
                continue;

            auto&& isohypsesOfHeight = allIsohypses.equal_range(*heightIterator);

            for (auto isohypseIterator = isohypsesOfHeight.first; isohypseIterator != isohypsesOfHeight.second; ++isohypseIterator)
            {
                // ///////////////////////////// TEMPORARY pan isohypse discarder (untill river gen can deal with them)
                bool temporaryPanCheck = false;
                auto&& firstPoint = isohypseIterator->second.front();
                for (auto isohypseIterator2 = isohypsesOfHeight.first; isohypseIterator2 != isohypsesOfHeight.second; ++isohypseIterator2)
                {
                    if (isohypseIterator->second == isohypseIterator2->second)
                        continue;

                    GVector2D point(firstPoint.x(), firstPoint.z());

                    if (std::get<0>(point.isInsidePolygon(isohypseIterator2->second)))
                    {
                        temporaryPanCheck = true;
                        break;
                    }
                }

                if (temporaryPanCheck)
                    continue;
                // /////////////////////////////

                std::set<Isohypse*> sourceIhsHit;

                // Find source ihs for this isohypse (and delete them from lastIsohypsesOfEachMountain, as this ih will replace them)
                for (int i = 0; i < lastIsohypsesOfEachMountain.size(); ++i)
                {
                    GVector2D sourcePoint = lastIsohypsesOfEachMountain[i]->getCircularPoints()[0];
                    if (std::get<0>(sourcePoint.isInsidePolygon(isohypseIterator->second)))
                    {
                        sourceIhsHit.emplace(lastIsohypsesOfEachMountain[i].get());
                        lastIsohypsesOfEachMountain.erase(lastIsohypsesOfEachMountain.begin() + i--);
                    }
                }

                // If no source isohypses were found, create a level 0 ih out of the current one
                if (sourceIhsHit.empty())
                {
                    IHProtoData ihData;
                    ihData.pts = isohypseIterator->second;
                    ihData.height = (*heightIterator) * heightScale;
                    auto&& newSourceMarker = spawnBatchedIH(ihData, 0);
                    //generationData->createMarker<DLineMarker, true>(isohypseIterator->second, QVector4D(0, 1, 1, 1), true, ((*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier) + 100);
                    lastIsohypsesOfEachMountain.emplace_back(newSourceMarker);
                    continue;
                }

                std::vector<IHSrcInfo> sourceInfo;
                auto&& isohypsePoints = isohypseIterator->second;

                // Find the closest source point for each current Ih point
                for (int i = 0; i < isohypsePoints.size(); ++i)
                {
                    Isohypse* sourceIsohypse;
                    int pointIdx = -1;
                    float distance = GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH;

                    for (auto&& sourceIh : sourceIhsHit)
                    {
                        auto&& sourceControlPoints = sourceIh->getCircularPoints();
                        for (int j = 0; j < sourceControlPoints.getSize(); ++j)
                        {
                            if (auto&& dist = isohypsePoints[i].distanceToPoint(sourceControlPoints[j]); dist < distance)
                            {
                                sourceIsohypse = sourceIh;
                                pointIdx = j;
                                distance = dist;
                            }
                        }
                    }

                    //                 auto&& sourceControlPoints = sourceIsohypse->getControlPoints();
                    //                 QVector3D debugPoint = { sourceControlPoints[pointIdx].x(), (*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier, sourceControlPoints[pointIdx].z() };
                    //                 QVector3D debug2Point = { isohypsePoints[i].x(), (*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier, isohypsePoints[i].z() };
                    //                 generationData->createMarker<DLineMarker, true>(std::vector({ debugPoint, debug2Point }), QVector4D(1, 0, 1, 1));

                    IHSrcInfo sourcePointInfo;
                    sourcePointInfo.ih = sourceIsohypse;
                    sourcePointInfo.idx = pointIdx;
                    sourcePointInfo.ihGuid = sourceIsohypse->getGuid();
                    sourceInfo.emplace_back(sourcePointInfo);
                }

                IHProtoData ihData;
                ihData.pts = (*isohypseIterator).second;
                ihData.height = (*heightIterator) * heightScale;
                ihData.parentIhs = sourceIhsHit;
                ihData.sources = sourceInfo;

                int highestSourceLevel = -1;
                for (auto&& sourceIh : sourceIhsHit)
                    if (sourceIh->getLevel() > highestSourceLevel)
                    {
                        highestSourceLevel = sourceIh->getLevel();
                        ihData.usedDomainId = sourceIh->data.usedDomainId;
                    }

                auto&& newSourceMarker = spawnBatchedIH(ihData, ++highestSourceLevel);

                // Set descendant info in source isohypses
                for (auto&& sourceIh : sourceIhsHit)
                {
                    IHSrcInfo descendantInfo;
                    descendantInfo.ih = newSourceMarker.get();
                    descendantInfo.ihGuid = newSourceMarker->getGuid();
                    auto&& sourceCpts = sourceIh->getCircularPoints();
                    auto&& newMarkerCpts = newSourceMarker->getCircularPoints();

                    // For each source point find the closest descendant point
                    for (int i = 0; i < sourceCpts.getSize(); ++i)
                    {
                        std::multimap<float, int> distanceMap;
                        std::map<float, int> angleMap;
                        float minimumDistance = GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH;

                        for (int j = 0; j < newMarkerCpts.getSize(); ++j)
                            distanceMap.emplace(sourceCpts[i].distanceToPoint(newMarkerCpts[j]), j);

                        for (auto&& it = distanceMap.begin(); it != distanceMap.end() && it->first < 1.5 * distanceMap.begin()->first; ++it)
                        {
                            auto angleDeviation = descendantAngleDeviationFromNominal(sourceCpts, newMarkerCpts[it->second], i);
                            angleMap.emplace(angleDeviation, it->second);
                        }

                        //                     QVector3D debugPoint = { newMarkerCpts[angleMap.begin()->second].x(), (*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier, newMarkerCpts[angleMap.begin()->second].z() };
                        //                     QVector3D debug2Point = { sourceCpts[i].x(), (*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier, sourceCpts[i].z() };
                        //                     generationData->createMarker<DLineMarker, true>(std::vector({debugPoint, debug2Point}), QVector4D(1, 0, 1, 1));

                        descendantInfo.idx = angleMap.begin()->second;
                        sourceIh->setDescendant(i, descendantInfo);
                    }

                    // Set descendant info for "swallowed" isohypses
                    auto&& descendants = sourceIh->getDescendants();
                    bool hasDescendants = false;

                    for (auto&& desc : descendants)
                    {
                        if (desc)
                        {
                            hasDescendants = true;
                            break;
                        }
                    }

                    if (!hasDescendants)
                    {
                        for (int i = 0; i < sourceCpts.getSize(); ++i)
                        {
                            int descendantIdx = -1;
                            float distance = GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH;
                            for (int j = 0; j < newMarkerCpts.getSize(); ++j)
                            {
                                if (auto&& dist = sourceCpts[i].distanceToPoint(newMarkerCpts[j]); dist < distance)
                                {
                                    descendantIdx = j;
                                    distance = dist;
                                }
                            }

                            descendantInfo.idx = descendantIdx;
                            sourceIh->setDescendant(i, descendantInfo);

                            //                         QVector3D debugPoint = { newMarkerCpts[descendantIdx].x(), (*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier, newMarkerCpts[descendantIdx].z() };
                            //                         QVector3D debug2Point = { sourceCpts[i].x(), (*heightIterator) * fHeightMultiplier * fHeightAuxiliaryMultiplier, sourceCpts[i].z() };
                            //                         generationData->createMarker<DLineMarker, true>(std::vector({ debugPoint, debug2Point }), QVector4D(0, 1, 1, 1));
                        }
                    }
                }

                lastIsohypsesOfEachMountain.emplace_back(newSourceMarker);
            }
        }

        ContourLines::computePreflow();
    }

    float StageGen<EGenerationStage::ContourLines>::descendantAngleDeviationFromNominal(const CircularVectorView<std::vector, QVector3D>& sourcePoints, const QVector3D& descendantPoint, int idx)
    {
        static const QQuaternion rotateLeft90 = QQuaternion::fromEulerAngles(0, 270.0f, 0);
        QVector3D prevPoint = sourcePoints.getPrev(idx);
        QVector3D nextPoint = sourcePoints.getNext(idx);
        QVector3D nextToPrev = (prevPoint - nextPoint).normalized();
        auto perfectDirection = rotateLeft90.rotatedVector(nextToPrev);
        QVector3D descendantToSource = (sourcePoints[idx] - descendantPoint).normalized();

        return angle180(perfectDirection, descendantToSource);
    }
}