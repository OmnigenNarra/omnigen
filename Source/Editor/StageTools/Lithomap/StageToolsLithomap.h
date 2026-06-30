#pragma once
#include "../StageToolsBase.h"
#include "Scene/OmnigenDrawable.h"
#include "LithomapToolsWidgets.h"
#include "Nodes/CellNode.h"

class  DLineMarker;
class  DPolygonMarker;
struct BoundingBox;
struct BoundingPlane;

namespace Design
{
    template<>
    class StageTools<EGenerationStage::Lithomap> final : public StageToolsBase
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

        const auto& getCellNodes() const { return cellNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;
    
    private:
        QWidget* createOutlineToolbar();
        bool eventFilter(QObject* obj, QEvent* event);

        void paintCells();
        
        std::unordered_map<int, std::array<qint64, 2>> changesByCellId;
        bool applyLithoTypeChanges();
        bool applyLithoTypeChanges_Undo();
    private:
        QSharedPointer<DLineMarker> brushMarker = nullptr;
        int brushSize = 5;
        bool bLithoMapTool = false;

        std::unordered_map<GVector2D, QSharedPointer<CellNode>> cellNodes;

        QLithoAssignmentTreeModel treeModel;
        QTreeView* treeView = nullptr;
    };
}
