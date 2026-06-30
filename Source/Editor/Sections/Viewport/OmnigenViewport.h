#pragma once
#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QTimer>

#include "Scene/OmnigenDrawable.h"

class QOpenGLShader;
class QOpenGLTexture;
class QPushButton;

// Constants
const QVector3D BG_COLOR(0.3f, 0.3f, 0.3f);

// The view and the renderer in one
// Up to 4 viewports may be active at the same time
class QOmnigenViewport : public QOpenGLWidget, public QOpenGLExtraFunctions
{
    Q_OBJECT

public:
    QOmnigenViewport(QWidget* parent = nullptr, bool focus = false, int index = 0);
    void initialize();

    void setIsActiveViewport(bool b) { isActiveViewport = b; };
    const int getViewportIndex() const { return viewportIndex; };

    // Objects on scene are shared between all viewports
    static void registerDrawable(QSharedPointer<OmnigenDrawable> drawable);
    static void unregisterDrawable(QSharedPointer<OmnigenDrawable> drawable);

    static void updateDrawable(QSharedPointer<OmnigenDrawable> drawable);

    static void clearDrawables();
    static void clearDrawables(const ERenderPriority& inCategory);

    struct SelectionData
    {
        std::vector<QSharedPointer<OmnigenDrawable>> selectionMap;
        std::vector<QVector4D> selectionBuffer;
    };
    SelectionData getSelectionData() const;

    OmnigenCamera* getActiveCamera();

    // Resets camera
    void resetView();

    // Does a camera animation that zooms in onto target location
    void tryMoveToSelection();
    void focusOnLocation(const QVector3D& location);

    // Used in repaint event
    void singleDrawBegin();
    void singleDrawEnd();

    void startPIE(const QVector3D& startingPoint);
    void endPIE();

protected:
    static inline QMap<ERenderPriority, QMap<quint32, std::vector<QSharedPointer<OmnigenDrawable>>>> drawables;
    static inline std::vector<QSharedPointer<OmnigenDrawable>> drawablesToUpdate;

    // Rendering
    virtual void initializeGL()                         override;
    virtual void paintGL()                              override;
    void drawScene();
    void updateLODs();

    // Gets the first valid object of all objects using the same shader
    std::optional<QSharedPointer<OmnigenDrawable>> getDrawableRepresentative(const std::vector<QSharedPointer<OmnigenDrawable>>& vec);

    // Event control
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event)    override;
    virtual void mouseMoveEvent(QMouseEvent* event)     override;
    virtual void mouseReleaseEvent(QMouseEvent* event)  override;
    virtual void keyPressEvent(QKeyEvent* event)        override;
    virtual void keyReleaseEvent(QKeyEvent* event)      override;
    virtual void wheelEvent(QWheelEvent* event)         override;
    virtual void resizeEvent(QResizeEvent* event)       override;

    void tick();
    void scarceTick();

    // Selection
    static inline ShaderPipeline selectionPipeline;
    void updateSelectionBuffers();
    void grabSelectionData();

    mutable GLuint selectionTexture;
    mutable std::vector<QVector4D> selectionBuffer;
    mutable std::vector<QSharedPointer<OmnigenDrawable>> selectionMap;

    mutable std::mutex selectionDataGuard;
	mutable QSize cachedViewportSize;
	mutable int cachedSelectionRadius = 0;
	mutable int cachedObjectCount = 0;

    // Multi Viewport
    bool isActiveViewport;
    bool shouldRepaint = false;
    int viewportIndex;

    //PIE
    QSharedPointer<class TestPlayer> PIECamera;
    QString resetCameraName;
    QPoint lastMouseRealPos;
    float PIESens = 10.f;
    float pitch = 0.0, yaw = 0.0;

    // Aux
    bool bInitialized = false;
    QPoint lastMousePos;
    QVector3D cameraMovement;
    bool isAutoMoving = false;
    float autoMoveProgress;
    QPair<QVector3D, QVector3D> currentMovePath;
    QPair<QQuaternion, QQuaternion> currentRotationPath;
    QTimer ticker, scarceTicker;
    int frameMs = 16;
    mutable int objectsDrawn = 0;
};
