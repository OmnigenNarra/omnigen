#pragma once
#include <QMatrix>
#include <memory>
#include "Utils/OmniBin/OmniBinQt.h"
#include "Utils/GeometryData.h"
#include "ShaderPipeline.h"
#include "BoundingBox.h"
#include "Editable.h"

class OmnigenCamera;
class QOpenGLFunctions;

// Marks visibility of DrawableType in each viewport
template<class DrawableType>
std::array<bool, 4> bShouldDraw = { true, true, true, true };

#define IMPLEMENT_SHOULD_DRAW() virtual bool shouldDraw(int vIdx) const override { return bShouldDraw<std::decay_t<decltype(*this)>>[vIdx]; }

enum class ELOD
{
    Zero, // most detailed
    Mid,
    Far,
    Last // least detailed, must always exist
};

enum class ERenderPriority
{
    Terrain,
    Foliage,
    Marker,
    Skybox,
    Grid,
    Domain,
    DomainHandle,
    Gizmo
};

// Renderer (Viewport) works with instances of OmnigenDrawable
// It mostly consists of geometry to render and drawing methods
// It is expected from each drawable to provide a shader
// 
// To implement shadows or other techniques:
// 1. Shaders should be decoupled from objects
// 2. Shaders should render any geometry, using EShaderUniform and EShaderAttribute to bind stuff
// 
// There is LOD support, but nothing really uses it, stuff residing in Last or in some cases (Assets) Zero.
// 
// Names of classes deriving from OmnigenDrawable are expected to be preceded with 'D'. eg. DEditorGrid
class OmnigenDrawable : public Editable
{
public:
    OmnigenDrawable();
    virtual ~OmnigenDrawable() = default;

    inline const auto& getGuid() const { return guid; }
    inline const auto& isCulled() const { return bIsCulled; }
    inline const auto& getActiveLOD() const { return activeLOD; }
    inline const auto& getBoundingBox() { if (cachedBoundingBox.sizes.isNull()) cacheBoundingBox(); return cachedBoundingBox; }
    virtual bool shouldDraw(int vIdx) const { return true; }
    
    virtual void updateCullStatus(const OmnigenCamera& camera, int vIdx);
    virtual void updateCullStatusInstanced(const OmnigenCamera& camera, int vIdx);
    void setActiveLOD(ELOD ll);
    void setGuid(qint64 id); // Only for restoring deleted domains.
    void assignLodLevel(ELOD ll, QSharedPointer<GeometryDataBase> newData);
    void clearLodLevel(ELOD ll);
    void updateVbo(ELOD ll);

    virtual ERenderPriority getRenderPriority() const = 0;
    virtual quint32 getShaderLabel() const = 0;
    virtual float getCullDistance() const { return std::numeric_limits<float>::max(); }

    // Most of initialize's logic needs to be run on main thread.
    virtual void initialize();

    // Objects are grouped by shader (getShaderLabel), bind and unbind are called once per shader per frame
    virtual void bindShader(const OmnigenCamera& camera) = 0;

    // Drawing using currently bound shader goes here
    virtual void draw() = 0;

    // See bindShader
    virtual void unbindShader() = 0;

    virtual ShaderPipeline& getShaderPipeline() const = 0;
    virtual bool canBeSelected() const { return false; };

    bool operator==(const OmnigenDrawable& other) const
    {
        return guid == other.guid;
    }

    auto& getAllGeometries() { return geometry; }

    template<typename PointType = QVector3D>
    inline QSharedPointer<RenderGeometryData<PointType>> getActiveGeometry() const
    { 
        return getGeometry<PointType>(activeLOD);
    }

    template<typename PointType, typename InstanceData>
    inline QSharedPointer<InstancedRenderGeometryData<PointType, InstanceData>> getActiveInstancedGeometry() const
    {
        return getInstancedGeometry<PointType, InstanceData>(activeLOD);
    }

    template<typename PointType, typename InstanceData>
    inline QSharedPointer<InstancedRenderGeometryData<PointType, InstanceData>> getInstancedGeometry(ELOD ll = ELOD::Last) const
    {
        return geometry.find(ll)->staticCast<InstancedRenderGeometryData<PointType, InstanceData>>();
    }

    template<typename PointType = QVector3D>
    inline QSharedPointer<RenderGeometryData<PointType>> getGeometry(ELOD ll) const
    { 
        return geometry.find(ll)->staticCast<RenderGeometryData<PointType>>();
    }

    inline QSharedPointer<GeometryDataBase> getActiveBaseGeometry() const
    {
        return getBaseGeometry(activeLOD);
    }

    inline QSharedPointer<GeometryDataBase> getBaseGeometry(ELOD ll) const
    {
        return *geometry.find(ll);
    }

protected:
    virtual void cacheBoundingBox() = 0; // usually called when first rendered
    virtual void createShader() = 0;
    virtual void createShaderResources() {};
    virtual void createDefaultLodLevel() {};

    QMap<ELOD, QSharedPointer<GeometryDataBase>> geometry;
    BoundingBox cachedBoundingBox; // reset this when the object moves or changes

    mutable bool bIsCulled = false; // Used by the renderer
    qint64 guid;
    ELOD activeLOD = ELOD::Last;

private:
    FRIEND_OMNIBIN(OmnigenDrawable)
};

inline void omniSave(const OmnigenDrawable& object, OmniBin<std::ios::out>& omniBin)
{
    // Implement save on a higher level where final type of GeometryData is known
    Q_ASSERT(false);
    // geometry
    // guid
}

inline void omniLoad(OmnigenDrawable& object, OmniBin<std::ios::in>& omniBin)
{
    // Implement load on a higher level where final type of GeometryData is known
    Q_ASSERT(false);
    // geometry
    // guid
}
