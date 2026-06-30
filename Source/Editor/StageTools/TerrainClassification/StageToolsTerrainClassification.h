#pragma once
#include "../StageToolsBase.h"
#include "TerrainClassificationWidgets.h"

class DLineMarker;

namespace Design
{
    template<>
    class StageTools<EGenerationStage::TerrainClassification> final : public StageToolsBase
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

        void modifyNode(QSharedPointer<Editable> object);

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

        // Creates markers based on voronoi cells, with colors representing block type associated during stage autoGen
        void visualizeBlockTypes();

    private:
        QWidget* createOutlineToolbar();
        void updateBlockTypesVisualization();

        bool eventFilter(QObject* obj, QEvent* event);

        // Manual cell block type painter
        void paintCells();

        std::optional<int> findCellUnderCursor() const;

        // Undo/Redo Logic
        bool blockTypeAssign();
        bool blockTypeAssign_Undo();

        // Undo/Redo data <CellId, old block type, new block type>
        std::vector<std::tuple<int, int, int>> blockTypeModifications;

        QSharedPointer<DLineMarker> brushMarker = nullptr;

        // Block Type Painting
        int brushSize = 2;
        bool bBlockPainting = false;

        QTerrainClassificationTreeModel treeModel;
        QTreeView* treeView = nullptr;

        Connection onGenerationFinished;
    };
}
