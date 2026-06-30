#include "stdafx.h"
#include "StageToolsTerrainFinalization.h"

#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/StageTools/StageTools.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "TerrainFinalizationSelection.h"
#include "Editor/Sections/CameraSystem/TestPlayer.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"

#include "Data/Assets/Plant/AssetPlant.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"
#include "Data/Assets/SoilMaterial/AssetSoilMaterial.h"
#include <gli/save_dds.hpp>
#include <tbb/task_group.h>

#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Source/Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Utils/PlatformMisc.h"
#include "Scene/Generation/Stages/Foliage/StageGeneration_Foliage.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Utils/TriangleAdjacencyUtils.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverSurfaceMarker.h"
#include "Scene/Generation/Stages/TerrainMods/Lake/LakeSurfaceMarker.h"

#include <nvtt/nvtt.h>
#include <DirectXMath.h>

namespace Design
{
    StageTools<EGenerationStage::TerrainFinalization>::StageTools()
        : StageToolsBase()
    {
    }

    SelectionMgrBase* StageTools<EGenerationStage::TerrainFinalization>::getSelectionMgr() const
    {
        return PIESelectionMgr::get();
    }

    void StageTools<EGenerationStage::TerrainFinalization>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* outline = omnigen->getOutline();
        auto* toolbar = createOutlineToolbar();

        auto treeView = new OutlineTreeView;

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, treeView });

        toolbar->show();

        bPlayTool = false;
        toolMode = EToolMode::None;

        connect(&ticker, &QTimer::timeout, this, &StageTools<EGenerationStage::TerrainFinalization>::editVertices);
        ticker.setInterval(100);

        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void StageTools<EGenerationStage::TerrainFinalization>::unbind()
    {
        StageToolsBase::unbind();

        if (brushGizmo)
            clearBrush();

        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }
    }

    void StageTools<EGenerationStage::TerrainFinalization>::save(OmniBin<std::ios::out>& writer) const
    {

    }

    void StageTools<EGenerationStage::TerrainFinalization>::load(OmniBin<std::ios::in>& reader)
    {
        DTerrainChunk::generateTerrainResources();
        
        auto&& [terrainChunks, chunkBlocksMap, blockChunkMap] = Generation::Data::get()->getTerrainChunkData();
        for (auto&& chunk : terrainChunks)
        {
            chunk->initialize();
            chunk->setActiveLOD(ELOD::Far);
            emit Editable::created(chunk);
        }
    }

    QWidget* StageTools<EGenerationStage::TerrainFinalization>::createOutlineToolbar()
    {
        auto* mainWidget = new QWidget();

        mainWidget->setContentsMargins(0, 0, 0, 0);
        mainWidget->setMaximumWidth(5000);

        auto* mainLayout = new QGridLayout(mainWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        auto* toolBar = new QToolBar();
        mainLayout->addWidget(toolBar, 0, 0, 1, -1);

        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        auto* toolsGroup = new QActionGroup(toolBar);
        toolsGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        auto spawnPlayerAction = new QAction(QIcon("Resources/Icons/BiomeIcon.png"), "Play from Here", toolsGroup);
        spawnPlayerAction->setCheckable(true);
        connect(spawnPlayerAction, &QAction::triggered, this, [this]()
            {
                playFromSelection();
            });

        auto calcWetnessAction = new QAction(QIcon("Resources/Icons/BiomeIcon.png"), "Calc TWI", toolsGroup);
        connect(calcWetnessAction, &QAction::triggered, this, [this]()
        {
            calcWetness();
        });

        auto spawnSculptAction = new QAction(QIcon("Resources/Icons/BiomeIcon.png"), "Sculpt Terrain", toolsGroup);
        spawnSculptAction->setCheckable(true);

        auto spawnTextureAction = new QAction(QIcon("Resources/Icons/BiomeIcon.png"), "Erosion Texture", toolsGroup);
        spawnTextureAction->setCheckable(true);

        auto exportAction = new QAction(QIcon("Resources/Icons/BiomeIcon.png"), "Export to UE5", toolsGroup);
        connect(exportAction, &QAction::triggered, this, [this]()
            {
                exportUE5();
            });

        toolBar->addAction(calcWetnessAction);
        toolBar->addAction(spawnPlayerAction);
        toolBar->addAction(spawnSculptAction);
        toolBar->addAction(spawnTextureAction);
        toolBar->addAction(exportAction);

        // Tools Brush Size Slider
        auto* brushSizeSlider = new QSlider(Qt::Orientation::Horizontal);
        brushSizeSlider->setMaximum(10);
        brushSizeSlider->setMinimum(1);
        brushSizeSlider->setValue(brushSize);
        auto* brushSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));
        brushSizeSlider->setVisible(true);
        brushSizeLabel->setVisible(false);

        // Tools Brush Strength Slider
        auto* brushStrengthSlider = new QSlider(Qt::Orientation::Horizontal);
        brushStrengthSlider->setMaximum(10);
        brushStrengthSlider->setMinimum(1);
        brushStrengthSlider->setValue(brushSize);
        auto* brushStrengthLabel = new QLabel("Brush Strength: " + QString::number(brushStrength * 20) + "%");
        brushStrengthSlider->setVisible(true);
        brushStrengthLabel->setVisible(false);

        QGroupBox    *sculptOperationsGroup = new QGroupBox(tr("Sculpt Types:"));
        QHBoxLayout  *layoutSculpt = new QHBoxLayout;
        QRadioButton *elevateGeometryRadio  = new QRadioButton(tr("Elevate Geometry"));
        layoutSculpt->addWidget(elevateGeometryRadio);
        sculptOperationsGroup->setLayout(layoutSculpt);
        sculptOperationsGroup->setVisible(false);
        elevateGeometryRadio->setChecked(true);

        mainLayout->addWidget(brushSizeLabel       , 1, 0);
        mainLayout->addWidget(brushSizeSlider      , 1, 1);
        mainLayout->addWidget(brushStrengthLabel   , 2, 0);
        mainLayout->addWidget(brushStrengthSlider  , 2, 1);
        mainLayout->addWidget(sculptOperationsGroup, 3, 0, 1, 2);

        connect(brushSizeSlider, &QSlider::valueChanged, this, [this, brushSizeSlider, brushSizeLabel]
        {
            brushSize = brushSizeSlider->value();
            brushSizeLabel->setText("Brush Size: " + QString::number(brushSize));
        });

        connect(brushStrengthSlider, &QSlider::valueChanged, this, [this, brushStrengthSlider, brushStrengthLabel]
        {
            brushStrength = brushStrengthSlider->value();
            brushStrengthLabel->setText("Brush Strength: " + QString::number(brushStrength * 20) + "%");
            });
        
        // Button logic
        connect(toolsGroup, &QActionGroup::triggered, this, [=, this]()
        {
            bPlayTool = spawnPlayerAction->isChecked();

            if (spawnSculptAction->isChecked())
                toolMode = EToolMode::Sculpt;

            if (spawnTextureAction->isChecked())
                toolMode = EToolMode::TexErode;

            sculptOperationsGroup->setVisible(toolMode == EToolMode::Sculpt);
                
            clearBrush();
        });

        mainLayout->setColumnMinimumWidth(2, 4700);

        return mainWidget;
    }

    
    void StageTools<EGenerationStage::TerrainFinalization>::playFromSelection()
    {
        auto&& selection = PIESelectionMgr::get()->getSelection<EPIESelection::SpawnSelector>();
        if (selection.empty())
        {
            OmniLog(ELoggingLevel::Warn) <<= "No spawn point chosen for PIE!";
            QMessageBox(QMessageBox::Icon::Critical,
                QString::fromStdString("Error"),
                QString::fromStdString("You need to choose a spawn point."), QMessageBox::StandardButton::Ok).exec();

            return;
        }

        QVector3D newPos = QVector3D(selection.begin()->x(), selection.begin()->y() + TestPlayer::getPlayerHeight(), selection.begin()->z());
        QOmnigenViewportSection::getActiveViewport()->startPIE(newPos);
    }

    void StageTools<EGenerationStage::TerrainFinalization>::editVertices()
    {
        if (!brushOrigin)
            return;

        switch (toolMode)
        {
        case EToolMode::Sculpt: return sculpt();
        case EToolMode::TexErode: return erodeTexture();
        }
    }

    void StageTools<EGenerationStage::TerrainFinalization>::updateBrush()
    {
        auto triangle = findChunkTriangleUnderCursor();
        if (!triangle)
        {
            brushOrigin = {};
            clearBrush();
            return;
        }

        // All operations rely on brushOrigin to be set here
        brushOrigin = triangle;

        std::vector<QVector3D> circlePoints(25);
        TODO("make circle on chunks");

        if (!brushGizmo)
        {
            brushGizmo = spawn<DLineMarker, true>(circlePoints, Colors::yellow, true);
        }
        else
        {
            brushGizmo->movePoints(circlePoints);
        }
    }

    bool StageTools<EGenerationStage::TerrainFinalization>::eventFilter(QObject* obj, QEvent* event)
    {
        if (toolMode == EToolMode::None)
            return false;

        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            // Constantly edit DEM while holding LMB
            if (mEvent->button() == Qt::LeftButton)
            {
                vertexChanges.clear();
                ticker.start();
            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            updateBrush();
        }
        else if (ticker.isActive() && mEvent->type() == QEvent::MouseButtonRelease)
        {
            ticker.stop();
            finalizeChanges(toolMode, vertexChanges);
        }

        return false;
    }

    void StageTools<EGenerationStage::TerrainFinalization>::sculpt()
    {
        auto&& pointsToEdit = findGeometryUnderBrush();
        float operationValue = brushStrength * 20;
      
        std::unordered_set<int> changedClusterIds;
        for (auto&& point : pointsToEdit)
        {
            TODO("implement sculpt on chunks")
        }

        updateGeometry(toolMode, changedClusterIds);
    }

    void StageTools<EGenerationStage::TerrainFinalization>::erodeTexture()
    {
        auto&& pointsToEdit = findGeometryUnderBrush();

        std::unordered_set<int> changedClusterIds;
        for (auto&& point : pointsToEdit)
        {
            TODO("implement tex change on chunks")
        }

        updateGeometry(toolMode, changedClusterIds);
    }

    void StageTools<EGenerationStage::TerrainFinalization>::exportUE5()
    {
        using namespace Generation;
        OmniLog(ELoggingLevel::Info) <<= "Exporting to Unreal...";

        QString filename = QFileDialog::getSaveFileName(Omnigen::get(), "Omnigen Export Format", "Output/", "OEF (*.OEF)");
        if (filename.isEmpty())
            return;

        QDir outputDir = QFileInfo(filename).absoluteDir();
        std::string outputDirPath = outputDir.absolutePath().toStdString();

        OmniBin<std::ios::out> writer(filename.toStdString());

        // Gather used resources
        std::map<EAsset, std::unordered_map<qint64, std::string>> assetFiles;
        std::mutex assetGuard;
        auto&& allAssets = QOmnigenAssetMgrSection::getAssets();

        // Rock materials
        auto&& rockMaterialArray = Data::get()->getTerrainTextureArray();
        writer << rockMaterialArray;
        tbb::parallel_for(0, int(rockMaterialArray.size()), [&](int i)
            {
                qint64 id = rockMaterialArray[i];
                auto&& asset = allAssets.at(EAsset::RockMaterial).at(id);
                std::string refPath = asset->name.toStdString() + ".oefa";
                std::string exportPath = outputDirPath + "/" + refPath;

                exportRockMaterial(static_cast<OmnigenAsset<EAsset::RockMaterial>&>(*asset), exportPath);
                std::scoped_lock lock(assetGuard);
                assetFiles[EAsset::RockMaterial][id] = refPath;
            });

        // Cover materials
        auto&& coverMaterialArray = Data::get()->getCoverTextureArray();
        writer << coverMaterialArray;
        tbb::parallel_for(0, int(coverMaterialArray.size()), [&](int i)
            {
                qint64 id = coverMaterialArray[i];
                auto&& asset = allAssets.at(EAsset::SoilMaterial).at(id);
                std::string refPath = asset->name.toStdString() + ".oefa";
                std::string exportPath = outputDirPath + "/" + refPath;

                exportSoilMaterial(static_cast<OmnigenAsset<EAsset::SoilMaterial>&>(*asset), exportPath);
                std::scoped_lock lock(assetGuard);
                assetFiles[EAsset::SoilMaterial][id] = refPath;
            });
        
        // Plants
        if (allAssets.contains(EAsset::Plant))
        {
            tbb::task_group tg;
            for (auto&& [id, asset] : allAssets.at(EAsset::Plant))
            {
                auto&& plant = static_cast<OmnigenAsset<EAsset::Plant>&>(*asset);
                for (auto&& mesh : plant.getMeshes())
                    if (!mesh.getGeometry().at(ELOD::Zero)->instanceData.empty())
                    {
                        tg.run([&, plant]()
                            {
                                std::string refPath = asset->name.toStdString() + ".oefa";
                                std::string exportPath = outputDirPath + "/" + refPath;

                                exportPlant(plant, exportPath);
                                std::scoped_lock lock(assetGuard);
                                assetFiles[EAsset::Plant][id] = refPath;
                            });

                        break;
                    }
            }

            tg.wait();
        }

        // Asset paths
        writer << assetFiles;

        // Terrain chunks
        auto&& [terrainChunks, chunkBlocksMap, blockChunkMap] = Data::get()->getTerrainChunkData();
        writer << terrainChunks.size();

        // Custom chunk export with only Zero LOD
        for (auto&& chunk : terrainChunks)
        {
            writer << *chunk->getGeometry<TerrainMeshVertex>(ELOD::Far);
            writer << chunk->getTerrainTextureIds();
            writer << chunk->getBiomeTextureIds();
        }

        // River meshes
        auto&& riverSurfaces = Data::get()->getMarkers<DRiverSurfaceMarker>();
        writer << riverSurfaces.size();
        for (auto&& riverSurface : riverSurfaces)
            writer << *riverSurface->getGeometry<>(ELOD::Last);

		// Lake meshes
		auto&& lakeSurfaces = Data::get()->getMarkers<DLakeSurfaceMarker>();
		writer << lakeSurfaces.size();
        for (auto&& lakeSurface : lakeSurfaces)
        {
            GeometryData<> exportGeometry = *lakeSurface->getGeometry<>(ELOD::Last);
            float h = lakeSurface->getHeight();
            tbb::parallel_for(0, int(exportGeometry.vertices.size()), [&](int i)
                {
                    exportGeometry.vertices[i].setY(h);
                });

            writer << exportGeometry;
        }

        // Ocean switch
        bool hasOcean = !Data::get()->getAllDomains<EDomainType::Water>().empty();
        writer << hasOcean;

        // Misc
        exportTexture(DTerrainChunk::tileNoise, writer);

        OmniLog(ELoggingLevel::Info) <<= "Successfully exported";
    }

    void StageTools<EGenerationStage::TerrainFinalization>::calcWetness()
    {
        // auto&& globalGraphNew = TrianglesGraph::mergeClustersGraphs(TrianglesGraph::calcTrianglesGraphByClusters());
    }

    void StageTools<EGenerationStage::TerrainFinalization>::exportRockMaterial(const OmnigenAsset<EAsset::RockMaterial>& rockMaterial, const std::string& exportFile)
    {
		OmniBin<std::ios::out> writer(exportFile);

		writer << rockMaterial.getTextures().size();
        for (auto&& mat : rockMaterial.getTextures())
            exportMaterial(mat, writer);
    }

    void StageTools<EGenerationStage::TerrainFinalization>::exportSoilMaterial(const OmnigenAsset<EAsset::SoilMaterial>& soilMaterial, const std::string& exportFile)
    {
        OmniBin<std::ios::out> writer(exportFile);

        writer << soilMaterial.getMaterials().size();
        for (auto&& mat : soilMaterial.getMaterials())
            exportMaterial(mat, writer);
    }

    void StageTools<EGenerationStage::TerrainFinalization>::exportPlant(const OmnigenAsset<EAsset::Plant>& plant, const std::string& exportFile)
    {
        OmniBin<std::ios::out> writer(exportFile);

        // Layer
        writer << plant.layer;

        // Materials
        writer << plant.getMaterials().size();
        for (auto&& mat : plant.getMaterials())
            exportMaterial(mat, writer);

        // Geometries
        writer << plant.getMeshes().size();
        for (auto&& variation : plant.getMeshes())
        {
            // Variations
            writer << variation.getGeometry().size();
            for (auto&& [lod, mesh] : variation.getGeometry())
            {
                // LODs
                auto&& triangles = mesh->indices;

                writer << int(lod);
                writer << triangles;
                writer << mesh->vertices;

                // Convert transforms to TRS
                std::vector<TRS> exportData(mesh->instanceData.size());
                auto s = exportData.size();
                for (quint64 i = 0; i < exportData.size(); ++i)
                {
                    auto&& trs = exportData[i];

                    auto transposedWorld = mesh->instanceData[i].world;
                    DirectX::XMMATRIX transform = reinterpret_cast<DirectX::XMMATRIX&>(transposedWorld);

                    DirectX::XMVECTOR translation, rotation, scale;
                    DirectX::XMMatrixDecompose(&scale, &rotation, &translation, transform);

                    trs.translation = { translation.m128_f32[0], translation.m128_f32[1], translation.m128_f32[2] };
                    trs.scale = { scale.m128_f32[0], scale.m128_f32[1], scale.m128_f32[2] };

					// Decompose quaternion into axis + angle
					float ax = rotation.m128_f32[0];
					float ay = rotation.m128_f32[1];
                    float az = rotation.m128_f32[2];
                    float angle = rotation.m128_f32[3];

                    trs.rotationAngleRadians = acos(angle) * 2;
                    trs.rotationAxis =
                    {
                        ax / sin(trs.rotationAngleRadians / 2),
                        ay / sin(trs.rotationAngleRadians / 2),
                        az / sin(trs.rotationAngleRadians / 2)
                    };
                }

                writer << exportData;
            }
        }
    }

    void StageTools<EGenerationStage::TerrainFinalization>::exportMaterial(const Material& material, OmniBin<std::ios::out>& writer) const
    {
        writer << material.tileSize << material.maxDisplacement << material.outputs.size();

        for (auto&& [slot, tex] : material.outputs)
        {
            writer << slot;
            exportTexture(tex, writer);
        }
    }

    void StageTools<EGenerationStage::TerrainFinalization>::exportTexture(const Texture& texture, OmniBin<std::ios::out>& writer) const
    {
        //std::vector<char> buf;
        //gli::save_dds(texture.getData(), buf);

        std::string ddsFile;
        std::string pngFile;
        {
            std::scoped_lock lock(textureConversionCounterGuard);
            ddsFile = std::format("Texture{}.dds", textureConversionCounter);
            pngFile = std::format("Texture{}.png", textureConversionCounter);
            ++textureConversionCounter;
        }

        // Save temp dds, then convert to png
        bool save_ok = gli::save_dds(texture.getData(), ddsFile);
        Q_ASSERT(save_ok);

        {
            nvtt::SurfaceSet images;
            bool load_ok = images.loadDDS(ddsFile.data());
            Q_ASSERT(load_ok);

            bool convert_ok = images.saveImage(pngFile.data(), 0, 0);
            Q_ASSERT(convert_ok);
        }

        // Append png
        {
            // Read
            std::ifstream infile(pngFile, std::ios::binary | std::ios::ate);
            size_t size = infile.tellg();

            infile.seekg(0, std::ios::beg);
            std::vector<char> buffer(size);
            infile.read(buffer.data(), size);

            // Write
            writer << size;
            writer << buffer;
        }

        QFile::remove(toQString(ddsFile));
        QFile::remove(toQString(pngFile));
    }

    std::optional<QVector3D> StageTools<EGenerationStage::TerrainFinalization>::findPoint(QMouseEvent* mEvent)
    {
        auto chunkTriangle = findChunkTriangleUnderCursor();
        if (!chunkTriangle)
            return {};

        auto&& geometry = chunkTriangle->chunk->getActiveGeometry<TerrainMeshVertex>();
        auto&& vertices = geometry->vertices;
        auto&& triangles = geometry->indices;
        auto&& p0 = vertices[triangles[chunkTriangle->triangleIdx * 3 + 0]];
        auto&& p1 = vertices[triangles[chunkTriangle->triangleIdx * 3 + 1]];
        auto&& p2 = vertices[triangles[chunkTriangle->triangleIdx * 3 + 2]];
        auto triangleCenter = (p0.position + p1.position + p2.position) / 3.0;

        return triangleCenter;
    }

    std::vector<std::tuple<int, int>> StageTools<EGenerationStage::TerrainFinalization>::findGeometryUnderBrush()
    {
        float  brushRadius  = brushSize * 100.0f;
        auto&& data = Generation::Data::get();
        auto&& clusterMap = data->getTerrainClustersMap();
        auto&& cells = data->getTerrainCells()->getCells();

        const float maxCellRadius = data->getLargestVoronoiCellRadius();
        auto blockNodesFound = data->getBlockQuadTree()->find_all_nearest(brushPosition.x(), brushPosition.z(), brushRadius + maxCellRadius);
        std::unordered_set<int> clustersIds;
        for (auto&& blockNode : blockNodesFound)
        {
            const int cellId = blockNode->data;
            const int clusterId = clusterMap[cellId]->keyCell;
            if (clustersIds.contains(clusterId))
            {
                continue;
            }
            auto&& cellPolygon = cells[cellId].getPolygon();
            GVector2D centerCell = cellPolygon.getCenter();
            GVector2D brushPosition2D(brushPosition);
            auto&& segs = cellPolygon.getPtsAsSegments();
            if (brushPosition2D.dist(centerCell, true) <= brushRadius * brushRadius || 
                std::find_if(segs.begin(), segs.end(), [&brushPosition2D, &brushRadius](const auto& seg)
                    {
                        return seg.dist(brushPosition2D) <= brushRadius;
                    }) != segs.end())
            {
                clustersIds.insert(clusterId);
            }
        }

        std::vector<std::tuple<int, int>> pointsUnder;
        for (auto& clusterId : clustersIds)
        {
            auto&& cluster = clusterMap[clusterId];
            auto vertices = cluster->section->getVertices();
            auto&& vertexTree = cluster->getVertexQuadTree();
            auto&& verticesFound = vertexTree.find_all_nearest(brushPosition.x(), brushPosition.z(), brushRadius);
            for (auto& vertex : verticesFound)
            {
                int vertexIndex = vertexIndex = vertex->data;
                GVector2D vertexPosition2D(vertices[vertexIndex].position);
                GVector2D brushPosition2D(brushPosition);
                if (brushPosition2D.dist(vertexPosition2D, true) <= brushRadius * brushRadius)
                {
                    pointsUnder.emplace_back(clusterId, vertexIndex);
                }
            }
        }
        return pointsUnder;
    }

    void StageTools<EGenerationStage::TerrainFinalization>::clearBrush()
    {
        if (brushGizmo)
        {
            Generation::Data::get()->clearSingleExactMarker<DLineMarker>(brushGizmo->getGuid());
            brushGizmo = nullptr;
        }
    }

    void StageTools<EGenerationStage::TerrainFinalization>::updateGeometry(EToolMode mode, const std::unordered_set<int>& clustersIds)
    {
        switch (mode)
        {
        case EToolMode::Sculpt: [[fallthrough]];
        case EToolMode::Smooth: [[fallthrough]];
        case EToolMode::Flatten:
            TODO("update normals");
        }

        TODO("update chunks");
    }

    bool StageTools<EGenerationStage::TerrainFinalization>::finalizeChanges(EToolMode mode, const std::unordered_map<std::pair<int, IndexType>, float>& changes)
    {
        HISTORY_PUSH(finalizeChanges, mode, changes);

        std::unordered_set<int> hChangedClusters;
        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();

        if (History::GetContext()->IsRedoing())
        {
            for (auto&& [vInfo, delta] : changes)
            {
                auto&& [clusterId, vIdx] = vInfo;
                auto&& vertex = clusterMap[clusterId]->section->mainBuffer->vertices[vIdx];
                switch (mode)
                {
                case EToolMode::Sculpt: [[fallthrough]];
                case EToolMode::Smooth: [[fallthrough]];
                case EToolMode::Flatten:
                    vertex.position.setY(vertex.position.y() + delta);
                    break;

                case EToolMode::TexErode:
                    Generation::setPackParam(&vertex.packParams, 0, std::clamp(Generation::getPackParam(vertex.packParams, 0) + delta, 0.0f, 1.0f));
                    break;
                }

                hChangedClusters.insert(clusterId);
            }
        }

        HISTORY_SAVE(hChangedClusters);
        updateGeometry(mode, hChangedClusters);
        return true;
    }

    bool StageTools<EGenerationStage::TerrainFinalization>::finalizeChanges_Undo(EToolMode mode, const std::unordered_map<std::pair<int, IndexType>, float>& changes)
    {
        HISTORY_POP();

        auto&& clusterMap = Generation::Data::get()->getTerrainClustersMap();
        for (auto&& [vInfo, delta] : changes)
        {
            auto&& [clusterId, vIdx] = vInfo;
            auto&& vertex = clusterMap[clusterId]->section->mainBuffer->vertices[vIdx];
            switch (mode)
            {
            case EToolMode::Sculpt: [[fallthrough]];
            case EToolMode::Smooth: [[fallthrough]];
            case EToolMode::Flatten:
                vertex.position.setY(vertex.position.y() + delta);
                break;

            case EToolMode::TexErode:
                Generation::setPackParam(&vertex.packParams, 0, std::clamp(Generation::getPackParam(vertex.packParams, 0) + delta, 0.0f, 1.0f));
                break;
            }
        }

        std::unordered_set<int> hChangedClusters;
        HISTORY_LOAD(hChangedClusters);
        updateGeometry(mode, hChangedClusters);

        return true;
    }

    auto StageTools<EGenerationStage::TerrainFinalization>::findChunkTriangleUnderCursor() const -> std::optional<ChunkTriangle>
    {
        auto chunkData = SelectionMgrBase::findObjectUnderCursor<DTerrainChunk>();
        if (!chunkData)
            return {};

        return ChunkTriangle{ chunkData->object.get(), chunkData->primitive };
    }
}