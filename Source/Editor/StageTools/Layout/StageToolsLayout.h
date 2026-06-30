#pragma once
#include "../StageToolsBase.h"
#include "LayoutWidgets.h"
#include "Nodes/TerrainDomainNode.h"
#include "Nodes/WaterDomainNode.h"
#include "Nodes/BiomeDomainNode.h"
#include "Nodes/DomainSquareNode.h"

class DDomainPaintingPreview;

namespace Design
{
    enum class ELayoutAction
    {
        CreateTerrain,
        CreateBiome,
        CreateWater,

        ExtractTerrain,
        ExtractBiome,
        ExtractWater,

        AppendToDomain,
        ExtractIntoDomain,
        SubtractFromDomain,

        MergeDomains,
        DeteleSelectedDomains
    };

    enum class EOmnigenPainting
    {
        New,
        Append,
        Extract,
        Subtract
    };

    template<>
    class StageTools<EGenerationStage::Layout> final : public StageToolsBase
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

        virtual void loadSnapshotData() override;
        virtual bool validatePipeline() override;
        virtual void updatePipeline() override;

        const auto& getTerrainDomainNodes() const { return terrainDomainNodes; }
        const auto& getWaterDomainNodes() const { return waterDomainNodes; }
        const auto& getBiomeDomainNodes() const { return biomeDomainNodes; }
        const auto& getDomainSquareNodes() const { return domainSquareNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        const auto& getPaintType() const { return domainTypeToPaint; }
        const auto& getPaintOption() const { return paintingOption; }

        // Dynamic domain painting
        bool dynamicPaintingClick(QMouseEvent* event);
        bool dynamicPaintingMove(QMouseEvent* event);
        void dynamicPaintingEnd();

    private:
        // Widgets
        QWidget* createOutlineToolbar();
        QWidget* createPaintTool();

        // Pipeline related

        // special version of partitioning, where land squares can extend diagonally, unlike shoreline squares
        std::vector<QSet<GPoint>> partitionLandWithShorelineSquares(QSet<GPoint> squares, const QSet<GPoint>& landSquares);

        // vector with pair of shorelines and inner shorelines, which shall represent landmass
        using PreservedLands = std::vector<std::pair<std::vector<QSharedPointer<DShorelineMarker>>, std::vector<QSharedPointer<DShorelineMarker>>>>;
        // Find and generate proper shorelines by analyzing original squares of landmass with squares that are avaliable for having land
        std::unordered_map<qint64, PreservedLands> findPreservedLandsOfLandmasses(const std::unordered_set<qint64> landmassToPreserve, const std::unordered_set<qint64> invalidShorelines, const QSet<GPoint>& changedSquares);

        // Init
        void setupActions();

        // Editor controls
        bool createDomainFromSquares(EDomainType type, const QSet<GPoint>& squares, bool extract, bool notify = true);
        bool createDomainFromSquares_Undo(EDomainType type, const QSet<GPoint>& squares, bool extract, bool notify = true);
        bool createFlatTerrainFromSquares(const QSet<GPoint>& squares);
        bool createFlatTerrainFromSquares_Undo(const QSet<GPoint>& squares);
        bool appendSquaresToDomain(QSharedPointer<DDomain> domain, const QSet<GPoint>& squares, bool extract);
        bool appendSquaresToDomain_Undo(QSharedPointer<DDomain>, const QSet<GPoint>&, bool extract);
        bool subtractSquaresFromDomain(QSharedPointer<DDomain> domain, const QSet<GPoint>& squares);
        bool subtractSquaresFromDomain_Undo(QSharedPointer<DDomain>, const QSet<GPoint>&);
        bool mergeDomains(const std::vector<QSharedPointer<DDomain>>& source, QSharedPointer<DDomain> target);
        bool mergeDomains_Undo(const std::vector<QSharedPointer<DDomain>>& source, QSharedPointer<DDomain> target);
        bool deleteDomains(std::vector<QSharedPointer<DDomain>> domains);
        bool deleteDomains_Undo(std::vector<QSharedPointer<DDomain>>);

        bool eventFilter(QObject* obj, QEvent* event);

        // Event reactions
        void selectNewDomain(size_t typeHash, QSharedPointer<Editable> object);
        void updateTreeViewSelection();

        QLayoutTreeModel treeModel;
        QTreeView* treeView = nullptr;
        QWidget* paintTool = nullptr;

        QMap<ELayoutAction, QAction*> actions;

        bool bPaintingSessionActive = false;
        bool bPainting = false;
        mutable QSharedPointer<DDomain> paintingTarget;
        mutable QSharedPointer<DDomainPaintingPreview> paintingPreview;
        mutable EDomainType domainTypeToPaint = EDomainType::Terrain;
        mutable EOmnigenPainting paintingOption = EOmnigenPainting::Append;

        // Stage Nodes
        std::unordered_map<qint64, QSharedPointer<TerrainDomainNode>> terrainDomainNodes;
        std::unordered_map<qint64, QSharedPointer<WaterDomainNode>> waterDomainNodes;
        std::unordered_map<qint64, QSharedPointer<BiomeDomainNode>> biomeDomainNodes;

        std::unordered_map<GPoint, QSharedPointer<DomainSquareNode>> domainSquareNodes;

        friend class QLayoutTreeModel;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;
        friend class SatScanDomainGeneration;
    };
}