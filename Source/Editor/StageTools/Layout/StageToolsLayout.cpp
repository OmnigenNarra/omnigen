#include "stdafx.h"
#include "StageToolsLayout.h"
#include "Omnigen.h"
#include "Utils/PlatformMisc.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Scene/Generation/Stages/Layout/DomainPaintingPreview.h"
#include "LayoutSelection.h"
#include "Scene/Generation/Stages/Layout/DomainSquareDrawable.h" 
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editable.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "../StageTools.h"
#include "../StageObjectNode.h"

#include <QShortcut>
#include <QApplication>

namespace Design
{
    StageTools<EGenerationStage::Layout>::StageTools()
        : StageToolsBase()
    {
        setupActions();
    }

    SelectionMgrBase* StageTools<EGenerationStage::Layout>::getSelectionMgr() const
    {
        return LayoutSelectionMgr::get();
    }

    void StageTools<EGenerationStage::Layout>::bind()
    {
        StageToolsBase::bind();

        bPainting = false;
        domainTypeToPaint = EDomainType::Terrain;
        paintingOption = EOmnigenPainting::Append;

        auto* omnigen = Omnigen::get();
        auto* genData = Generation::Data::get();
        auto* outline = omnigen->getOutline();
        auto* properties = omnigen->getProperties();

        // Widget setup
        auto* toolbar = createOutlineToolbar();
        paintTool = createPaintTool();

        treeView = new OutlineTreeView;
        treeView->setModel(&treeModel);
        treeModel.setTreeView(treeView);
        treeModel.loadDomains();

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, paintTool, treeView });

        treeView->show();
        paintTool->setVisible(bPainting);
        paintTool->setMaximumSize(270, 150);

        // Data events
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(&treeModel, &QLayoutTreeModel::addDomain);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Modified>(&treeModel, &QLayoutTreeModel::updateDomain);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(&treeModel, &QLayoutTreeModel::removeDomain);

        // Selection mgr
        auto* selMgr = LayoutSelectionMgr::get();
        qConnections << connect(selMgr, &SelectionMgrBase::selectionChanged, this, [this, selMgr, properties]()
            {
                updateTreeViewSelection();
                QOmnigenViewportSection::repaintAll();
            });

        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools::selectNewDomain);

        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
            viewport->installEventFilter(this);

        // Shortcuts
        auto bindShortcut = [&](const QKeySequence keys, QAction* action)
        {
            shortcuts.emplace_back(new QShortcut(omnigen));
            auto* shortcut = shortcuts.back();
            shortcut->setKey(keys);
            qConnections << connect(shortcut, &QShortcut::activated, action, &QAction::trigger);
        };

        bindShortcut(Qt::ALT + Qt::Key_1, actions[ELayoutAction::CreateTerrain]);
        bindShortcut(Qt::ALT + Qt::Key_2, actions[ELayoutAction::CreateBiome]);
        bindShortcut(Qt::ALT + Qt::Key_3, actions[ELayoutAction::CreateWater]);

        bindShortcut(Qt::CTRL + Qt::ALT + Qt::Key_1, actions[ELayoutAction::ExtractTerrain]);
        bindShortcut(Qt::CTRL + Qt::ALT + Qt::Key_2, actions[ELayoutAction::ExtractBiome]);
        bindShortcut(Qt::CTRL + Qt::ALT + Qt::Key_3, actions[ELayoutAction::ExtractWater]);

        bindShortcut(Qt::Key_Backspace, actions[ELayoutAction::DeteleSelectedDomains]);
        bindShortcut(Qt::Key_Delete, actions[ELayoutAction::DeteleSelectedDomains]);

        qConnections << connect(omnigen, &Omnigen::toggleShortcut, this, [this](bool enable)
            {
                for (auto* shortcut : shortcuts)
                    shortcut->setEnabled(enable);
            });

        treeView->expandAll();
    }

    void StageTools<EGenerationStage::Layout>::unbind()
    {
        StageToolsBase::unbind();

        for (auto&& viewport : Omnigen::get()->getAllViewports())
            viewport->removeEventFilter(this);

        treeModel.clear();
    }

    void StageTools<EGenerationStage::Layout>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();

        // Save terrain
        int terrain_chunk_count = 0;
        for (int x = 0; x < GRID_SEGMENT_COUNT; ++x)
            for (int z = 0; z < GRID_SEGMENT_COUNT; ++z)
                if (genData->domainSquares[x][z])
                    ++terrain_chunk_count;

        writer << terrain_chunk_count;

        for (int x = 0; x < GRID_SEGMENT_COUNT; ++x)
            for (int z = 0; z < GRID_SEGMENT_COUNT; ++z)
                if (genData->domainSquares[x][z])
                    writer << x << z << genData->domainSquares[x][z];

        // Save domains
        writer << genData->domains;

        // save nodes
        writer << terrainDomainNodes;
        writer << waterDomainNodes;
        writer << biomeDomainNodes;
        writer << domainSquareNodes;
    }

    void StageTools<EGenerationStage::Layout>::load(OmniBin<std::ios::in>& reader)
    {
        OmniProfile("Layout");

        // Need to disconnect nodes, as loading always happen on layout stage
        disconnectNodes();
        auto&& genData = Generation::Data::get();

        // Load terrain
        int count, x, z;
        reader >> count;

        for (int i = 0; i < count; ++i)
        {
            reader >> x >> z >> genData->domainSquares[x][z];
            emit Editable::created(genData->domainSquares[x][z]);
        }

        // Load domains
        reader >> genData->domains;
        for (auto&& [handle, domain] : genData->domains)
        {
            domain->bindHandle(handle);
            emit Editable::created(domain);
            emit Editable::created(handle);

            // Recalculate domain square mappings
            domain->setSquares(domain->getSquares());
        }

        // load nodes
        reader >> terrainDomainNodes;
        reader >> waterDomainNodes;
        reader >> biomeDomainNodes;
        reader >> domainSquareNodes;
    }

    void StageTools<EGenerationStage::Layout>::connectNodes()
    {
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::Layout>::addNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::Layout>::removeNode);
        qNodesConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeModified>(this, &StageTools<EGenerationStage::Layout>::modifyNode);
    }

    void StageTools<EGenerationStage::Layout>::aboutToEnterStage(int dir)
    {
        connectNodes();
    }

    void StageTools<EGenerationStage::Layout>::aboutToExitStage(int dir)
    {
        if (dir > 0)
            cleanNodesState();

        disconnectNodes();
    }

    void StageTools<EGenerationStage::Layout>::clearNodes()
    {
        terrainDomainNodes.clear();
        waterDomainNodes.clear();
        biomeDomainNodes.clear();
        domainSquareNodes.clear();
    }

    void StageTools<EGenerationStage::Layout>::cleanNodesState()
    {
        std::erase_if(terrainDomainNodes, [](auto& kv) { return !kv.second->getDomain(); });
        std::erase_if(waterDomainNodes, [](auto& kv) { return !kv.second->getDomain(); });
        std::erase_if(biomeDomainNodes, [](auto& kv) { return !kv.second->getDomain(); });
        std::erase_if(domainSquareNodes, [](auto& kv) { return !kv.second->getSquare(); });

        for (auto&& [domainGuid, domainNode] : terrainDomainNodes)
        {
            domainNode->setCreatedOnCurrentStage(false);
            domainNode->clearSnapshot();
        }
        for (auto&& [domainGuid, domainNode] : waterDomainNodes)
        {
            domainNode->setCreatedOnCurrentStage(false);
            domainNode->clearSnapshot();
        }
        for (auto&& [domainGuid, domainNode] : biomeDomainNodes)
        {
            domainNode->setCreatedOnCurrentStage(false);
            domainNode->clearSnapshot();
        }
        for (auto&& [square, squareNode] : domainSquareNodes)
        {
            squareNode->setCreatedOnCurrentStage(false);
            squareNode->setModifiedOnCurrentStage(false);
        }
    }

    void StageTools<EGenerationStage::Layout>::loadSnapshotData()
    {
        disconnectNodes();
        // best way to improve it would be to make generic domain node

        auto addDomain = [&](EDomainType type, qint64 guid, const QSet<GPoint>& squares, const QSharedPointer<DomainDataBase>& database)
        {
            auto newDomain = QSharedPointer<DDomain>::create();
            auto newDomainHandle = QSharedPointer<DDomainHandle>::create();
            newDomain->setType(type);
            newDomain->setGuid(guid);
            newDomain->initialize();
            newDomainHandle->initialize();
            Generation::Data::get()->addDomain(newDomainHandle, newDomain);

            newDomain->setSquares(squares);
            Generation::Data::get()->createDomainSquares(newDomain->getSquares());
            newDomain->bindHandle(newDomainHandle);
            newDomain->setData(database);
            emit Editable::created(newDomain);
            emit Editable::created(newDomainHandle);

            return newDomain;
        };

        auto modifyDomain = [](const QSharedPointer<DDomain>& domain, const QSet<GPoint>& squares, const QSharedPointer<DomainDataBase>& database)
        {
            Generation::Data::get()->clearDomainSquares(domain->getSquares(), false);
            domain->setSquares(squares);
            Generation::Data::get()->createDomainSquares(domain->getSquares());
            domain->setData(database);
        };

        auto removeDomain = [](const QSharedPointer<DDomain>& domain)
        {
            auto dh = domain->getHandle().lock();
            auto squares = domain->getSquares();
            Generation::Data::get()->removeDomain(dh);
            Generation::Data::get()->clearDomainSquares(squares, false);
        };

        // Delete first so squares are updated properly
        for (auto it = terrainDomainNodes.begin(); it != terrainDomainNodes.end();)
            if (auto&& [domainGuid, domainNode] = *it; domainNode->isCreatedOnCurrentStage())
            {
                removeDomain(domainNode->getDomain());
                it = terrainDomainNodes.erase(it);
            }
            else
                it++;

        for (auto it = waterDomainNodes.begin(); it != waterDomainNodes.end();)
            if (auto&& [domainGuid, domainNode] = *it; domainNode->isCreatedOnCurrentStage())
            {
                removeDomain(domainNode->getDomain());
                it = waterDomainNodes.erase(it);
            }
            else
                it++;

        for (auto it = biomeDomainNodes.begin(); it != biomeDomainNodes.end();)
            if (auto&& [domainGuid, domainNode] = *it; domainNode->isCreatedOnCurrentStage())
            {
                removeDomain(domainNode->getDomain());
                it = biomeDomainNodes.erase(it);
            }
            else
                it++;

        for (auto&& [domainGuid, domainNode] : terrainDomainNodes)
            if (auto&& snapshotDomain = domainNode->getSnapshot())
            {
                // modify
                if (domainNode->getDomain())
                    modifyDomain(domainNode->getDomain(), snapshotDomain->squares, snapshotDomain->database);
                // add
                else
                    domainNode->setDomain(addDomain(snapshotDomain->type, domainGuid, snapshotDomain->squares, snapshotDomain->database));

                domainNode->clearSnapshot();
            }

        for (auto&& [domainGuid, domainNode] : waterDomainNodes)
            if (auto&& snapshotDomain = domainNode->getSnapshot())
            {
                // modify
                if (domainNode->getDomain())
                    modifyDomain(domainNode->getDomain(), snapshotDomain->squares, snapshotDomain->database);
                // add
                else
                    domainNode->setDomain(addDomain(snapshotDomain->type, domainGuid, snapshotDomain->squares, snapshotDomain->database));

                domainNode->clearSnapshot();
            }

        for (auto&& [domainGuid, domainNode] : biomeDomainNodes)
            if (auto&& snapshotDomain = domainNode->getSnapshot())
            {
                // modify
                if (domainNode->getDomain())
                    modifyDomain(domainNode->getDomain(), snapshotDomain->squares, snapshotDomain->database);
                // add
                else
                    domainNode->setDomain(addDomain(snapshotDomain->type, domainGuid, snapshotDomain->squares, snapshotDomain->database));

                domainNode->clearSnapshot();
            }

        for (auto it = domainSquareNodes.begin(); it != domainSquareNodes.end();)
        {
            auto&& [square, squareNode] = *it;
            squareNode->setSquare(square);
            squareNode->setModifiedOnCurrentStage(false);

            if (squareNode->isCreatedOnCurrentStage())
            {
                it = domainSquareNodes.erase(it);
            }
            else
                it++;
        }

        connectNodes();
    }

    bool StageTools<EGenerationStage::Layout>::validatePipeline()
    {
        for (auto&& [domainGuid, domainNode] : terrainDomainNodes)
        {
            // removed domain
            if (!domainNode->getDomain())
                return false;
            // added domain
            else if (domainNode->isCreatedOnCurrentStage())
                return false;
            // modified domain
            else if (domainNode->getSnapshot())
                return false;
        }

        for (auto&& [domainGuid, domainNode] : waterDomainNodes)
        {
            // removed domain
            if (!domainNode->getDomain())
                return false;
            // added domain
            else if (domainNode->isCreatedOnCurrentStage())
                return false;
            // modified domain
            else if (domainNode->getSnapshot())
                return false;
        }

        return true;
    }

    std::vector<QSet<GPoint>> StageTools<EGenerationStage::Layout>::partitionLandWithShorelineSquares(QSet<GPoint> squares, const QSet<GPoint>& landSquares)
    {
        std::vector<QSet<GPoint>> result;
        QSet<GPoint> perimeter;
        QSet<GPoint> target;

        auto findNextSquare = [&perimeter, &squares]() -> std::optional<GPoint>
        {
            if (perimeter.isEmpty())
                return *squares.begin();

            for (auto&& sq : perimeter)
                if (squares.contains(sq))
                    return sq;

            return {};
        };

        auto updatePerimeter = [&target, &perimeter, &landSquares](const GPoint& newSquare)
        {
            if (GPoint sq = { newSquare.x - 1, newSquare.z }; !target.contains(sq))
                perimeter += sq;

            if (GPoint sq = { newSquare.x + 1, newSquare.z }; !target.contains(sq))
                perimeter += sq;

            if (GPoint sq = { newSquare.x, newSquare.z - 1 }; !target.contains(sq))
                perimeter += sq;

            if (GPoint sq = { newSquare.x, newSquare.z + 1 }; !target.contains(sq))
                perimeter += sq;

            if (landSquares.contains(newSquare))
            {
                if (GPoint sq = { newSquare.x - 1, newSquare.z - 1 }; !target.contains(sq) && landSquares.contains(sq))
                    perimeter += sq;

                if (GPoint sq = { newSquare.x - 1, newSquare.z + 1 }; !target.contains(sq) && landSquares.contains(sq))
                    perimeter += sq;

                if (GPoint sq = { newSquare.x + 1, newSquare.z - 1 }; !target.contains(sq) && landSquares.contains(sq))
                    perimeter += sq;

                if (GPoint sq = { newSquare.x + 1, newSquare.z + 1 }; !target.contains(sq) && landSquares.contains(sq))
                    perimeter += sq;
            }

            perimeter.remove(newSquare);
        };

        while (true)
        {
            if (squares.isEmpty())
                break;

            // Init new set
            target.clear();
            perimeter.clear();

            // Fill set by adjacency
            while (auto nextSquare = findNextSquare())
            {
                target += *nextSquare;
                squares.remove(*nextSquare);
                updatePerimeter(*nextSquare);
            }

            // Insert set into output
            result << target;
        }

        return result;
    }

    using PreservedLands = std::vector<std::pair<std::vector<QSharedPointer<DShorelineMarker>>, std::vector<QSharedPointer<DShorelineMarker>>>>;
    std::unordered_map<qint64, PreservedLands> StageTools<EGenerationStage::Layout>::findPreservedLandsOfLandmasses(const std::unordered_set<qint64> landmassToPreserve, const std::unordered_set<qint64> invalidShorelines, const QSet<GPoint>& changedSquares)
    {
        auto&& landmassStageTools = getStageTools<EGenerationStage::Landmasses>();
        auto&& landmassNodes = landmassStageTools->getLandmassNodes();

        QSet<GPoint> terrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
        QSet<GPoint> waterSquares = Generation::Data::get()->getAllSquares<EDomainType::Water>();

        std::unordered_map<GPoint, std::vector<QSharedPointer<DShorelineMarker>>> shorelineSquares;
        std::unordered_map<GPoint, std::vector<QSharedPointer<DShorelineMarker>>> innerShorelineSquares;
        QSet<GPoint> landUnavaliableSquares;
        QSet<GPoint> landAvaliableSquares;

        // fill map with not invalidated shorelines
        for (auto&& landmassGuid : landmassToPreserve)
            landmassNodes.at(landmassGuid)->getLandmass()->forEachShoreline([&](auto& s, bool isInner)
                {
                    if (invalidShorelines.contains(s->getGuid()))
                    {
                        if (isInner)
                            landAvaliableSquares += s->getSquares();
                        return;
                    }

                    auto&& shorelineMap = isInner ? innerShorelineSquares : shorelineSquares;
                    for (auto&& sq : s->getSquares())
                        shorelineMap[sq] << s;
                });

        // find avaliable/unavaliable squares,
        // avaliable: (landmass terrain & all terrain) + (terrain - water) + innner shoreline squares
        // unavaliable: all around (water - terrain)
        for (auto&& sq : (waterSquares - terrainSquares))
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    landUnavaliableSquares += GPoint(sq.x + x, sq.z + z);

        landAvaliableSquares += terrainSquares - waterSquares;
        for (auto&& landmassGuid : landmassToPreserve)
            landAvaliableSquares += container_and(landmassNodes.at(landmassGuid)->getLandmass()->getSquares(), terrainSquares);
        for (auto&& [sq, _] : innerShorelineSquares)
            if (!shorelineSquares.contains(sq))
                landAvaliableSquares += sq;

        landAvaliableSquares -= landUnavaliableSquares;

        std::unordered_map<qint64, PreservedLands> preservedLandsPerLandmass;

        QSet<GPoint> assignedSquares;

        // Search for landmass to preserve by analyzing original avaliable squares and unaffected shorelines
        for (auto&& landmassGuid : landmassToPreserve)
        {
            auto landmass = landmassNodes.at(landmassGuid)->getLandmass();
            auto avaliableLandmassSquares = container_and(landmass->getSquares(), landAvaliableSquares) - assignedSquares;

            // search for avaliable landmass squares, starting from original avaliable landmass squares, extending to any avaliable squares
            QQueue<GPoint> queue;
            for (GPoint sq : avaliableLandmassSquares)
            {
                queue << sq;
                assignedSquares += sq;
            }

            auto&& addToQueue = [&](const GPoint&& sq)
            {
                if (!assignedSquares.contains(sq) && landAvaliableSquares.contains(sq))
                {
                    queue << sq;
                    assignedSquares += sq;
                    avaliableLandmassSquares += sq;
                }
            };

            while (!queue.isEmpty())
            {
                const GPoint square = queue.dequeue();

                for (int x = -1; x <= 1; x++)
                    for (int z = -1; z <= 1; z++)
                        if (!(x == 0 && z == 0))
                            addToQueue(GPoint(square.x + x, square.z + z));
            }

            // Catch all shorelines that are neighboring avaliable landmass squares, and select their squares
            QSet<GPoint> avaliableLandmassWithShoreliensSquares = avaliableLandmassSquares;
            std::unordered_set<qint64> checkedShorelines;
            
            for (auto&& sq : avaliableLandmassSquares)
                for (int x = -1; x <= 1; x++)
                    for (int z = -1; z <= 1; z++)
                    {
                        auto newSq = GPoint(sq.x + x, sq.z + z);

                        if (shorelineSquares.contains(newSq))
                        {
                            for (auto&& shoreline : shorelineSquares[newSq])
                                if (!checkedShorelines.contains(shoreline->getGuid()) && !container_and(shoreline->getLandmass().lock()->getSquares(), avaliableLandmassSquares).empty())
                                {
                                    checkedShorelines.insert(shoreline->getGuid());
                                    avaliableLandmassWithShoreliensSquares += shoreline->getSquares();
                                }
                        }
                        if (innerShorelineSquares.contains(newSq))
                            for (auto&& shoreline : innerShorelineSquares[newSq])
                                if (!checkedShorelines.contains(shoreline->getGuid()) && !container_and(shoreline->getLandmass().lock()->getSquares(), avaliableLandmassSquares).empty())
                                {
                                    checkedShorelines.insert(shoreline->getGuid());
                                    avaliableLandmassWithShoreliensSquares += shoreline->getSquares();
                                }
                    }

            PreservedLands preservedLands;

            // partition, then pick associated shorelines and generate new one if required
            auto&& partitionedLandWithShorelineSquares = partitionLandWithShorelineSquares(avaliableLandmassWithShoreliensSquares, avaliableLandmassSquares);
            for (auto&& landWithShorelineSquares : partitionedLandWithShorelineSquares)
            {
                std::unordered_set<qint64> checkedShorelines;
                std::vector<QSharedPointer<DShorelineMarker>> associatedShorelines;
                std::vector<QSharedPointer<DShorelineMarker>> associatedInnerShorelines;

                QSet<GPoint> landSquares;
                QSet<GPoint> existingShorelineSquares;
                QSet<GPoint> needNewShorelineSquares;

                // find all associated shorelines
                for (auto&& sq : landWithShorelineSquares)
                {
                    if (shorelineSquares.contains(sq))
                    {
                        for (auto&& shoreline : shorelineSquares[sq])
                            if (!checkedShorelines.contains(shoreline->getGuid()) && !container_and(shoreline->getLandmass().lock()->getSquares(), landWithShorelineSquares).empty())
                            {
                                existingShorelineSquares += shoreline->getSquares();
                                associatedShorelines << shoreline;
                                checkedShorelines.insert(shoreline->getGuid());
                            }
                    }
                    if (innerShorelineSquares.contains(sq))
                    {
                        for (auto&& shoreline : innerShorelineSquares[sq])
                            if (!checkedShorelines.contains(shoreline->getGuid()) && !container_and(shoreline->getLandmass().lock()->getSquares(), landWithShorelineSquares).empty())
                            {
                                existingShorelineSquares += shoreline->getSquares();
                                associatedInnerShorelines << shoreline;
                                checkedShorelines.insert(shoreline->getGuid());
                            }
                    }
                }

                // find squares that requiers new shoreline on them (land squares that are neighboring square with water, but does not have shoreline)
                landSquares = landWithShorelineSquares - existingShorelineSquares;
                for (auto&& sq : landSquares)
                    for (int x = -1; x <= 1; x++)
                        for (int z = -1; z <= 1; z++)
                            if (auto&& newSq = GPoint(sq.x + x, sq.z + z); !shorelineSquares.contains(newSq) && !landSquares.contains(newSq) && waterSquares.contains(newSq))
                                needNewShorelineSquares += newSq;

                // It can rarely happen that solo square that is only connected by corner is selectd as new shoreline, which is invalid
                QSet<GPoint> landWithNewShorelineSquares = landSquares + needNewShorelineSquares;
                QSet<GPoint> invalidSoloCornerSquares;
                for (auto&& sq : needNewShorelineSquares)
                {
                    if (!landWithNewShorelineSquares.contains(GPoint(sq.x + 1, sq.z)) &&
                        !landWithNewShorelineSquares.contains(GPoint(sq.x - 1, sq.z)) &&
                        !landWithNewShorelineSquares.contains(GPoint(sq.x, sq.z + 1)) &&
                        !landWithNewShorelineSquares.contains(GPoint(sq.x, sq.z - 1)))
                        invalidSoloCornerSquares += sq;
                }
                needNewShorelineSquares -= invalidSoloCornerSquares;

                for(auto&& shorelines : DShorelineMarker::generateBasicShorelines(landWithShorelineSquares, needNewShorelineSquares))
                    associatedShorelines << shorelines;

                preservedLands << std::pair(associatedShorelines, associatedInnerShorelines);
            }

            preservedLandsPerLandmass[landmassGuid] = preservedLands;
        }

        return preservedLandsPerLandmass;
    }

    void StageTools<EGenerationStage::Layout>::updatePipeline()
    {
        auto&& landmassStageTools = getStageTools<EGenerationStage::Landmasses>();
        auto&& ridgeStageTools = getStageTools<EGenerationStage::Ridges>();
        auto&& isohypseStageTools = getStageTools<EGenerationStage::ContourLines>();
        auto&& terrainModelStageTools = getStageTools<EGenerationStage::TerrainModel>();
        auto&& lithomapStageTools = getStageTools<EGenerationStage::Lithomap>();
        auto&& shorelineNodes = landmassStageTools->getShorelineNodes();
        auto&& landmassNodes = landmassStageTools->getLandmassNodes();
        auto&& ridgeNodes = ridgeStageTools->getRidgeNodes();
        auto&& isohypseNodes = isohypseStageTools->getIsohypseNodes();

        std::unordered_map<GPoint, float> newMaxHeights;
        std::unordered_map<GPoint, ELandform> oldLandforms;
        QSet<GPoint> changedSquares;
        QSet<GPoint> changedTerrainSquares;
        std::unordered_set<qint64> modifiedTerrainDomains;

        std::unordered_set<qint64> invalidateRidges;
        std::unordered_set<qint64> isohypsesToInvalidate;
        std::unordered_set<qint64> invalidateLandmasses;
        std::unordered_set<qint64> affectedLandmasses;
        std::unordered_set<qint64> affectedShorelines;

        // Use nodes to gather/affect data
        for (auto&& [domainGuid, domainNode] : terrainDomainNodes)
            if (!domainNode->getDomain())
            {
                auto&& snapshotDatabase = domainNode->getSnapshot()->database.dynamicCast<DomainData<EDomainType::Terrain>>();

                auto&& updatedSquares = domainNode->getSnapshot()->squares;
                changedSquares += updatedSquares;
                changedTerrainSquares += updatedSquares;

                for (auto&& sq : domainNode->getSnapshot()->squares)
                    oldLandforms[sq] = snapshotDatabase->landform;

                modifiedTerrainDomains += domainGuid;
            }
            else if (auto&& domain = domainNode->getDomain(); domainNode->isCreatedOnCurrentStage())
            {
                auto&& database = domainNode->getDomain()->getData<EDomainType::Terrain>();

                auto&& updatedSquares = domain->getSquares();
                changedSquares += updatedSquares;
                changedTerrainSquares += updatedSquares;

                for (auto&& sq : domain->getSquares())
                    newMaxHeights[sq] = database->maxHeight;

                modifiedTerrainDomains += domainGuid;
            }
            else if (auto&& snapshotDomain = domainNode->getSnapshot())
            {
                auto&& snapshotDatabase = snapshotDomain->database.dynamicCast<DomainData<EDomainType::Terrain>>();
                auto&& domain = domainNode->getDomain();
                auto&& database = domainNode->getDomain()->getData<EDomainType::Terrain>();

                auto&& updatedSquares = (snapshotDomain->squares + domain->getSquares()) - container_and(snapshotDomain->squares, domain->getSquares());
                changedSquares += updatedSquares;
                changedTerrainSquares += updatedSquares;

                if (snapshotDatabase->maxHeight > database->maxHeight)
                    for (auto&& sq : domain->getSquares())
                        newMaxHeights[sq] = database->maxHeight;

                if (snapshotDatabase->landform != database->landform)
                    for (auto&& sq : snapshotDomain->squares)
                        oldLandforms[sq] = snapshotDatabase->landform;

                modifiedTerrainDomains += domainGuid;
            }

        for (auto&& [domainGuid, domainNode] : waterDomainNodes)
            if (!domainNode->getDomain())
            {
                changedSquares += domainNode->getSnapshot()->squares;
            }
            else if (domainNode->isCreatedOnCurrentStage())
            {
                changedSquares += domainNode->getDomain()->getSquares();
            }
            else if (auto&& snapshotDomain = domainNode->getSnapshot())
            {
                changedSquares += (snapshotDomain->squares + domainNode->getDomain()->getSquares()) - container_and(snapshotDomain->squares, domainNode->getDomain()->getSquares());
            }

        std::unordered_map<qint64, std::unordered_set<qint64>> rootRidgesPerDomain;
        std::unordered_set<qint64> affectedRidges;

        for (auto&& sq : changedSquares)
            affectedRidges += domainSquareNodes[sq]->getRidgeNodes();

        for(auto&& [sq, _] : newMaxHeights)
            affectedRidges += domainSquareNodes[sq]->getRidgeNodes();

        for (auto&& [sq, _] : oldLandforms)
            affectedRidges += domainSquareNodes[sq]->getRidgeNodes();

        // find all ridges which are highter then new max height or have different landform
        for (auto&& affectedRidge : affectedRidges)
        {
            if (invalidateRidges.contains(affectedRidge))
                continue;

            auto&& ridge = ridgeNodes.at(affectedRidge)->getRidge();
            auto&& ridgeSquares = ridge->getSquares();

            for (auto&& sq : ridgeSquares)
                if (auto&& domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain))
                    rootRidgesPerDomain[ridge->findRootParent()->getGuid()] += domain->getGuid();

            // check for diff height
            if (auto&& sq = *ridgeSquares.begin(); newMaxHeights.contains(sq))
            {
                auto&& ridgePts = ridge->getControlPoints();
                auto&& ridgeH = ridge->getHeights();
                auto&& newH = newMaxHeights[sq];

                for (IndexType i = 0; i < ridgePts.size(); i++)
                    if (newH < ridgeH[i])
                        ridge->forEachChild([&](auto& r) { invalidateRidges += r->getGuid(); }, ridge);
            }

            // check for diff landform
            if (auto&& sq = *ridgeSquares.begin(); oldLandforms.contains(sq))
                if (auto&& domain = Generation::Data::get()->getDomainAtSquare(sq, EDomainType::Terrain); domain && oldLandforms[sq] != domain->getData<EDomainType::Terrain>()->landform)
                    ridge->forEachChild([&](auto& r) { invalidateRidges += r->getGuid(); }, ridge);
        }

        // find all ridges if they are placed on more then one domain
        for (auto&& [rootRidgeGuid, domainGuids] : rootRidgesPerDomain)
            if (auto&& ridge = ridgeNodes.at(rootRidgeGuid)->getRidge(); domainGuids.size() > 1)
                ridge->forEachChild([&](auto& r) { invalidateRidges += r->getGuid(); }, ridge->findRootParent());


        // invalidate ridges
        for (auto&& ridgeNodeToInvalidate : invalidateRidges)
        {
            auto ridge = ridgeNodes.at(ridgeNodeToInvalidate)->getRidge();

            ridgeStageTools->removeNode(ridge);

            if (!ridge->getParent())
                Generation::Data::get()->clearSingleExactMarker<DRidgeMarker>(ridge->getGuid());
            else
            {
                emit Editable::aboutToBeDeleted(ridge);
                ridge->getParent().lock()->removeChild(ridge);
            }
        }

        std::unordered_set<qint64> isohypsesAroundModifiedDomains;
        for (auto&& sq : GPoint::growMargin(changedTerrainSquares))
            if (domainSquareNodes.contains(sq))
                isohypsesAroundModifiedDomains += domainSquareNodes[sq]->getIsohypseNodes();

        isohypsesToInvalidate += isohypsesAroundModifiedDomains;

        // Check against other IH affected by modified terrain domains
        for (auto&& ihGuid : isohypsesAroundModifiedDomains)
            if (isohypseNodes.contains(ihGuid) && isohypseNodes.at(ihGuid)->getIsohypse())
            {
                auto&& isohypse = isohypseNodes.at(ihGuid)->getIsohypse();

                std::set<Isohypse*> parentIhs = isohypse->getParentIHs();

                while (true)
                {
                    if (parentIhs.empty())
                        break;

                    auto&& parentIh = *parentIhs.begin();

                    if (!isohypsesToInvalidate.contains(parentIh->getGuid()) && !container_and(parentIh->data.affectedBy[EIHAffectType::Domain], modifiedTerrainDomains).empty())
                    {
                        parentIhs.insert(parentIh->getParentIHs().begin(), parentIh->getParentIHs().end());
                        isohypsesToInvalidate += parentIh->getGuid();
                    }

                    parentIhs.erase(parentIh);
                }
            }

        for (auto&& isohypseToInvalidate : isohypsesToInvalidate)
        {
            auto isohypse = isohypseNodes.at(isohypseToInvalidate)->getIsohypse();
            isohypseStageTools->removeNode(isohypse);
            despawnBatched(isohypse);
        }

        // find all affected shorelines and landmasses (right now, all that are neighboring changed squares)
        for(auto&& sq : changedSquares)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                {
                    auto&& newSq = GPoint(sq.x + x, sq.z + z);
                    auto&& domains = Generation::Data::get()->getDomainsAtSquare(sq);

                    if (!domainSquareNodes.contains(newSq))
                        continue;

                    // if landmass is neighboring changed water only square, then invalidate
                    auto&& landmassesContainer = domains.contains(EDomainType::Water) && !domains.contains(EDomainType::Terrain) ? invalidateLandmasses : affectedLandmasses;

                    affectedShorelines += domainSquareNodes[newSq]->getShorelineNodes();
                    landmassesContainer += domainSquareNodes[newSq]->getLandmassNodes();
                }
        for (auto&& invalidLandmass : invalidateLandmasses)
            affectedLandmasses.erase(invalidLandmass);                    

        // Make shorelines which are sharing squares with affected shorelines in landmass also affected (they would cause issue during invalidation)
        for (auto&& affectedLandmass : affectedLandmasses)
        {
            std::unordered_map<GPoint, std::unordered_set<qint64>> shorelinesNeighborMap;
            std::unordered_map<qint64, std::unordered_set<qint64>> shorelineNeighbors;

            landmassNodes.at(affectedLandmass)->getLandmass()->forEachShoreline([&](auto& s, bool isInner)
                {
                    for (auto&& sq : s->getSquares())
                        shorelinesNeighborMap[sq] += s->getGuid();

                });

            for (auto&& [sq, shorelineGuids] : shorelinesNeighborMap)
                if (shorelineGuids.size() > 1)
                    for (auto&& shorelineGuid : shorelineGuids)
                        shorelineNeighbors[shorelineGuid] += shorelineGuids;


            std::unordered_set<qint64> shorelinesToCheck = affectedShorelines;

            while (true)
            {
                if (shorelinesToCheck.empty())
                    break;

                auto&& shorelineGuid = *shorelinesToCheck.begin();
                affectedShorelines += shorelineGuid;

                for (auto&& shorelineNeighbor : shorelineNeighbors[shorelineGuid])
                    if (!affectedShorelines.contains(shorelineNeighbor))
                        shorelinesToCheck += shorelineNeighbor;
                
                shorelinesToCheck.erase(shorelineGuid);
            }
        }

        // Find all lands to preserve
        auto&& preservedLandsPerLandmass = findPreservedLandsOfLandmasses(affectedLandmasses, affectedShorelines, changedSquares);

        // Right now all affected shorelines are invalidated
        for (auto&& shorelineToInvalidate : affectedShorelines)
        {
            auto shoreline = shorelineNodes.at(shorelineToInvalidate)->getShoreline();
            
            shoreline->getLandmass().lock()->removeShoreline(shoreline);
            landmassStageTools->removeNode(shoreline);
            Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(shoreline->getGuid());
        }

        // Invalidate landmasses
        for (auto&& invalidateLandmass : invalidateLandmasses)
        {
            auto landmass = landmassNodes.at(invalidateLandmass)->getLandmass();

            landmass->forEachShoreline([&](auto& s, bool isInner)
                {
                    landmassStageTools->removeNode(s);
                    Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(s->getGuid());
                });

            landmassStageTools->removeNode(landmass);
            Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
        }

        // Invalidate, update and add lands according to their preserved status
        for (auto&& [landmassGuid, preservedLands] : preservedLandsPerLandmass)
        {
            auto landmass = landmassNodes.at(landmassGuid)->getLandmass();

            // Invalidate landmass
            if (preservedLands.empty())
            {
                landmassStageTools->removeNode(landmass);
                Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());

                continue;
            }

            // Replace current landmass with new data (first lands in vector right now)
            auto&& [shorelinesReplacement, innerShorelinesReplacement] = preservedLands.front();
            landmassStageTools->modifyNode(landmass);
            landmass->clearShorelines();
            landmass->setShoreline(shorelinesReplacement);
            landmass->setInnerSeaShoreline(innerShorelinesReplacement);
            landmass->forEachShoreline([&](auto& s, bool isInner) 
                {
                    s->setLandmass(landmass);
                    landmassStageTools->addNode(0, s);
                });
            landmass->recalculateLandmassPolygons();
            QOmnigenViewport::updateDrawable(landmass);

            // Spawn other landmasses that were separated from original landmass
            for (IndexType i = 1; i < preservedLands.size(); i++)
            {
                auto&& [newShorelines, newInnerShorelines] = preservedLands[i];
                auto&& newLandmass = spawn<DLandmassMarker>(newShorelines, newInnerShorelines);
                landmassStageTools->addNode(0, newLandmass);
                newLandmass->forEachShoreline([&](auto& s, bool isInner)
                    {
                        s->setLandmass(newLandmass);
                        landmassStageTools->addNode(0, s);
                    });
            }
        }

        // generate init landmasses from changed squares (terarin only) that has not affected any landmass
        Generation::Data::get()->initializeQueuedMarkers();
        auto&& shorelinesPerLand = DShorelineMarker::generateInitBasicShorelines();
        for (auto&& shorelines : shorelinesPerLand)
        {
            auto&& landmass = spawn<DLandmassMarker>(shorelines, std::vector<QSharedPointer<DShorelineMarker>>{});
            for (auto&& shoreline : shorelines)
            {
                shoreline->setLandmass(landmass);
                landmassStageTools->addNode(0, shoreline);
            }

            landmassStageTools->addNode(0, landmass);
        }

        Generation::Data::get()->initializeQueuedMarkers();
        Generation::Data::get()->clearExactMarkers<DSeamassMarker>();
        DSeamassMarker::generateSeamassMarkers(Generation::Data::get()->getMarkers<DLandmassMarker>());


        if (auto&& dem = Generation::Data::get()->getDEM())
        {
            terrainModelStageTools->connectNodes();
            dem->reshapeGrid();
            terrainModelStageTools->disconnectNodes();
        }

        TODO("Update data after reshaping of terrain cells, old cell indexes need to be swaped with new cell indexes (no need to change other data)");
        // have to update all containers that uses cells
        //if (Generation::Data::get()->getTerrainCells())
        //{
        //    lithomapStageTools->disconnectNodes();
        //    Generation::StageGen<EGenerationStage::Lithomap>::reshapeTerrainCellsDiagram();
        //    lithomapStageTools->connectNodes();
        //}

        landmassStageTools->updatePipeline();
        cleanNodesState();
    }

    void StageTools<EGenerationStage::Layout>::addNode(size_t typeHash, QSharedPointer<Editable> object)
    {
        if (auto&& domain = object.dynamicCast<DDomain>(); domain)
        {
            if (domain->getType() == EDomainType::Terrain && !terrainDomainNodes.contains(domain->getGuid()))
                terrainDomainNodes[domain->getGuid()] = QSharedPointer<TerrainDomainNode>::create(domain);
            else if (domain->getType() == EDomainType::Water && !waterDomainNodes.contains(domain->getGuid()))
                waterDomainNodes[domain->getGuid()] = QSharedPointer<WaterDomainNode>::create(domain);
            else if (domain->getType() == EDomainType::Biome && !biomeDomainNodes.contains(domain->getGuid()))
                biomeDomainNodes[domain->getGuid()] = QSharedPointer<BiomeDomainNode>::create(domain);
        }
        else if (auto&& domainSquare = object.dynamicCast<DDomainSquare>(); domainSquare)
        {
            if (!domainSquareNodes.contains(domainSquare->getSquare()))
                domainSquareNodes[domainSquare->getSquare()] = QSharedPointer<DomainSquareNode>::create(domainSquare->getSquare());
            else if (!domainSquareNodes[domainSquare->getSquare()]->getSquare())
                domainSquareNodes[domainSquare->getSquare()]->setSquare(domainSquare->getSquare());
        }
    }

    void StageTools<EGenerationStage::Layout>::removeNode(QSharedPointer<Editable> object)
    {
        if (auto&& domain = object.dynamicCast<DDomain>(); domain)
        {
            if (domain->getType() == EDomainType::Terrain && terrainDomainNodes.contains(domain->getGuid()))
            {
                if (terrainDomainNodes[domain->getGuid()]->isCreatedOnCurrentStage())
                    terrainDomainNodes.erase(domain->getGuid());
                else
                {
                    if (!terrainDomainNodes[domain->getGuid()]->getSnapshot())
                        terrainDomainNodes[domain->getGuid()]->makeSnapshot();

                    terrainDomainNodes[domain->getGuid()]->nullifyDomain();
                }
            }
            else if (domain->getType() == EDomainType::Water && waterDomainNodes.contains(domain->getGuid()))
            {
                if (waterDomainNodes[domain->getGuid()]->isCreatedOnCurrentStage())
                    waterDomainNodes.erase(domain->getGuid());
                else
                {
                    if (!waterDomainNodes[domain->getGuid()]->getSnapshot())
                        waterDomainNodes[domain->getGuid()]->makeSnapshot();

                    waterDomainNodes[domain->getGuid()]->nullifyDomain();
                }
            }
            else if (domain->getType() == EDomainType::Biome && biomeDomainNodes.contains(domain->getGuid()))
                if (biomeDomainNodes[domain->getGuid()]->isCreatedOnCurrentStage())
                    biomeDomainNodes.erase(domain->getGuid());
                else
                {
                    if (!biomeDomainNodes[domain->getGuid()]->getSnapshot())
                        biomeDomainNodes[domain->getGuid()]->makeSnapshot();

                    biomeDomainNodes[domain->getGuid()]->nullifyDomain();
                }
        }
        else if (auto&& domainSquare = object.dynamicCast<DDomainSquare>(); domainSquare)
        {
            if (domainSquareNodes.contains(domainSquare->getSquare()))
            {
                if (domainSquareNodes[domainSquare->getSquare()]->isCreatedOnCurrentStage())
                    domainSquareNodes.erase(domainSquare->getSquare());
                else
                {
                    domainSquareNodes[domainSquare->getSquare()]->setModifiedOnCurrentStage(true);
                    domainSquareNodes[domainSquare->getSquare()]->nullifySquare();
                }
            }
        }
    }

    void StageTools<EGenerationStage::Layout>::modifyNode(QSharedPointer<Editable> object)
    {
        if (auto&& domain = object.dynamicCast<DDomain>(); domain)
        {
            if (domain->getType() == EDomainType::Terrain && terrainDomainNodes.contains(domain->getGuid()) && !terrainDomainNodes[domain->getGuid()]->isCreatedOnCurrentStage() && !terrainDomainNodes[domain->getGuid()]->getSnapshot())
                terrainDomainNodes[domain->getGuid()]->makeSnapshot();
            else if (domain->getType() == EDomainType::Water && waterDomainNodes.contains(domain->getGuid()) && !waterDomainNodes[domain->getGuid()]->isCreatedOnCurrentStage() && !waterDomainNodes[domain->getGuid()]->getSnapshot())
                waterDomainNodes[domain->getGuid()]->makeSnapshot();
            else if (domain->getType() == EDomainType::Biome && biomeDomainNodes.contains(domain->getGuid()) && !biomeDomainNodes[domain->getGuid()]->isCreatedOnCurrentStage() && !biomeDomainNodes[domain->getGuid()]->getSnapshot())
                biomeDomainNodes[domain->getGuid()]->makeSnapshot();
        }
    }

    void StageTools<EGenerationStage::Layout>::setupActions()
    {
        auto bindAction = [&](ELayoutAction id, const QString& label, auto lambda)
        {
            actions[id] = new QAction(QIcon(), label, this);
            connect(actions[id], &QAction::triggered, this, lambda);
        };

        bindAction(ELayoutAction::CreateTerrain, "Terrain", [&]() { createDomainFromSquares(EDomainType::Terrain, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), false); });
        bindAction(ELayoutAction::CreateBiome, "Biome", [&]() { createDomainFromSquares(EDomainType::Biome, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), false); });
        bindAction(ELayoutAction::CreateWater, "Water", [&]() { createDomainFromSquares(EDomainType::Water, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), false); });

        bindAction(ELayoutAction::ExtractTerrain, "Terrain", [&]() { createDomainFromSquares(EDomainType::Terrain, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), true); });
        bindAction(ELayoutAction::ExtractBiome, "Biome", [&]() { createDomainFromSquares(EDomainType::Biome, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), true); });
        bindAction(ELayoutAction::ExtractWater, "Water", [&]() { createDomainFromSquares(EDomainType::Water, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), true); });

        bindAction(ELayoutAction::AppendToDomain, "", [&]() { appendSquaresToDomain(DomainSelection::contextMenuDomain, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), false); });
        bindAction(ELayoutAction::ExtractIntoDomain, "", [&]() { appendSquaresToDomain(DomainSelection::contextMenuDomain, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>(), true); });
        bindAction(ELayoutAction::SubtractFromDomain, "", [&]() { subtractSquaresFromDomain(DomainSelection::contextMenuDomain, LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Grid>()); });

        bindAction(ELayoutAction::MergeDomains, "", [&]() { mergeDomains(DomainSelection::otherSelectedDomains, DomainSelection::contextMenuDomain); });

        bindAction(ELayoutAction::DeteleSelectedDomains, "Delete selected domains", [&]() 
            { 
                std::vector<QSharedPointer<DDomain>> domains;
                for (auto&& dh : LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Domain>())
                     domains << dh->getDomain().lock();

                deleteDomains(domains); 
            });
    }

    bool StageTools<EGenerationStage::Layout>::createDomainFromSquares(EDomainType type, const QSet<GPoint>& squares, bool extract, bool notify)
    {
        // Create and initialize objects
        auto newDomain = QSharedPointer<DDomain>::create();
        auto newDomainHandle = QSharedPointer<DDomainHandle>::create();
        newDomain->setType(type);
        newDomain->initialize();
        newDomainHandle->initialize();
        Generation::Data::get()->addDomain(newDomainHandle, newDomain);

        QSet<GPoint> hFinalSquares;
        QMap<qint64, QSet<GPoint>> hOwnershipMap;
        QString hDomainName;
        qint64 hDomainGuid;

        HISTORY_PUSH(createDomainFromSquares, type, {}, extract, notify);

        if (!HISTORY_LOAD4(hFinalSquares, hOwnershipMap, hDomainName, hDomainGuid))
        {
            hFinalSquares = squares;
            hDomainName = newDomain->getName();
            hDomainGuid = newDomain->getGuid();

            for (auto&& sq : squares)
            {
                if (auto&& domain = Generation::Data::get()->getDomainAtSquare(sq, type))
                    if (extract)
                        hOwnershipMap[domain->getGuid()].insert(sq);
                    else
                        hFinalSquares.remove(sq);
            }

            HISTORY_SAVE4(hFinalSquares, hOwnershipMap, hDomainName, hDomainGuid);
        }
        else
        {
            newDomain->setName(hDomainName);
            newDomain->setGuid(hDomainGuid);
        }

        // Check if new domain is not all redundant squares
        if (hFinalSquares.isEmpty())
        {
            OmniLog(ELoggingLevel::Warn) <<= "All selected squares are already owned! Domain creation aborted.";
            auto&& domains = Generation::Data::get()->getAllDomains();
            Generation::Data::get()->removeDomain(domains.size() - 1);
            HISTORY_ABORT_PUSH();
            return false;
        }

        // Substep #1
        // Steal ownership
        if (extract)
            for (auto&& kv = hOwnershipMap.keyValueBegin(); kv != hOwnershipMap.keyValueEnd(); ++kv)
                if (auto domain = Generation::Data::get()->findDomainByGuid((*kv).first))
                    subtractSquaresFromDomain(*domain, (*kv).second);

        // Substep #2
        createFlatTerrainFromSquares(hFinalSquares);

        // Substep #3
        newDomain->setSquares(hFinalSquares);
        newDomain->bindHandle(newDomainHandle);

        if (notify)
        {
            emit Editable::created(newDomain);
            emit Editable::created(newDomainHandle);
        }

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::createDomainFromSquares_Undo(EDomainType type, const QSet<GPoint>& /*unused*/, bool extract, bool notify)
    {
        HISTORY_POP();

        qint64 hDomainGuid;
        QSet<GPoint> hFinalSquares;
        QMap<qint64, QSet<GPoint>> hOwnershipMap;
        HISTORY_LOAD3(hDomainGuid, hFinalSquares, hOwnershipMap);

        // Undo substep #3
        auto&& domains = Generation::Data::get()->getAllDomains();
        for (int i = 0; i < domains.size(); ++i)
            if (domains[i].second->getGuid() == hDomainGuid)
            {
                Generation::Data::get()->removeDomain(i);
                break;
            }

        // Undo substep #2
        createFlatTerrainFromSquares_Undo({});

        // Undo substep #1
        // Restore ownership
        if (extract)
            for (auto it = hOwnershipMap.keyBegin(); it != hOwnershipMap.keyEnd(); ++it)
                subtractSquaresFromDomain_Undo(nullptr, {});

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::createFlatTerrainFromSquares(const QSet<GPoint>& squares)
    {
        HISTORY_PUSH(createFlatTerrainFromSquares, {});

        QSet<GPoint> hSelection;
        if (!HISTORY_LOAD(hSelection))
            hSelection = squares;

        QSet<GPoint> redundantSelection;
        Generation::Data::get()->createDomainSquares(hSelection);

        // Strip redundant selection
        while (redundantSelection.size())
        {
            hSelection.remove(*redundantSelection.begin());
            redundantSelection.remove(*redundantSelection.begin());
        }

        HISTORY_SAVE(hSelection);

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::createFlatTerrainFromSquares_Undo(const QSet<GPoint>& /*unused*/)
    {
        HISTORY_POP();
        QSet<GPoint> hSelection;
        HISTORY_LOAD(hSelection);

        Generation::Data::get()->clearDomainSquares(hSelection, false);

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::appendSquaresToDomain(QSharedPointer<DDomain> domain, const QSet<GPoint>& squares, bool extract)
    {
        qint64 hDomainGuid;
        QSet<GPoint> hSquaresAdded;
        QMap<qint64, QSet<GPoint>> hOwnershipMap;

        HISTORY_PUSH(appendSquaresToDomain, nullptr, {}, extract);

        if (!HISTORY_LOAD3(hDomainGuid, hSquaresAdded, hOwnershipMap))
        {
            hDomainGuid = domain->getGuid();
            hSquaresAdded = squares - domain->getSquares();

            for (auto&& sq : squares - domain->getSquares())
            {
                if (auto&& otherDomain = Generation::Data::get()->getDomainAtSquare(sq, domain->getType()))
                    if (extract)
                        hOwnershipMap[otherDomain->getGuid()].insert(sq);
                    else
                        hSquaresAdded.remove(sq);
            }

            HISTORY_SAVE3(hDomainGuid, hSquaresAdded, hOwnershipMap);
        }
        else
        {
            auto&& domains = Generation::Data::get()->getAllDomains();
            domain = (*std::find_if(domains.begin(), domains.end(), [&hDomainGuid](auto&& kv) { return kv.second->getGuid() == hDomainGuid; })).second;
        }

        // Substep #1
        // Steal ownership
        if (extract)
            for (auto&& kv = hOwnershipMap.keyValueBegin(); kv != hOwnershipMap.keyValueEnd(); ++kv)
                if (auto otherDomain = Generation::Data::get()->findDomainByGuid((*kv).first))
                    subtractSquaresFromDomain(*otherDomain, { (*kv).second });

        // Substep #2
        // Create new terrain
        createFlatTerrainFromSquares(hSquaresAdded);

        // Substep #3
        // Add squares
        domain->setSquares(domain->getSquares() + hSquaresAdded);
        domain->getHandle().lock()->update();

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::appendSquaresToDomain_Undo(QSharedPointer<DDomain> /*unused*/, const QSet<GPoint>& /*unused*/, bool extract)
    {
        HISTORY_POP();
        QSet<GPoint> hSquaresAdded;
        qint64 hDomainGuid;
        QMap<qint64, QSet<GPoint>> hOwnershipMap;
        HISTORY_LOAD3(hDomainGuid, hSquaresAdded, hOwnershipMap);

        auto&& domains = Generation::Data::get()->getAllDomains();
        auto domain = (*std::find_if(domains.begin(), domains.end(), [&hDomainGuid](auto&& kv) { return kv.second->getGuid() == hDomainGuid; })).second;

        //Undo substep #3
        domain->setSquares(domain->getSquares() - hSquaresAdded);
        domain->getHandle().lock()->update();

        //Undo substep #2
        createFlatTerrainFromSquares_Undo({});

        //Undo substep #1
        if (extract)
            for (auto&& kv = hOwnershipMap.keyValueBegin(); kv != hOwnershipMap.keyValueEnd(); ++kv)
                subtractSquaresFromDomain_Undo(nullptr, {});

        LayoutSelectionMgr::get()->setSelection<ELayoutSelection::Grid>(hSquaresAdded);

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::subtractSquaresFromDomain(QSharedPointer<DDomain> domain, const QSet<GPoint>& squares)
    {
        QString hDomainName;
        QSet<GPoint> hSquaresRemoved;
        QString hSubtractDomainsRemoved;

        HISTORY_PUSH(subtractSquaresFromDomain, nullptr, {});

        if (!HISTORY_LOAD2(hDomainName, hSquaresRemoved))
        {
            hDomainName = domain->getName();
            hSquaresRemoved = squares & domain->getSquares();
            HISTORY_SAVE2(hDomainName, hSquaresRemoved);
        }
        else
        {
            auto&& domains = Generation::Data::get()->getAllDomains();
            domain = (*std::find_if(domains.begin(), domains.end(), [&hDomainName](auto&& kv) { return kv.second->getName() == hDomainName; })).second;
        }

        // Subtract squares
        HISTORY_LOAD(hSubtractDomainsRemoved);

        //if subtracting would leave an empty domain, delete it instead 
        if (domain->getSquares() == hSquaresRemoved)
        {
            hSubtractDomainsRemoved = domain->getName();
            deleteDomains({ domain });
        }
        else
        {
            domain->setSquares(domain->getSquares() - hSquaresRemoved);
            domain->getHandle().lock()->update();

            // Clear orphaned terrain
            Generation::Data::get()->clearDomainSquares(hSquaresRemoved, false);
        }
        HISTORY_SAVE(hSubtractDomainsRemoved);

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::subtractSquaresFromDomain_Undo(QSharedPointer<DDomain> /*unused*/, const QSet<GPoint>& /*unused*/)
    {
        HISTORY_POP()
            QString hDomainName;
        QSet<GPoint> hSquaresRemoved;
        HISTORY_LOAD2(hDomainName, hSquaresRemoved);

        QString hSubtractDomainsRemoved;
        HISTORY_LOAD(hSubtractDomainsRemoved);

        if (hSubtractDomainsRemoved == hDomainName)
        {
            deleteDomains_Undo({});
        }
        else
        {
            auto&& domains = Generation::Data::get()->getAllDomains();
            auto domain = (*std::find_if(domains.begin(), domains.end(), [&hDomainName](auto&& kv) { return kv.second->getName() == hDomainName; })).second;

            LayoutSelectionMgr::get()->setSelection<ELayoutSelection::Grid>(hSquaresRemoved);

            appendSquaresToDomain(domain, hSquaresRemoved, false);
        }

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::mergeDomains(const std::vector<QSharedPointer<DDomain>>& source, QSharedPointer<DDomain> target)
    {
        HISTORY_PUSH(mergeDomains, {}, nullptr);
        QSet<GPoint> sourceSquares;
        for (auto&& srcDomain : source)
            sourceSquares += srcDomain->getSquares();

        bool result = true;
        result &= appendSquaresToDomain(target, sourceSquares, true);

        return result;
    }

    bool StageTools<EGenerationStage::Layout>::mergeDomains_Undo(const std::vector<QSharedPointer<DDomain>>& source, QSharedPointer<DDomain> target)
    {
        HISTORY_POP();

        bool result = true;
        result &= appendSquaresToDomain_Undo(nullptr, {}, true);
        return result;
    }

    bool StageTools<EGenerationStage::Layout>::deleteDomains(std::vector<QSharedPointer<DDomain>> domainsToDelete)
    {
        std::vector<std::tuple<DDomainHandle, DDomain, int>> hData;

        HISTORY_PUSH(deleteDomains, {});

        if (!HISTORY_LOAD(hData))
        {
            // Save Domain data
            for (int i = 0; i < domainsToDelete.size(); ++i)
            {
                EDomainTypeConstexpr::UseIn<EAC::SaveDomainDataToHistory>(domainsToDelete[i]->getType(), domainsToDelete[i]->getData(), i);
                hData.push_back(std::make_tuple(*domainsToDelete[i]->getHandle().lock(), *domainsToDelete[i], i));
            }

            HISTORY_SAVE(hData);
        }
        else
        {
            // Load domain pointers
            std::vector<QSharedPointer<DDomain>> loadedDomainsToDelete;

            for (auto&& data : hData)
                if (auto dptr = Generation::Data::get()->findDomainByGuid(std::get<1>(data).getGuid()))
                    loadedDomainsToDelete << *dptr;

            domainsToDelete = loadedDomainsToDelete;
        }

        for (auto&& domain : domainsToDelete)
        {
            auto squares = domain->getSquares();

            // Remove domain
            QSharedPointer<DDomainHandle> dh = domain->getHandle().lock();
            Generation::Data::get()->removeDomain(dh);

            // Remove orphaned terrain
            Generation::Data::get()->clearDomainSquares(squares, false);
        }

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::deleteDomains_Undo(std::vector<QSharedPointer<DDomain>> /*unused*/)
    {
        HISTORY_POP()
            std::vector<std::tuple<DDomainHandle, DDomain, int>> hData;
        HISTORY_LOAD(hData);

        // Recreate domains
        for (auto&& data : hData)
        {
            LayoutSelectionMgr::get()->setSelection<ELayoutSelection::Grid>(std::get<1>(data).getSquares());

            // Restore and rebind
            createDomainFromSquares(std::get<1>(data).getType(), std::get<1>(data).getSquares(), true, false);
            Generation::Data::get()->restoreDomain(data);
        }

        return true;
    }

    void StageTools<EGenerationStage::Layout>::selectNewDomain(size_t typeHash, QSharedPointer<Editable> object)
    {
        auto domain = object.dynamicCast<DDomain>();
        if (!domain)
            return;

        LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Domain>({ domain->getHandle().lock() });
    }

    void StageTools<EGenerationStage::Layout>::updateTreeViewSelection()
    {
        blockSignals(true);
        treeModel.clearSelection();

        for (auto&& dh : LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Domain>())
            treeModel.selectDomain(dh->getDomain().lock()->getGuid());

        blockSignals(false);
    }

    bool StageTools<EGenerationStage::Layout>::dynamicPaintingClick(QMouseEvent* event)
    {
        if (paintingOption == EOmnigenPainting::Append)
        {
            for (auto&& dh : LayoutSelectionMgr::get()->getSelection<ELayoutSelection::Domain>())
                if (auto domain = dh->getDomain().lock(); domain->getType() == domainTypeToPaint)
                {
                    paintingTarget = domain;
                    break;
                }
        }

        LayoutSelectionMgr::get()->clearSelection();

        paintingPreview = QSharedPointer<DDomainPaintingPreview>::create();
        paintingPreview->initialize();
        Editable::created(paintingPreview);

        // Update immediately.
        dynamicPaintingMove(event);

        return true;
    }

    bool StageTools<EGenerationStage::Layout>::dynamicPaintingMove(QMouseEvent* event)
    {
        QMap<ELayoutSelection, std::any> objects;
        GridSelection::findOnScene(&objects, event->x(), event->y(), QOmnigenViewportSection::getActiveViewport()->getSelectionData());
        if (objects.contains(ELayoutSelection::Grid))
            paintingPreview->update({ std::any_cast<GPoint>(objects[ELayoutSelection::Grid]) });

        return true;
    }

    void StageTools<EGenerationStage::Layout>::dynamicPaintingEnd()
    {
        auto paintMode = getPaintOption();

        if ((paintMode == EOmnigenPainting::New) || (paintMode == EOmnigenPainting::Extract))
        {
            createDomainFromSquares(getPaintType(), paintingPreview->getSquares(), getPaintOption() == EOmnigenPainting::Extract);
        }
        else if (paintMode == EOmnigenPainting::Append)
        {
            if (paintingTarget.isNull())
                createDomainFromSquares(getPaintType(), paintingPreview->getSquares(), false);
            else
                appendSquaresToDomain(paintingTarget, paintingPreview->getSquares(), false);
        }
        else if (paintMode == EOmnigenPainting::Subtract)
        {
            QMap<qint64, QSet<GPoint>> data;

            auto&& domains = Generation::Data::get()->getAllDomains();
            for (auto&& [handle, domain] : domains)
                if (domain->getType() == getPaintType())
                    if (auto sharedSquares = paintingPreview->getSquares() & domain->getSquares(); !sharedSquares.isEmpty())
                        data[domain->getGuid()] = sharedSquares;

            for (auto it = data.keyValueBegin(); it != data.keyValueEnd(); ++it)
                subtractSquaresFromDomain(*Generation::Data::get()->findDomainByGuid((*it).first), (*it).second);
        }

        Editable::aboutToBeDeleted(paintingPreview);
        paintingPreview.clear();
        paintingTarget = {};
    }

    bool StageTools<EGenerationStage::Layout>::eventFilter(QObject* obj, QEvent* event)
    {
        QMouseEvent* mEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mEvent)
            return false;

        if (mEvent->type() == QEvent::MouseButtonPress)
        {
            if (mEvent->button() == Qt::LeftButton)
            {
                if (bPainting)
                {
                    bPaintingSessionActive = true;
                    dynamicPaintingClick(mEvent);
                    emit Omnigen::get()->toggleShortcut(false);
                }
            }
        }
        else if (mEvent->type() == QEvent::MouseMove)
        {
            if (mEvent->buttons().testFlag(Qt::LeftButton))
            {
                if (bPaintingSessionActive)
                {
                    dynamicPaintingMove(mEvent);
                    return true;
                }
            }
        }
        else if (mEvent->type() == QEvent::MouseButtonRelease)
        {
            if (bPaintingSessionActive)
            {
                bPaintingSessionActive = false;
                dynamicPaintingEnd();
                return true;
            }

            if (mEvent->button() == Qt::RightButton)
            {
                if (!QApplication::overrideCursor())
                {
                    LayoutSelectionMgr::get()->rightClick(mEvent);
                }
            }
        }

        return false;
    }
}