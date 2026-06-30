#pragma once
#include "../StageToolsBase.h"
#include <variant>
#include "Source/Utils/ManipulationGizmo.h"
#include "Data/Assets/AssetBase.h"

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include <variant>

class  TestPlayer;
class  DLineMarker;
class  DPolygonMarker;
class  DManipulationGizmo;
struct BoundingBox;

class Material;

struct TRS
{
	QVector3D translation;
	QVector3D scale;
	QVector3D rotationAxis;
	float rotationAngleRadians;
};
template<> constexpr bool serializeAsPOD<TRS> = true;

namespace Design
{
    template<>
    class StageTools<EGenerationStage::TerrainFinalization> final : public StageToolsBase
    {
        enum class EToolMode
        {
            None,

            Sculpt,
            Smooth,
            Flatten,
            TexChange,
            TexErode
        };

    public:
        StageTools();

        virtual SelectionMgrBase* getSelectionMgr() const override;

        virtual void bind() override;
        virtual void unbind() override;

        virtual void save(OmniBin<std::ios::out>& writer) const override;
        virtual void load(OmniBin<std::ios::in>& reader) override;

        struct ChunkTriangle
        {
            DTerrainChunk* chunk;
            IndexType triangleIdx;
        };
        std::optional<ChunkTriangle> findChunkTriangleUnderCursor() const;

        template<typename SelectionEnum, SelectionEnum LS>
        friend class Selection;

    private:
        QWidget* createOutlineToolbar();

        void playFromSelection();

        // Vertex brushes were originally coded by William, not to be trusted.
        void editVertices();
        void sculpt();
        void erodeTexture();

        void exportUE5();
        void calcWetness();
        void exportRockMaterial(const OmnigenAsset<EAsset::RockMaterial>& rockMaterial, const std::string& exportFile);
        void exportSoilMaterial(const OmnigenAsset<EAsset::SoilMaterial>& soilMaterial, const std::string& exportFile);
        void exportPlant(const OmnigenAsset<EAsset::Plant>& plant, const std::string& exportFile);
        void exportMaterial(const Material& material, OmniBin<std::ios::out>& writer) const;
        void exportTexture(const Texture& texture, OmniBin<std::ios::out>& writer) const;

        bool eventFilter(QObject* obj, QEvent* event);

        std::optional<QVector3D> findPoint(QMouseEvent* mEvent);
        std::vector<std::tuple<int, int>> findGeometryUnderBrush();
        void updateBrush();
        
        void clearBrush();

        void updateGeometry(EToolMode mode, const std::unordered_set<int>& clustersIds);

        bool finalizeChanges(EToolMode mode, const std::unordered_map<std::pair<int, IndexType>, float>& changes);
        bool finalizeChanges_Undo(EToolMode mode, const std::unordered_map<std::pair<int, IndexType>, float>& changes);

    private:
        QSharedPointer<DLineMarker>    brushGizmo    = nullptr;
        QVector3D                      brushPosition;
        int  brushSize = 5;
        int  brushStrength = 5;

        QTimer ticker;
        
        //from 0.8 to 1.2
        float biomePackParam  = 0.8f;
        float biomeUIParm     = 80;

        bool bPlayTool = false;

        EToolMode toolMode = EToolMode::None;

        std::optional<ChunkTriangle> brushOrigin;
        std::unordered_map<std::pair<int, IndexType>, float> vertexChanges;

        static inline int textureConversionCounter = 0;
        static inline std::mutex textureConversionCounterGuard;
    };
}
