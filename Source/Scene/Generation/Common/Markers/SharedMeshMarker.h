#pragma once
#include "BatchingMeshMarker.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include <sstream>

// Deprecated, use BatchingMeshMarker instead
template<typename VertexType = QVector3D>
class DSharedMeshMarker : public DMarker
{
public:
    DSharedMeshMarker(
        const QSharedPointer<RenderGeometryData<VertexType>>& inGeometry,
        decltype(GL_TRIANGLES) inPrimitiveType,
        const QVector4D& inColor = QVector4D(1, 1, 1, 1),
        ERenderType inRenderMode = ERenderType::Wireframe,
        const QVector3D& inRenderOffset = QVector3D())
        : batchParams({ inColor, inRenderOffset, inRenderMode, inPrimitiveType })
    {
        assignLodLevel(ELOD::Last, inGeometry);
    }

    ~DSharedMeshMarker()
    {
        getActiveGeometry<VertexType>()->vbo.destroy();
    }

    virtual bool shouldDraw(int vIdx) const override
    {
        return bShouldDraw<DSharedMeshMarker<>>[vIdx];
    }

    virtual void cacheBoundingBox() override
    {
        size_t byteOffset = 0;
        if constexpr (sizeof(VertexType) != sizeof(QVector3D))
            byteOffset = offsetof(VertexType, position);

        auto getPos = [byteOffset](const VertexType& v) -> const QVector3D&
        {
            if constexpr (sizeof(VertexType) == sizeof(QVector3D))
                return reinterpret_cast<const QVector3D&>(v);
            else
                return *reinterpret_cast<const QVector3D*>(reinterpret_cast<const BYTE*>(&v) + byteOffset);
        };

        cachedBoundingBox = BoundingBox::fromPoints(getActiveGeometry<VertexType>()->vertices, getPos);
    }

    virtual quint32 getShaderLabel() const override { return painter.getShaderLabel(); };
    virtual bool isSelected() const { return selected; };
    virtual bool isHovered() const { return hovered; };

    virtual void bindShader(const OmnigenCamera& camera) override
    {
        painter.bindShader(camera);
    }

    virtual void setSelected(bool isSelected)
    {
        selected = isSelected;
    }

    virtual void setHovered(bool isHovered)
    {
        hovered = isHovered;
    }

    virtual void draw() override
    {
        painter.draw(*getActiveGeometry<VertexType>(), batchParams);
    }

    virtual void unbindShader() override
    {
        painter.unbindShader();
    }

    void setColor(const QVector4D& inColor)
    {
        batchParams.color = inColor;
    }

    virtual ShaderPipeline& getShaderPipeline() const override { return painter.shaderPipeline; };

protected:
    DSharedMeshMarker() = default;

    virtual void createShader() override
    {
        painter.createShader();
    }

    MeshBatchParams<VertexType> batchParams;

    // do not save:
    bool selected = false;
    bool hovered = false;

    static inline MeshPainter<VertexType> painter;

    FRIEND_OMNIBIN_T(DSharedMeshMarker);
};

template<typename T>
inline void omniSave(const DSharedMeshMarker<T>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DMarker&>(object);
    omniBin << object.batchParams;
}

template<typename T>
inline void omniLoad(DSharedMeshMarker<T>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DMarker&>(object);
    omniBin >> object.batchParams;
}