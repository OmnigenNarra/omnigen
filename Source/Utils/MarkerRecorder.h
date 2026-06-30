#pragma once
#include "Utils/Event.h"
#include "Scene/Generation/Common/Markers/MarkerDrawable.h"
#include "Editable.h"

struct MarkerRecorder
{
    MarkerRecorder()
        : recorderConnection(Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &MarkerRecorder::recordMarker))
    {}

    std::map<size_t, std::vector<qint64>> guidMap;

private:
    void recordMarker(size_t typeHash, QSharedPointer<Editable> e)
    {
        if (auto* marker = dynamic_cast<DMarker*>(e.get()); marker)
            guidMap[typeHash] << marker->getGuid();
    }

    ScopedConnection recorderConnection;
};