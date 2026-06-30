#pragma once

#include "Editor/Sections/Profiler/OmnigenProfilerSection.h"
#include "Scene/Generation/OmnigenGenerationStage.h"

// The Profiler auto deduces entry stage as long as no other stage is specified (and as such 'stage' parameter is optional)
// Forcing a stage only works on the first entry of a chain (the entry whose parent in the Tree View is a section) other entries down the chain will follow
// By default all times are calculated for the main thread, even entires inside TBB
// Setting profileMultiThread to 'true' for an entry inside TBB will save all times from all threads, and show the worst time
// All other entries down the chain will automatically be assumed as multi threaded and profiled as such (no need to set true for them)
// Setting 'true' outside of TBB might results in ill entry structure (the times will be correct, but the % and callstack no, if there will be TBB down the chain)
#define OmniProfile OmnigenProfilerSegment scopeProfiling
#define OmniStartProfiling OmnigenProfilerStart profilingStart

class OmnigenProfilerSegment
{
public:
    OmnigenProfilerSegment(const QString& entryName, bool profileMultiThread = false, EGenerationStage stage = EGenerationStage::Last);
    ~OmnigenProfilerSegment();

private:
    EGenerationStage entrySection = EGenerationStage::Last;
    double operationTime = 0.0f;
    QString entryText = "";
    QString entryId = "";
    bool multiThreadedEntry = false;

    std::chrono::time_point<std::chrono::steady_clock> entryTimeStamp;

    std::thread::id threadId;
};

class OmnigenProfilerStart
{
public:
    OmnigenProfilerStart();
    ~OmnigenProfilerStart();

private:
    double generationTime = 0.0f;
    std::chrono::time_point<std::chrono::steady_clock> entryTimeStamp;
};
