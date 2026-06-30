#pragma once
#include "Scene/Generation/Common/Objects/Heightfield.h"

namespace Generation
{
    class DEM;
}

class Polygon2D;

void omniSave(const Generation::DEM& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::DEM& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    struct CellElevationData
    {
        float height;
        float minH = std::numeric_limits<float>::max();
        float maxH = 0;
        float steepness = 1;
        GVector2D gradient;
    };

    struct DEMUpdateInfo : Editable
    {
        std::vector<GVector2D> points;
    };

    class DEM
    {
    public:
        DEM() = default; // for loading
        DEM(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing);

        Heightfield heightData;
        Heightfield levelData;
        Heightfield verticalDisplacementXCoords;

        void addHeightToPoints(const std::unordered_map<IndexType, float>& pointsToUpdate);
        void updatePointsNormal(const std::unordered_map<IndexType, QVector3D>& pointsToUpdate);
        void reshapeGrid();

        CellElevationData getCellElevationData(const Polygon2D& cell) const;

        static void initialize();
        static void clear();
        void loadFromIHs(const std::vector<GVector2D>& pointsToLoad = {});

        template<typename F>
        void forEachPoint(const GPoint& sq, const F& func) const
        {
            GVector2D fromCorner(sq.x * GRID_SEGMENT_WIDTH, sq.z * GRID_SEGMENT_WIDTH);
            GVector2D toCorner((sq.x + 1) * GRID_SEGMENT_WIDTH, (sq.z + 1) * GRID_SEGMENT_WIDTH);

            for (float x = fromCorner.x; x <= toCorner.x; x += heightData.getGridSpacing())
                for (float z = fromCorner.z; z <= toCorner.z; z += heightData.getGridSpacing())
                    func(GVector2D(x, z));
        }

        inline static const float gridSpacing = 100.0f;
        inline static const QVector4D color = QVector4D(0.75, 0.45, 1, 1);
    };
}

void omniSave(const Generation::DEM& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::DEM& object, OmniBin<std::ios::in>& omniBin);

class DDemMarker : public Generation::DHeightfieldMarker
{
	static inline ShaderPipeline shaderPipeline;
    static inline std::vector<QVector4D> gradient;
    static inline GLuint gradientTexture;

public:
    using Generation::DHeightfieldMarker::DHeightfieldMarker;

    quint32 getShaderLabel() const { return typeid(decltype(*this)).hash_code(); };
	virtual void bindShader(const OmnigenCamera& camera) override;
	virtual void draw() override;
	virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

protected:
    virtual void createShader() override;
    virtual void createShaderResources() override;

    IMPLEMENT_SHOULD_DRAW();
};