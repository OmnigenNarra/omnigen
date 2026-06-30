#pragma once
#include "../StageToolsBase.h"
#include "Nodes/IsohypseNode.h"
#include "ContourLinesWidgets.h"

namespace Design
{
    template<>
    class StageTools<EGenerationStage::ContourLines> final : public StageToolsBase
    {
    public:
        StageTools();

        virtual SelectionMgrBase* getSelectionMgr() const override;

        virtual void bind() override;
        virtual void unbind() override;

        virtual void save(OmniBin<std::ios::out>& writer) const override;
        virtual void load(OmniBin<std::ios::in>& reader) override;

        virtual void connectNodes() override;

        virtual void aboutToEnterStage(int dir) override;
        virtual void aboutToExitStage(int dir) override;

        void clearNodes() override;
        void cleanNodesState();
        void updateParentNodes();

        void loadSnapshotData() override;
        virtual bool validatePipeline() override;
        virtual void updatePipeline() override;

        const auto& getIsohypseNodes() const { return isohypseNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        void setPeakPoints(const std::vector<QVector3D>& peaks);

        bool ignoreModifiedNodes = false;
        std::unordered_map<qint64, QSharedPointer<IsohypseNode>> isohypseNodes;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

    private:
        QWidget* createOutlineToolbar();

        std::vector<QVector3D> peakPoints;
        std::vector<qint64> peakMarkers;
    };
}
