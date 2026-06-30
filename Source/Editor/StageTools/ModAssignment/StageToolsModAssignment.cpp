#include "stdafx.h"
#include "StageToolsModAssignment.h"
#include "ModTools.h"
#include "Scene/Generation/Stages/TerrainMods/TerrainMod.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverMarker.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverNurbsMarker.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverSurfaceMarker.h"
#include "Editor/StageTools/StageTools.h"

namespace Design
{
    StageTools<EGenerationStage::ModAssignment>::StageTools()
        : StageToolsBase()
    {
    }

    void StageTools<EGenerationStage::ModAssignment>::loadTreeViewEntries()
    {
        Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->loadTreeViewModEntries();
    }

    void StageTools<EGenerationStage::ModAssignment>::onSelectionChanged()
    {
        if(boundModTools)
            Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->onModSelectionChanged();
    }

    void StageTools<EGenerationStage::ModAssignment>::addEntry(size_t typeHash, QSharedPointer<Editable> object)
    {
        Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->addModEntry(typeHash, object);
    }

    void StageTools<EGenerationStage::ModAssignment>::removeEntry(QSharedPointer<Editable> object)
    {
        Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->removeModEntry(object);
    }

    void StageTools<EGenerationStage::ModAssignment>::updateEntry(QSharedPointer<Editable> object, bool reset)
    {
        Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->updateModEntry(object, reset);
    }

    SelectionMgrBase* StageTools<EGenerationStage::ModAssignment>::getSelectionMgr() const
    {
        return ModSelectionMgr::get();
    }

    void StageTools<EGenerationStage::ModAssignment>::bind()
    {
        StageToolsBase::bind();

        auto* outline = Omnigen::get()->getOutline();
        createModTools();

        treeView = new OutlineTreeView;
        outline->applyTreeStyle(treeView);
        treeView->show();

        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &StageTools<EGenerationStage::ModAssignment>::addEntry);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::Modified>(this, &StageTools<EGenerationStage::ModAssignment>::updateEntry);
        gConnections <<= Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &StageTools<EGenerationStage::ModAssignment>::removeEntry);

        auto* selMgr = Design::ModSelectionMgr::get();
        qConnections << connect(selMgr, &SelectionMgrBase::selectionChanged, this, &StageTools<EGenerationStage::ModAssignment>::onSelectionChanged);

        outline->fillSection({ createToolbar(), treeView });
    }

    void StageTools<EGenerationStage::ModAssignment>::unbind()
    {
        StageToolsBase::unbind();

        if (boundModTools)
        {
            modTools[*boundModTools]->hide();
            Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->unbind();
        }
    }

    void StageTools<EGenerationStage::ModAssignment>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();
        
        //Rivers
        genData->saveMarkers<DRiverMarker>(writer);
        genData->saveMarkers<DRiverNurbsMarker>(writer);
        genData->saveMarkers<DRiverSurfaceMarker>(writer);

        // Mods
        auto&& mods = genData->getTerrainMods();
        writer << mods.size();

        for (auto modIt = mods.keyValueBegin(); modIt != mods.keyValueEnd(); ++modIt)
        {
            auto&& [type, mods] = *modIt;
            writer << type;

            Generation::ETerrainModConstexpr::UseIn<EAC::SaveTerrainMods>(type, mods, writer);
        }
    }

    void StageTools<EGenerationStage::ModAssignment>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();

        //Rivers
        genData->loadMarkers<DRiverMarker>(reader);
        genData->loadMarkers<DRiverNurbsMarker>(reader);
        genData->loadMarkers<DRiverSurfaceMarker>(reader);

        // Load River pointers
        auto&& riversUncasted = genData->getExactMarkersFast<DRiverMarker>();
        for (auto&& marker : riversUncasted)
        {
            auto* river = static_cast<DRiverMarker*>(marker.get());
            for (auto&& infInfo : river->getInfluents())
                if (infInfo.riverGuid != -1)
                    const_cast<InfluentInfo&>(infInfo).river = genData->findMarkerByGuid<DRiverMarker>(infInfo.riverGuid);
        }

        // Mods
        QMap<Generation::ETerrainMod, std::vector<QSharedPointer<Generation::TerrainModBase>>> mods;

        decltype(mods.size()) modsCount;
        reader >> modsCount;

        for (size_t i=0; i<modsCount; ++i)
        {
            Generation::ETerrainMod type;
            reader >> type;

            mods[type] = Generation::ETerrainModConstexpr::UseIn<EAC::LoadTerrainMods>(type, reader);
        }

        genData->setTerrainMods(mods);

        for (auto&& modVec : mods)
            for (auto&& mod : modVec)
                Generation::ETerrainModConstexpr::UseIn<EAC::PostLoadTerrainMod>(mod->getType(), mod.get());
    }

    QWidget* StageTools<EGenerationStage::ModAssignment>::createToolbar()
    {
        auto* host = new QWidget;
        host->setLayout(new QVBoxLayout());

        auto* toolBar = new QToolBar();
        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        host->layout()->addWidget(toolBar);

        auto* modGroup = new QActionGroup(toolBar);
        modGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        for (int i = 0; i<int(Generation::ETerrainMod::Last); ++i)
        {
            auto* useModTools = new QAction(QIcon("Resources/Icons/downarrow.png"), toQString(std::string(magic_enum::enum_name(Generation::ETerrainMod(i)))), modGroup);

            connect(useModTools, &QAction::triggered, this, [this, i]()
                {
                    if (boundModTools && *boundModTools != i)
                    {
                        getSelectionMgr()->clearSelection();
                        modTools[*boundModTools]->hide();
                        Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->unbind();
                    }

                    Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(i))->bind();
                    treeModel = Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(i))->getTreeModel();
                    treeView->setModel(treeModel);
                    
                    modTools[i]->show();
                    boundModTools = i;
                    Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(*boundModTools))->setModToolTreeView(treeView);
                });

            toolBar->addAction(useModTools);

            host->layout()->addWidget(modTools[i]);
            modTools[i]->hide();
        }

        return host;
    }

    void StageTools<EGenerationStage::ModAssignment>::createModTools()
    {
        for (int i = 0; i<int(Generation::ETerrainMod::Last); ++i)
            modTools[i] = Generation::ETerrainModConstexpr::UseIn<EAC::GetModTools>(Generation::ETerrainMod(i))->create();
    }
}