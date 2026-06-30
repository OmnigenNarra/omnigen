#include "stdafx.h"
#include "StageToolsUrbanLayout.h"

#include "UrbanLayoutSelection.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

namespace Design
{
    SelectionMgrBase* StageTools<EGenerationStage::UrbanLayout>::getSelectionMgr() const
    {
        return UrbanLayoutSelectionMgr::get();
    }

    void StageTools<EGenerationStage::UrbanLayout>::bind()
    {
        StageToolsBase::bind();

        auto* omnigen = Omnigen::get();
        auto* outline = omnigen->getOutline();
        auto* toolbar = createOutlineToolbar();
        auto* properties = omnigen->getProperties();


        treeView = new OutlineTreeView;
        treeView->setModel(&treeModel);
        treeModel.setTreeView(treeView);
        treeModel.loadSuggestions();
        treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        treeView->setUniformRowHeights(true);

        outline->applyTreeStyle(treeView);
        outline->fillSection({ toolbar, treeView });

        treeView->show();

        auto* selMgr = UrbanLayoutSelectionMgr::get();
        qConnections << connect(selMgr, &SelectionMgrBase::selectionChanged, this, [this, selMgr, properties]()
            {
                updateTreeViewSelection();
                QOmnigenViewportSection::repaintAll();
            });

        // Viewport events
        for (auto&& viewport : omnigen->getAllViewports())
        {
            viewport->installEventFilter(this);
            viewport->setMouseTracking(true);
        }
    }

    void StageTools<EGenerationStage::UrbanLayout>::unbind()
    {
        StageToolsBase::unbind();

        for (auto&& viewport : Omnigen::get()->getAllViewports())
        {
            viewport->removeEventFilter(this);
            viewport->setMouseTracking(false);
        }

        treeModel.clear();
    }

    void StageTools<EGenerationStage::UrbanLayout>::updateTreeViewSelection()
    {
        blockSignals(true);
        treeModel.clearSelection();

        for (auto&& suggestion : UrbanLayoutSelectionMgr::get()->getSelection<EUrbanLayoutSelection::SuggestionHandle>())
            treeModel.selectSuggestion(suggestion->ownedSuggestion.lock()->getGuid());

        blockSignals(false);
    }

    void StageTools<EGenerationStage::UrbanLayout>::save(OmniBin<std::ios::out>& writer) const
    {
        auto&& genData = Generation::Data::get();
        writer << genData->getUrbanSuggestions();
    }

    void StageTools<EGenerationStage::UrbanLayout>::load(OmniBin<std::ios::in>& reader)
    {
        auto&& genData = Generation::Data::get();

        std::vector<QSharedPointer<Generation::UrbanSuggestion>> suggestions;
        size_t sCount;
        reader >> sCount;
        suggestions.resize(sCount);

        for (auto&& suggestion : suggestions)
        {
            reader >> suggestion;
        }

        genData->setUrbanSuggestions(suggestions);
        for (auto&& suggestion : genData->getUrbanSuggestions())
        {
            suggestion->initializeHandle();
        }
    }
}
