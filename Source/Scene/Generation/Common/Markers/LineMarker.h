#pragma once
#include "MarkerDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "LineMarkerData.h"

// Deprecated, use BatchingLineMarker instead
class DLineMarker : public DMarker
{
protected:
    static inline ShaderPipeline shaderPipeline;
    static inline GLfloat lastLineWidth;

public:
    // Fully defnined lines
    DLineMarker(const std::vector<QVector3D>& inControlPoints, const QVector4D& inColor = QVector4D(1, 1, 1, 1), bool inIsLoop = false, float inHeight = 0.0f);
    DLineMarker(const std::vector<GVector2D>& inControlPoints, const QVector4D& inColor = QVector4D(1, 1, 1, 1), bool inIsLoop = false, float inHeight = 0.0f);
    // Vertical line upwards
    DLineMarker(const QVector3D& inControlPoint, const float inHeight = 10000, const QVector4D& inColor = QVector4D(1, 1, 1, 1), bool inIsLoop = false);
    // Arc / Arrow
    DLineMarker(const QVector3D& p1, const QVector3D& p2, const QVector4D& inColor = QVector4D(1, 1, 1, 1), float inHeight = 0.0f, ELineDecorator ld = ELineDecorator::Arc);

    // Copy ctor
    DLineMarker(const DLineMarker& other);

    // Assignment
    DLineMarker& operator=(const DLineMarker& other);

    // Drawable
    virtual void initialize() override;
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    virtual void extendMarker(const QVector3D& p);
    virtual void setPoints(const std::vector<QVector3D>& newVerts, bool isLoop = false);
    virtual void movePoints(const std::vector<QVector3D>& newVerts, int vertsAdded = 0);
    virtual void setSelected(bool b);
    virtual void setHovered(bool b);
    virtual void computeBoxVerts();

    // Rendering
    IMPLEMENT_SHOULD_DRAW();

    const std::vector<QVector3D>& getControlPoints() const;
    std::vector<GVector2D> get2DPoints() const;
    float getLength() const;
    bool isLoop() const { return bIsLoop; }

protected:
    DLineMarker() = default;
    virtual void createShader() override;
    virtual void computeLength() const;
    virtual void createDefaultLodLevel(const std::vector<QVector3D>& controlPoints);

    QVector4D color;
    std::optional<QVector4D> selectionColor = std::nullopt;
    bool bIsLoop;
    float height;

    mutable std::optional<float> length; // lazy impl
    mutable std::mutex lengthGuard;

    bool bSelected = false;
    bool bHovered = false;
    
    QSharedPointer<RenderGeometryData<>> selectionGeometry;

    FRIEND_OMNIBIN(DLineMarker);
};

struct LineMarkerPoint
{
    const DLineMarker* marker = nullptr;
    int idx = -1;

    const auto& getPoint() const { return marker->getControlPoints()[idx]; }
    explicit operator bool() const { return marker; }
};

class DCircleMarker : public DLineMarker
{
public:
    DCircleMarker(const GVector2D& center, float radius, const QVector4D& inColor = QVector4D(1, 1, 1, 1), float inHeight = 0.0f);
};

class DMultiLineMarker : public DLineMarker
{
public:
    DMultiLineMarker(const std::vector<std::vector<QVector3D>>& inLines, const QVector4D& inColor = QVector4D(1, 1, 1, 1), float inHeight = 0.0f);

    virtual void initialize() override;
};


inline void omniSave(const DLineMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const DMarker&>(object);
    omniBin << object.color;
    omniBin << object.height;
    omniBin << object.selectionGeometry;
}

inline void omniLoad(DLineMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<DMarker&>(object);
    omniBin >> object.color;
    omniBin >> object.height;
    omniBin >> object.selectionGeometry;
}

// side: -1 = right, 1 = left
int getLineSide(const LineMarkerPoint& closestLinePoint, const GVector2D& point);
int getLineSide(const std::vector<GVector2D>& pts, const GVector2D& point);