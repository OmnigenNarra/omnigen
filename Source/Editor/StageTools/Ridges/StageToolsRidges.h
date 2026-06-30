#pragma once
#include "../StageToolsBase.h"
#include "RidgesWidgets.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Nodes/RidgeNode.h"

namespace Design
{
    enum class ERidgeAction
    {
        DeleteSelectedRidge,
        EditSelectedRidge    //WIP dummy
    };

    template<>
    class StageTools<EGenerationStage::Ridges> final : public StageToolsBase
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

        const auto& getRidgeNodes() const { return ridgeNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        void changeTo3DRidges();
        void changeTo2DRidges();

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

    private:
        QWidget* createOutlineToolbar();

        void setupActions();

        bool eventFilter(QObject* obj, QEvent* event);

        // Main Ridge drawing
        bool startRidge(QMouseEvent* event);
        bool drawRidge(QMouseEvent* event);
        bool endRidge(qint64 parentGuid = 0);
        bool checkForCurrentTerrain(const GVector2D& point);

        void updateTreeViewSelection();
        void onSelectionChanged();

        // Ridge creation
        bool createRidge(const std::vector<QVector3D>& cPts, qint64 parentGuid = 0);
        bool createRidge_Undo(const std::vector<QVector3D>& cPts, qint64 parentGuid);
        bool deleteRidge(qint64 guid);
        bool deleteRidge_Undo(qint64 guid);
        bool deleteMultipleRidges(const std::vector<qint64>& guids);
        bool deleteMultipleRidges_Undo(const std::vector<qint64>& guids);

        // Ridge Height Assignment On Draw
        std::vector<float> getRidgeHeight(const std::vector<QVector3D>& cPts, QSharedPointer<DRidgeMarker> parentRidge);

        // Subridge Drawing
        void clearTempRidge();
        std::optional<QPair<qint64, QVector3D>> checkParentIntersection(const GVector2D& point);

        // Ridge editing logic
        void editRidgeByBrush(QMouseEvent* event);

        // Find ridge in range of editing brush, while saving closest point index
        std::optional<std::pair<QSharedPointer<DRidgeMarker>, int>> findClosestRidge(const Segment2D& segToCheck, const GVector2D& mPoint);

        // Propagates transformation to another element of pts returning the new point, taking into account previous/next (positiveDirection) point transformation and other factors
        QVector3D getAnotherPoint(const std::vector<QVector3D>& newPts, const std::vector<QVector3D>& oldPoints, int pIndex, const GVector2D& mousePos, QSharedPointer<DDomain> domain, QSharedPointer<DRidgeMarker> ownerRidge, bool positiveDirection);

        // Propagates point changes to subridges (if any are present) of currently edited ridge
        bool propagateToSubridge(QSharedPointer<DRidgeMarker> ridge, const QVector3D& parentPoint, const std::unordered_set<qint64>& ridgesToSkip = {});

        // Checks if potential new point is not out of its owning domains borders
        bool valdiatePoint(const std::vector<QVector3D>& newPoints, const QVector3D& pointToCheck, int pointToCheckIndex, QSharedPointer<DDomain> ownerDomain, QSharedPointer<DRidgeMarker> ownerRidge, const std::unordered_set<qint64>& ridgesToSkip = {}, bool skipSelf = false );

        // Checks new ridge points and adds new points or erases them depending on their density
        std::vector<QVector3D> modifyRidgePointCount(const std::vector<QVector3D>& ptsToCheck, QSharedPointer<DRidgeMarker> ridge);

        // Gathers all GUIDs of all subridges (and their subridges) of a given ridge
        std::unordered_set<qint64> collectAllSubridgesIds(QSharedPointer<DRidgeMarker> ridge);

        std::optional<QVector3D> mergePoints(const std::vector<QVector3D>& ptsToCheck, int mIdx, QSharedPointer<DRidgeMarker> ridge);

        // Highlight drawing
        void highlightPointsToEdit(QMouseEvent* mEvent);
        void highlightPointsToEdit(qint64 ridgeId, const QVector3D& brushOriginPoint);
        std::vector<QVector3D> getCirclePoints(const QVector3D& circleCenter, int points, int radius);
        void clearHighlights();

        // Visual representation of brush size, where circleDetail is the number of points creating the circle
        void drawBrushCircle(QMouseEvent* mEvent, int circleDetail);

        // Ridge Height Editing
        std::optional<std::pair<qint64, QVector3D>> findClosestRidgeline(QMouseEvent* mEvent);
        std::vector<int> findPointsUnderBrush(qint64 ridgeId, const QVector3D& brushCentralPoint);
        void editPointsHeight();
        void sculpt(QSharedPointer<DRidgeMarker> ridge, const std::vector<int>& pointsToEdit);
        void smooth(QSharedPointer<DRidgeMarker> ridge, const std::vector<int>& pointsToEdit);
        void flatten(QSharedPointer<DRidgeMarker> ridge, const std::vector<int>& pointsToEdit);

        // Ridge editing history
        bool finishEditing();
        bool finishEditing_Undo();

        // Ridge drawing data
        float brushWholeMovement;
        std::optional<GVector2D> oldBrushPos = {};
        std::vector<qint64> highlights;

        std::optional<GVector2D> newRidgeStart = {};
        QSharedPointer<DLineMarker> ridgePreview;

        QMap<ERidgeAction, QAction*> actions;

        QToolBar* toolBar = nullptr;

        QRidgesTreeModel treeModel;
        QTreeView* treeView = nullptr;

        // Event filter bools
        bool bDrawingOperation = false;
        bool bIsRidgeDrawing = false;
        bool bIsRidgeEditing = false;
        bool bBlockHover = false;
        bool bHeightEditing = false;

        // Ridge editing
        bool bDrawHighlights = true;
        bool bBrushStrokes = true;
        int brushSize;
        int heightBrushSizeFactor = 250;
        int brushStrength;
        QSharedPointer<DLineMarker> brushMarker = nullptr;

        bool bSculptTool = false;
        bool bFlattenTool = false;
        bool bSmoothTool = false;

        std::optional<QVector3D> brushOriginPoint;
        QTimer ticker;

        // <changed ridges guid, points, source index>
        std::vector<std::tuple<qint64, std::vector<QVector3D>, qint64>> oldRidgesData;

        std::unordered_map<qint64, QSharedPointer<RidgeNode>> ridgeNodes;

        QSharedPointer<DDomain> currentDomain;
        friend class QRidgesTreeModel;
    };
}
