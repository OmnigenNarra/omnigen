#pragma once
#include "MarkerDrawable.h"

class QOpenGLFunctions;

// Deprecated, use BatchingMeshMarker or implement BatchingPolygonMarker; BatchingCellMarker is a special case
class DPolygonMarker : public DMarker
{
protected:
    static inline ShaderPipeline shaderPipeline;

public:
    DPolygonMarker(const std::vector<QVector3D>& inControlPoints, float inHeight = 0.0f, const QVector4D& inColor = QVector4D(1, 1, 1, 1));

    // Drawable
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual void setSelected(bool isSelected);
    virtual void setHovered(bool isHovered);
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    const auto& getControlPoints() const { return getActiveGeometry()->vertices; }
    const auto& getHeight() const { return height; }

	// Rendering
	IMPLEMENT_SHOULD_DRAW();

protected:
    DPolygonMarker() = default;
    virtual void createShader() override;
    virtual void createDefaultLodLevel(const std::vector<QVector3D>& inControlPoints);
    virtual const QVector4D& getColor() { return color; }

    QVector4D color;
    float height;

    // do not save:
    bool selected = false;
    bool hovered = false;

    FRIEND_OMNIBIN(DPolygonMarker);
};

inline void omniSave(const DPolygonMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DMarker&>(object);
    omniBin << object.color;
    omniBin << object.height;
}

inline void omniLoad(DPolygonMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DMarker&>(object);
    omniBin >> object.color;
    omniBin >> object.height;
}

class DPolygonWithHolesMarker : public DPolygonMarker
{
public:
    DPolygonWithHolesMarker(const std::vector<QVector3D>& inControlPoints, const std::vector<std::vector<QVector3D>>& inCutPolygons, float inHeight = 0.0f, const QVector4D& inColor = QVector4D(1, 1, 1, 1));

    const auto& getMainPolygon() const { return mainPolygon; }
    const auto& getCutPolygons() const { return cutPolygons; }

    // Rendering
    IMPLEMENT_SHOULD_DRAW();

protected:
    DPolygonWithHolesMarker() = default;
    virtual void createDefaultLodLevel(const std::vector<QVector3D>& inControlPoints) override;

    std::vector<QVector3D> mainPolygon;
    std::vector<std::vector<QVector3D>> cutPolygons;

    FRIEND_OMNIBIN(DPolygonWithHolesMarker);
};

inline void omniSave(const DPolygonWithHolesMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DPolygonMarker&>(object);
    omniBin << object.mainPolygon;
    omniBin << object.cutPolygons;
}

inline void omniLoad(DPolygonWithHolesMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DPolygonMarker&>(object);
    omniBin >> object.mainPolygon;
    omniBin >> object.cutPolygons;
}