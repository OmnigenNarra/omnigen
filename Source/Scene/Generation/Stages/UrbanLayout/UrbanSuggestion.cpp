#include "stdafx.h"
#include "UrbanSuggestion.h"


#include "UrbanPlanner.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/Sections/Profiler/OmnigenProfilerSection.h"
#include "Editor/StageTools/UrbanLayout/UrbanHandleDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"

namespace Generation
{
    UrbanSuggestion::UrbanSuggestion(int inId)
        : initialCellId(inId)
    {
        guid = makeGuid();

        auto&& clusterMap = Data::get()->getTerrainClustersMap();
        auto&& cellPoly = Data::get()->getTerrainCells()->getCellAt(initialCellId);

        if (auto potentialPts = clusterMap[initialCellId]->raycastDataFrom2D(cellPoly->getCenter()); !potentialPts.empty())
        {
            cachedVertex = potentialPts[0];
        }
        else
        {
            cachedVertex = QVector3D(cellPoly->getCenter().x, 20.f, cellPoly->getCenter().z);
        }
    }

    UrbanSuggestion::~UrbanSuggestion()
    {
        showHandle(false);
    }

    void UrbanSuggestion::initializeHandle()
    {
        handle = QSharedPointer<DUrbanHandle>::create(getCenterPoint3D());
        handle->ownedSuggestion = sharedFromThis();
        handle->initialize();
        handle->update();

        QOmnigenViewport::registerDrawable(handle);
    }

    void UrbanSuggestion::showHandle(const bool bShow) const
    {
        if (handle.isNull())
            return;

        if (bShow)
            QOmnigenViewport::registerDrawable(handle);
        else
            QOmnigenViewport::unregisterDrawable(handle);
    }

    void UrbanSuggestion::generateSuggestions()
    {
        OmniProfile("Urban Sites: Suggestions");

        std::vector<QSharedPointer<UrbanSuggestion>> suggestions;
        const auto planner = std::make_unique<UrbanPlanner>();
        planner->calculateSuggestions();
        auto&& sugg = planner->getSuggestions(3, true);

        int i = 0;
        for (auto&& entry : sugg)
        {
            const auto newSuggestion = QSharedPointer<UrbanSuggestion>::create(entry.cellId);
            newSuggestion->setMaxAreaSize(entry.maxAreaSize);
            newSuggestion->initializeHandle();
            newSuggestion->setName("Town " + QString::number(++i));

            suggestions.push_back(newSuggestion);
        }

        Data::get()->setUrbanSuggestions(suggestions);
    }

    void UrbanSuggestion::setMaxAreaSize(const EUrbanSize& val)
    {
        if (val < areaSize)
            areaSize = val;

        maxAreaSize = val;
    }

    void UrbanSuggestion::setName(const QString& inName)
    {
        name = inName;
    }

    QVector3D UrbanSuggestion::getCenterPoint3D() const
    {
        return cachedVertex;
    }
}
