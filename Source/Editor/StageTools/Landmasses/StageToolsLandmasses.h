#pragma once
#include "../StageToolsBase.h"
#include "LandmassWidgets.h"
#include "Nodes/ShorelineNode.h"
#include "Nodes/LandmassNode.h"

namespace Design
{
    class SeasideData
    {
    public:

        QSet<GPoint> allSeaside;
        QSet<GPoint> emptySeaside;
        QSet<GPoint> landSeaside;
        QSet<GPoint> shorelineSeaside;

        std::unordered_map<GPoint, QWeakPointer<DLandmassMarker>> landmassSquareMap;
        std::unordered_map<GPoint, QWeakPointer<DShorelineMarker>> shorelineSquareMap;

        void clear();
    };

    struct AfterEditLandmass
    {
        std::vector<QVector3D> mainPolygon;
        std::vector<std::tuple<std::vector<QVector3D>, float>> innerPolygons;
        float area = 0;
        // Whenever landmass have to exist for validation reasons (contains terrain only squares etc.)
        bool isMandatory = false;
    };

    struct LandmassFindResult
    {
        QSharedPointer<DLandmassMarker> landmass;
        std::vector<QSharedPointer<DShorelineMarker>> shorelines;
        std::vector<QSharedPointer<DShorelineMarker>> innerShorelines;
    };

    struct ShorelineFindResult
    {
        QSharedPointer<DShorelineMarker> shoreline;
        int idx;
    };

    enum class ELandmassEditType
    {
        Change,
        Create,
        Delete
    };

    enum class ELandmassAction
    {
        DeleteSelectedLandmasses,
        ReshapeShoreline
    };

    template<>
    class StageTools<EGenerationStage::Landmasses> final : public StageToolsBase
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

        const auto& getShorelineNodes() const { return shorelineNodes; }
        const auto& getLandmassNodes() const { return landmassNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        void changeTo3DShorelines();
        void changeTo2DShorelines();

    private:
        // Widgets
        QWidget* createOutlineToolbar();

        // Init
        void setupActions();

        bool eventFilter(QObject* obj, QEvent* event);

        void updateTreeViewSelection();
        void onSelectionChanged();

        void clearSeasideData();
        void calculateSeasideData();
        void recalculateSeasideData(const QSharedPointer<SeasideData>& seasideData);

        void finalizeLandmassChanges(const std::vector<QSharedPointer<SeasideData>>& seasideDatas, bool respawnSeamass = true);

        // Reshape shoreline
        void reshapeShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& shorelines);

        // Delete landmass
        void deleteLandmasses(const std::vector<QSharedPointer<DLandmassMarker>>& landmasses);
        std::vector<QSharedPointer<DLandmassMarker>> spawnInitialLandmasses(const QSharedPointer<DLandmassMarker>& landmass);

        // Spawn landmass
        void spawnLandmass(QMouseEvent* event);

        // Edit Cliffs
        void editCliffsByBrush();

        std::vector<float> calculateCliffHeightsAfterIncrement(const QSharedPointer<DShorelineMarker>& shoreline, const std::set<int>& idxs, float baseHeight, float increment);

        std::vector<float> calculateCliffHeightsAfterFlattening(const QSharedPointer<DShorelineMarker>& shoreline, const std::set<int>& idxs, float baseHeight, float strength);

        float calculateBaseHeight(const QSharedPointer<DShorelineMarker>& shoreline, const std::set<int> affectedIndexes, float increment);

        std::set<int> findIndexesToEdit(const QSharedPointer<DShorelineMarker>& shoreline, int closestIdx, const GVector2D& brushPoint, float brushSize);

        // Edit landmass
        void updateLandmassBrushPolygons(QMouseEvent* event);

        void updateLandmassesWithBrushPolygons();

        std::map<float, ShorelineFindResult> findShorelinesInRange(const GVector2D& point, float range);

        // find landmasses overlapping with given polygons
        std::vector<LandmassFindResult> findOverlapingLandmasses(const std::vector<std::vector<QVector3D>>& polygons);

        // apply new shorelines paths to existing shoreline markers
        std::pair<std::vector<QSharedPointer<DShorelineMarker>>, std::vector<QSharedPointer<DShorelineMarker>>> applyNewShorelines(const std::vector<QSharedPointer<DShorelineMarker>>& shorelinesToEdit, const std::vector<QSharedPointer<DShorelineMarker>>& shorelinesToReapply, std::vector<std::vector<QVector3D>>* newShorelines, bool isCoast, const std::vector<QVector3D>& landmass, const QSet<GPoint>& relevantSeaside);

        // delete landmasses if they are caught inside existing landmasses
        void deleteLandmassesInsideLandmass(const QSharedPointer<DLandmassMarker>& landmassToEdit);

        // delete inner seas if they are caught inside existing inner seas
        void deleteInnerSeasInsideInnerSeas(const QSharedPointer<DLandmassMarker>& landmassToEdit);

        // select old landmasses to edit with new
        std::vector<int> matchLandmasses(const std::vector<QSharedPointer<DLandmassMarker>>& oldLandmasses, const std::vector<AfterEditLandmass>& newLandmasses);

        // returns after effect of using brush on landmasses
        std::vector<AfterEditLandmass> useBrushOnLandmasses(const std::vector<QSharedPointer<DLandmassMarker>>& landmass, const std::vector<QSharedPointer<DShorelineMarker>>& innerShorelinesInRange, const std::vector<std::vector<QVector3D>>& brushPolygons, const QSet<GPoint>& avaliableSeaside);

        // cut given polygon by given squares
        std::tuple<std::vector<std::vector<QVector3D>>, std::vector<std::vector<QVector3D>>> cutPolygonByBorders(const std::vector<std::vector<QVector3D>>& polygonsToCut, const QSet<GPoint>& ownLand, const QSet<GPoint>& avaliableSeaside);

        // check if paths are comparable to given number of elements
        bool areComparable(const std::set<GVector2D>& setOfpath1, const std::vector<QVector3D>& path2, int degreeOfComparison);

        // Edit history
        using DShorelineMarkerData = std::vector<std::tuple<qint64 /*guid*/, QString /*name*/, std::vector<QVector3D> /*pts*/, bool /*isLoop*/>>;
        using DLandmassMarkerData = std::tuple<ELandmassEditType, QString /*name*/, DShorelineMarkerData /*shorelines*/, DShorelineMarkerData /*inner shorelines*/>;

        void saveEditLandmassData(ELandmassEditType editType, const QSharedPointer<DLandmassMarker>& marker);
        void saveLandmassData(std::unordered_map<qint64, DLandmassMarkerData>* editData, ELandmassEditType editType, const QSharedPointer<DLandmassMarker>& marker);

        std::vector<QSharedPointer<SeasideData>> findEditedSeasideDatas(const std::unordered_map<qint64, DLandmassMarkerData>& landmassData);

        bool finishEditing(const std::unordered_map<qint64, DLandmassMarkerData>& landmassChanges, bool requiresFinalize = true);
        bool finishEditing_Undo(const std::unordered_map<qint64, DLandmassMarkerData>& landmassChanges, bool requiresFinalize = true);

        void finishEditing_Delete(qint64 landmassId);
        void finishEditing_Create(qint64 landmassId, const DLandmassMarkerData& landmassData, std::set<qint64>* allLeftoverShorelines);
        void finishEditing_Change(qint64 landmassId, const DLandmassMarkerData& landmassData, std::set<qint64>* allLeftoverShorelines);

        // Brush methods
        std::vector<QVector3D> squarifyPath(const std::vector<QVector3D>& path, float squareWidth);
        void drawBrushCircle(QMouseEvent* mEvent, int circleDetail);
        std::vector<QVector3D> getCirclePoints(const QVector3D& circleCenter, int points, int radius);

        QMap<ELandmassAction, QAction*> actions;

        QToolBar* toolBar = nullptr;

        QLandmassTreeModel treeModel;
        QTreeView* treeView = nullptr;

        // Event filter bools
        bool drawingOperation = false;
        bool isLandmassSpawning = false;
        bool isCliffEditing = false;
        bool isLandmassEditing = false;

        // Spawn landmass data
        ELandmassSize spawnSize;
        EShorelineComplexity spawnComplexity;

        // Edit cliff data
        bool isCliffEditIncrement = false;
        bool isCliffEditFlattening = false;

        bool isCliffEditIncrementFlattening;

        int cliffBrushSizeFactor = 1000;
        int cliffStrength;
        int cliffStrengthFactor = 5;

        QTimer tickerForCliffEditing;
        std::optional<GVector2D> brushPointForCliffEditing;

        // Edit landmass data
        bool isLandmassEditAppend = false;
        bool isLandmassEditExtract = false;

        std::vector<std::vector<QVector3D>> storedBrushPolygons;
        std::vector<QSharedPointer<DLineMarker>> storedBrushPolygonsMarkers;

        int brushSize;
        int brushSizeFactor = 500;
        QSharedPointer<DLineMarker> brushMarker = nullptr;
        std::optional<GVector2D> oldBrushPos = {};

        std::unordered_map<qint64, DLandmassMarkerData> editLandmassData;


        // Seaside data
        std::vector<QSharedPointer<SeasideData>> allSeasideData;
        std::unordered_map<GPoint, QSharedPointer<SeasideData>> seasideSquareMap;

        inline static std::pair<float, float> minMaxShorelinePointDist = {100.0f, 400.0f};

        // Stage Nodes
        std::unordered_map<qint64, QSharedPointer<ShorelineNode>> shorelineNodes;
        std::unordered_map<qint64, QSharedPointer<LandmassNode>> landmassNodes;

        friend class QLandmassTreeModel;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;
    };
}


