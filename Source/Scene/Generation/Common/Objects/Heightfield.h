#pragma once
#include "Utils/CoreUtils.h"
#include "Scene/Generation/Common/Markers/MarkerDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include <Mathematics/IntpBicubic2.h>

#define DEBUG_HEIGHTFIELD_TEXCOORD 0

namespace Generation
{
    class Heightfield;
    class DHeightfieldMarker;
}

void omniSave(const Generation::Heightfield& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::Heightfield& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    class Heightfield
    {
    public:
        struct Config
        {
            Config() = default;
            Config(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing);

            float gridSpacing;
            int sizeX, sizeZ;
            int offsetX, offsetZ;

            mutable bool edited = false;
        };

        struct Vertex
        {
            float height = -1.0f;
            QVector3D normal;
        };

        Heightfield() = default; // Used for loading only
        Heightfield(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing);

        float getGridSpacing() const { return config.gridSpacing; }
        GPoint getSize() const { return { config.sizeX, config.sizeZ }; };
        GPoint getOffset() const { return { config.offsetX, config.offsetZ }; };
        GVector2D getBotCorner() const { return { config.offsetX * config.gridSpacing, config.offsetZ * config.gridSpacing }; };
        GVector2D getTopCorner() const { return { (config.offsetX + config.sizeX) * config.gridSpacing, (config.offsetZ + config.sizeZ) * config.gridSpacing }; };
        const auto& getGeometryRW() const { return geometry; }

        bool wasEdited() const { return config.edited; };
        BoundingBox getBoundingBox() const;
        std::vector<float> createGDALbuffer(int margin);
        
        void setEditedStatus(bool status) { config.edited = status; };
        void setHeight(int x, int z, float height);
        void setHeight(IndexType i, float height);
        void addHeight(IndexType i, float deltaH);
        void setNormal(int x, int z, const QVector3D& normal);
        void setNormal(IndexType i, const QVector3D& normal);
        QVector3D getPoint(int x, int z) const;
        const QVector3D& getNormal(int x, int z) const;
        GVector2D getPoint2D(int x, int z) const;
        inline QVector3D getPoint(const GPoint& gp) const { return getPoint(gp.x, gp.z); };
        inline QVector3D getPoint2D(const GPoint& gp) const { return getPoint2D(gp.x, gp.z); };
        inline QVector3D getPoint2D(const IndexType& i) const { return getPoint2D(fromIdx(i)); };

        GVector2D getCoords(const GVector2D& position) const;
        float sampleGrid(int x, int z) const;
        float sample(const GVector2D& position) const;
        float sampleSmooth(const GVector2D& position) const;
        GVector2D sampleGradient(const GVector2D& position) const;
        QVector3D sampleNormal(const GVector2D& position) const;

        template<typename MarkerType = DHeightfieldMarker>
        void makePreview(const QVector4D& color)
        {
            updateNormals();
            auto hf = spawn<MarkerType>(geometry, config, color);
            //spawn<DLineMarker>(hf->getBoundingBox().nbl, 10000);
        }

        void reshapeGrid(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing);
        void update() const;
        void update(const std::vector<GPoint>& pts) const;
        void updateNormals();
        IndexType idx(int x, int z) const;
        IndexType idx(const GVector2D& point) const;
        GPoint fromIdx(IndexType i) const;
        float getHeightByIdx(IndexType i) const;
        QVector3D computeNormal(int x, int z) const;

    private:
        Config config;

        // Height data is stored in 1D array as the renderer needs it like this anyway.
        QSharedPointer<RenderGeometryData<Vertex>> geometry = QSharedPointer<RenderGeometryData<Vertex>>::create();

        friend class QSharedPointer<Heightfield>;

        FRIEND_OMNIBIN_NS(Heightfield);
    };

    class DHeightfieldMarker : public DMarker
    {
        static inline ShaderPipeline shaderPipeline;
        static inline float lineWidthCached;

    public:
        DHeightfieldMarker(const QSharedPointer<RenderGeometryData<Heightfield::Vertex>>& inGeom, const Heightfield::Config& inConfig, const QVector4D& inColor);

        // Rendering
        IMPLEMENT_SHOULD_DRAW();

        quint32 getShaderLabel() const { return typeid(decltype(*this)).hash_code(); };
        virtual void cacheBoundingBox() override;
        virtual void bindShader(const OmnigenCamera& camera) override;
        virtual void draw() override;
        virtual void unbindShader() override;
        virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    protected:
        virtual void createShader() override;

        QSharedPointer<RenderGeometryData<Heightfield::Vertex>> geometry;
        QVector4D color;
        Heightfield::Config config;
    };
}

inline void omniSave(const Generation::Heightfield& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.geometry;
    omniBin << object.config;
}

inline void omniLoad(Generation::Heightfield& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.geometry;
    omniBin >> object.config;
}