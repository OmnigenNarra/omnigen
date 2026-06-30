#pragma once
#include "Scene/Generation/OmnigenGenerationStage.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "Editor/Sections/History/History.h"
#include "Utils/Event.h"

class QShortcut;

// There exists exactly one StageTools instance for each Stage
// Tools are responsible for user actions, saving and loading

namespace Design
{
    class SelectionMgrBase;

    // Interface for all Stage Tools
    class StageToolsBase : public QObject
    {
        Q_OBJECT

    public:
        StageToolsBase() = default;

        virtual SelectionMgrBase* getSelectionMgr() const;

        // MUST CALL THESE IN THE DERIVED CLASSES!
        virtual void bind();
        virtual void unbind();

        virtual void save(OmniBin<std::ios::out>& writer) const = 0;
        virtual void load(OmniBin<std::ios::in>& reader) = 0;

        // connect/disconnect signals for nodes
        virtual void connectNodes() {};
        void disconnectNodes();

        virtual void aboutToEnterStage(int dir) {};
        virtual void aboutToExitStage(int dir) {};

        virtual void clearNodes() {};

        virtual void loadSnapshotData() {};
        virtual bool validatePipeline() { return true; };
        virtual void updatePipeline() { };

        virtual void clearHistory();

    protected:

        // Qt connections
        std::vector<QMetaObject::Connection> qConnections;
        // Universal connections
        std::vector<Connection> gConnections;

        // Qt connections for nodes
        std::vector<Connection> qNodesConnections;

        std::vector<QShortcut*> shortcuts;

        // Each stage has it's own History Stack (is this a good design?)
        HistoryContext historyContext;
    };

    template<EGenerationStage GS>
    class StageTools : public StageToolsBase
    {
    public:
        virtual void save(OmniBin<std::ios::out>& writer) const override {};
        virtual void load(OmniBin<std::ios::in>& reader) override {};
    };
}