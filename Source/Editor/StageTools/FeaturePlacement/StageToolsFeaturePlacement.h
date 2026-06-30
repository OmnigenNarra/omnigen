#pragma once
#include "../StageToolsBase.h"
#include "FeaturePlacementWidgets.h"
#include <tbb/spin_mutex.h>
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockBase.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlock.h"
#include "ClusterBorderMarker.h"
#include "Nodes/ClusterNode.h"
#include "Nodes/MetaClusterNode.h"

class DLineMarker;

namespace Design
{
    enum class EFeaturePlacementAction
    {
        AutoGenerateSelection,

        CreateMetaClusters,
        RemoveMetaClusterCells,

        CreateClusters,
        RemoveClusterCells
    };

    enum class EFeaturePlacementToolType
    {
        Select,
        Brush,
        None
    };

    template<>
    class StageTools<EGenerationStage::FeaturePlacement> final : public StageToolsBase
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

        const auto& getClusterNodes() const { return clusterNodes; }
        const auto& getMetaClusterNodes() const { return metaClusterNodes; }

        void addNode(size_t typeHash, QSharedPointer<Editable> object);
        void removeNode(QSharedPointer<Editable> object);
        void modifyNode(QSharedPointer<Editable> object);

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

    private:
        QWidget* createOutlineToolbar();

        void setupActions();

        bool eventFilter(QObject* obj, QEvent* event);

        void brushClusters(QMouseEvent* mEvent);

        void brushMetaClusters(QMouseEvent* mEvent);

        // Spawn meta cluster from found qtree cells
        QSharedPointer<Generation::TerrainBlockMetaClusterBase> spawnMetaCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, int sourceCell);

        // Spawn meta cluster from given cells
        QSharedPointer<Generation::TerrainBlockMetaClusterBase> spawnMetaCluster(const std::unordered_set<int>& cells, ETerrainBlock type, bool autoGenCluster = false, std::optional<qint64> guid = std::nullopt);

        // Spawn cluster from found qtree cells
        QSharedPointer<Generation::TerrainBlockClusterBase> spawnCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, int sourceCell, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster);

        // Spawn cluster from given cells
        QSharedPointer<Generation::TerrainBlockClusterBase> spawnCluster(const std::unordered_set<int>& cells, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster, std::optional<qint64> guid = std::nullopt);

        // Tries to extend meta cluster from found qtree cells
        void extendMetaCluster(const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster, const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes);

        // Tries to extend cluster from found qtree cells
        void extendCluster(const QSharedPointer<Generation::TerrainBlockClusterBase>& cluster, const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes);

        // Select proper cells for meta cluster from qtree found cells
        std::unordered_set<int> selectCellsForMetaCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, const std::unordered_set<int>& assignedCells, Generation::ETerrainBlock blockType);

        // Select proper cells for cluster from found qtree cells
        std::unordered_set<int> selectCellsForCluster(const std::vector<const tml::node<float, IndexType>*>& foundBlockNodes, const std::unordered_set<int>& assignedCells, const QSharedPointer<Generation::TerrainBlockMetaClusterBase>& metaCluster);

        // Select from given cells, if connected to assignedCells or first candidate
        std::unordered_set<int> selectCellsIfNeighboring(const std::unordered_set<int>& assignedCells, std::unordered_set<int>&& candidateCells);

        // Cluster given cells
        std::vector<std::unordered_set<int>> clusterSelectedCells(const std::unordered_set<int>& selectedCells, bool boundByMetaClusters = false, const std::optional<int>& maxSize = std::nullopt);

        // Update clusters and meta clusters by deleting set of given cells
        std::pair<std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>> updateDataByDeletedCells(const std::unordered_set<int>& cellsToDelete, bool updateMeta = true);

        // Update clusters which cells are now belonging to modifed clusters, return edited clusters
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> updateDataByModifiedClusters(const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& modifiedClusters);

        // Update clusters and meta clusters which cells are now belonging to modifed meta clusters, return edited clusters and metaclusters
        std::pair<std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>, std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>> updateDataByModifiedMetaClusters(const std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>& modifiedMetaClusters);

        // Make sure clusters are connected, if not, separate clusters. Returns new clusters after separation
        std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>> perserveClustersIntegrity(const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& editedClusters);

        // Make sure meta clusters are connected, if not, separate meta clusters. Returns new meta clusters after separation
        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> perserveMetaClustersIntegrity(const std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>& editedMetaClusters);

        // Cluster given clusters
        std::vector<std::vector<int>> clusterSelectedClusters(const std::vector<std::unordered_set<int>>& selectedClusters);

        // Calculate map of neighbor relations between given clusters
        std::vector<std::unordered_set<int>> calculateClusterNeighborRelation(const std::vector<std::unordered_set<int>>& selectedClusters);

        // Save cluster data on cell before editing
        void saveClusterEdit(int cellId);

        // Save cluster data for undo/redo
        bool finishEditing(const std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>>& clustersChanges);
        bool finishEditing_Undo(const std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>>& clustersChanges);

        // Helper function to apply cluster data changes
        void applyEditClusterData(const std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>>& clustersChanges);

        // Create or update
        void updateClusterMarkers(const std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>>& metaClustersToUpdate, const std::vector<QSharedPointer<Generation::TerrainBlockClusterBase>>& clustersToUpdate);
        void showClusterBorders(bool forceUpdate = false);

        QMap<EFeaturePlacementAction, QAction*> actions;

        QTimer ticker;
        QTreeView* treeView = nullptr;

        // Tools
        EFeaturePlacementToolType selectedTool = EFeaturePlacementToolType::None;

        // Select Clusters
        int selectSize;
        float selectScale = 250.0f;

        // Brush Clusters
        bool clusterBrush;
        bool metaClusterBrush;

        QSharedPointer<Generation::TerrainBlockClusterBase> brushEditedCluster;
        QSharedPointer<Generation::TerrainBlockMetaClusterBase> brushEditedMetaCluster;

        // Brush
        QSharedPointer<DLineMarker> brushMarker = nullptr;
        int brushSize;
        float brushScale = 250.0f;

        std::vector<QSharedPointer<Generation::TerrainBlockMetaClusterBase>> terrainMetaClustersMap;

        std::unordered_map<qint64, QSharedPointer<BatchedSection<ClusterBorderBatchParams>>> metaClusterMarkers;
        std::unordered_map<qint64, QSharedPointer<BatchedSection<ClusterBorderBatchParams>>> clusterMarkers;

        std::unordered_map<qint64, QSharedPointer<Generation::TerrainBlockMetaClusterBase>> currentMetaClusters;
        std::unordered_map<qint64, QSharedPointer<Generation::TerrainBlockClusterBase>> currentClusters;

        std::unordered_map<int, std::optional<std::pair<qint64, std::optional<qint64>>>> editedClustersData;

        static const inline auto metaClusterBorderParams = ClusterBorderBatchParams(Colors::white);
        static const inline auto clusterBorderParams = ClusterBorderBatchParams(QVector4D(0.65f, 0.65f, 0.65f, 1.0f));

        // Stage Nodes
        std::unordered_map<qint64, QSharedPointer<ClusterNode>> clusterNodes;
        std::unordered_map<qint64, QSharedPointer<MetaClusterNode>> metaClusterNodes;
        tbb::spin_mutex nodesGuard;
    };
}
