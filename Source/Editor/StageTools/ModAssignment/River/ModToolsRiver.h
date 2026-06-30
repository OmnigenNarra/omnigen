#pragma once
#include "../ModToolsBase.h"
#include "ModRiverWidget.h"

namespace Design
{
    template<>
    struct ModTools<Generation::ETerrainMod::River> : ModToolsBase
    {
        virtual void bind() override;
        virtual void unbind() override;
        virtual QWidget* create() override;
        virtual OutlineTreeModel* getTreeModel() override;

        virtual void loadTreeViewModEntries() override;
        virtual void onModSelectionChanged() override;
        virtual void addModEntry(size_t typeHash, QSharedPointer<Editable> object) override;
        virtual void removeModEntry(QSharedPointer<Editable> object) override;
        virtual void updateModEntry(QSharedPointer<Editable> object, bool reset) override;
        virtual void setModToolTreeView(QTreeView* treeView) override;

    private:
        std::optional<QVector3D> startingPoint;
        QSharedPointer<DLineMarker> riverPreview;

        bool isSpawningRiver = false;
        bool isDrawingRiver = false;
        bool drawingOperation = false;
        bool isEditingDrawn = false;
        bool isEditing = false;

        int brushSize;
        int brushStrength;

        std::optional<QVector3D> oldBrushPos = {};

        QSharedPointer<DLineMarker> brushMarker = nullptr;

        QModRiverTreeModel treeModel;

        bool spawnRiver(const QMouseEvent& me, const std::vector<QVector3D>& riverPoints = {});
        bool spawnRiver_Undo(const QMouseEvent&, const std::vector<QVector3D>&);

        // River drawing
        void startRiverDrawing(const QMouseEvent& me);
        bool riverDrawing(const QMouseEvent& me);
        bool endRiverDrawing();

        // Drawn River Editing
        void editDrawnRiver(const QMouseEvent& me);
        void drawBrushCircle(QMouseEvent* mEvent, int circleDetail);

        std::optional<QVector3D> findPointOnMap(const QMouseEvent& me);

        // Viewport mouse controls
        bool eventFilter(QObject* obj, QEvent* event);
    };

    template<>
    class Selection<Generation::ETerrainMod, Generation::ETerrainMod::River> : public SelectionBase, public QEnableSharedFromThis<Selection<Generation::ETerrainMod, Generation::ETerrainMod::River>>
    {
    public:
        using DataType = QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>>;

        Selection(const std::any& inRMod);
        static bool findOnScene(QMap<Generation::ETerrainMod, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive) {};
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>>>& inRMods);

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;
        virtual void select() const override;
        virtual void deselect() const override;
        virtual QVector3D getPosition() const override;

    protected:
        Selection() = default;

        QSharedPointer<Generation::TerrainMod<Generation::ETerrainMod::River>> riverModPtr;

        template<typename T>
        friend class QSharedPointer;
    };
    using RiverSelection = Selection<Generation::ETerrainMod, Generation::ETerrainMod::River>;
}