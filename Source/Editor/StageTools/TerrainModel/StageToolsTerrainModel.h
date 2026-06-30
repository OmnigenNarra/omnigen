#pragma once
#include "Editable.h"
#include "../StageToolsBase.h"
#include "Nodes/DEMPointNode.h"
#include <variant>

class DLineMarker;

namespace Generation
{
    class DEM;
}

namespace Design
{
    template<>
    class StageTools<EGenerationStage::TerrainModel> final : public StageToolsBase
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
        bool validatePipeline() override;
        void updatePipeline() override;

        const auto& getDEMPointNodes() const { return demPointNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

        std::optional<std::array<GPoint, 4>> findDemQuadUnderCursor();

    private:
        QWidget* createOutlineToolbar();

        bool eventFilter(QObject* obj, QEvent* event);

        // Editor controls
        void editPoints();
        void sculpt();
        void smooth();
        void flatten();
        std::vector<GPoint> findPointsUnderBrush();
        void updateBrush();
        void clearBrush();

        // Undo/Redo logic
        void updatePointsHeight(Generation::DEM* dem, const std::unordered_map<IndexType, float>& pointsToUpdate);
        void updatePointsNormal(Generation::DEM* dem, const std::unordered_map<IndexType, QVector3D>& pointsToUpdate);
        void computePointsNormal(Generation::DEM* dem, std::unordered_map<IndexType, QVector3D>* pointsToUpdate, int px, int pz);
        bool registerDemChange(const std::unordered_map<IndexType, float>& changeMap);
        bool registerDemChange_Undo(const std::unordered_map<IndexType, float>& changeMap);

        std::optional<GPoint> brushOrigin;
        QSharedPointer<DLineMarker> brushMarker = nullptr;
        int brushSize = 5;
        int brushStrength = 5;

        QTimer ticker;

        // Undo/Redo data
        std::unordered_map<IndexType, float> pointModification;

        Generation::DEM* getDem() const;

        enum class EToolMode
        {
            None,
            Sculpt,
            Flatten,
            Smooth
        };
        EToolMode toolMode = EToolMode::None;

        // Stage Nodes
        std::unordered_map<GVector2D, QSharedPointer<DEMPointNode>> demPointNodes;
    };
}
