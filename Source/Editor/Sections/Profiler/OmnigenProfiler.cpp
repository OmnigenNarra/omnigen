#include "stdafx.h"
#include "OmnigenProfiler.h"
#include "Omnigen.h"

OmnigenProfilerSegment::OmnigenProfilerSegment(const QString& entryName, bool profileMultiThread, EGenerationStage generationStage)
    : entryId(entryName)
    , entryText(entryName)
    , threadId(std::this_thread::get_id())
{
    if (!OmnigenProfiler::get()->isProfiling())
        return;

    if (generationStage == EGenerationStage::Last)
        entrySection = *Generation::Data::get()->getStageBeingGenerated();
    else
        entrySection = static_cast<EGenerationStage>(generationStage);

    // Multi thread profiling
    auto lastEntry = OmnigenProfiler::get()->getLastThreadEntry(threadId);
    if (profileMultiThread || !lastEntry.isEmpty())
    {
        multiThreadedEntry = true;

        // Entry ID is entry name + parentId
        if (!lastEntry.isEmpty())
            // For continuous MT entries
            entryId += lastEntry;
        else if (OmnigenProfiler::get()->getEntryStack().size() > 0)
            // For first MT entry
            entryId += OmnigenProfiler::get()->getEntryStack().back();
        else
            // For first entry overall
            entryId += toQString(static_cast<int>(*Generation::Data::get()->getStageBeingGenerated()));

        entryTimeStamp = std::chrono::steady_clock::now();
        OmnigenProfiler::get()->appendThreadStack(threadId, entryId);
        return;
    }
    else if (OmnigenProfiler::get()->getMainThread() != threadId)
    {
        // Ignore entries from other threads if not multi thread profiling

        //OmnigenProfiler::get()->sendMultiThreadLogMessage(entryName);         // For debug purposes
        return;
    }

    // Non Multi thread entries
    if (OmnigenProfiler::get()->getEntryStack().empty())
        // To ensure uniqueness of names per section
        entryId += toQString(static_cast<int>(*Generation::Data::get()->getStageBeingGenerated()));
    else
        entryId += OmnigenProfiler::get()->getEntryStack().back();

    OmnigenProfiler::get()->appendEntryStack(entryId);
    entryTimeStamp = std::chrono::steady_clock::now();
}

OmnigenProfilerSegment::~OmnigenProfilerSegment()
{
    if (!OmnigenProfiler::get()->isProfiling())
        return;

    using namespace std::chrono;
    auto now = steady_clock::now();
    operationTime = duration_cast<nanoseconds>(now - entryTimeStamp).count();
    auto threadId = std::this_thread::get_id();

    // Multi threading
    auto lastEntry = OmnigenProfiler::get()->getLastThreadEntry(threadId);
    if (multiThreadedEntry || !lastEntry.isEmpty())
    {
        OmnigenProfiler::get()->appendMultiThreadData(entryId, entryText, operationTime, threadId);
        return;
    }
    else if(OmnigenProfiler::get()->getMainThread() != threadId)
        return;

    int idx = indexOf(OmnigenProfiler::get()->getEntryStack(), entryId);
    OmnigenProfiler::get()->popEntryStack();

    // idx 0 are for main sections (Ridges, Rivers, Lithomap etc)
    if (idx == 0)
    {
        OmnigenProfiler::get()->gatherData(entrySection, {operationTime, entryText, entryId});
    }
    else
    {
        auto* gatheredData = OmnigenProfiler::get()->getGatheredData();
        auto& entryParent = OmnigenProfiler::get()->getEntryStack().back();

        for (int i = 0; i < (*gatheredData)[idx][entryParent].size(); i++)
        {
            if ((*gatheredData)[idx][entryParent][i].checkId(entryId))
            {
                (*gatheredData)[idx][entryParent][i].appendTime(operationTime);
                return;
            }
        }

        OmnigenProfiler::get()->gatherData(entryParent, { operationTime, entryText, entryId }, idx);
    }
}

OmnigenProfilerStart::OmnigenProfilerStart()
{
    OmnigenProfiler::get()->setMainThread(std::this_thread::get_id());
    Omnigen::get()->getProfiler()->clearProfiler();
    entryTimeStamp = std::chrono::steady_clock::now();
    OmnigenProfiler::get()->setIsProfiling(true);
}

OmnigenProfilerStart::~OmnigenProfilerStart()
{
    OmnigenProfiler::get()->setIsProfiling(false);

    using namespace std::chrono;

    auto now = steady_clock::now();
    generationTime = duration_cast<nanoseconds>(now - entryTimeStamp).count();

    Omnigen::get()->getProfiler()->calculateSectionTimes(generationTime);

    Omnigen::get()->getProfiler()->populateProfiler();
    OmnigenProfiler::get()->clearData();
}
