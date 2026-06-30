#pragma once
#include <memory>
#include <QVector3D>
#include "Scene/Core/EditorGridDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include <numbers>
#include "Editor/Sections/Viewport/OmnigenViewport.h"

const QVector3D STARTING_CAMERA_POSITION(GRID_SEGMENT_WIDTH* GRID_SEGMENT_COUNT * 0.475f, GRID_SEGMENT_WIDTH * 3, GRID_SEGMENT_WIDTH* GRID_SEGMENT_COUNT * 0.475f);
const QVector3D STARTING_CAMERA_UP(0, 1, 0);
const double STARTING_CAMERA_PITCH = 60.0;
const double STARTING_CAMERA_YAW = 45.0;
const float VIEW_DISTANCE = 500000.0f;
const float VIEW_MIN_DISTANCE = 100.0f;

const float VIEW_ANGLE = 90.0f;
const float CAMERA_SPEED = 5 * GRID_SEGMENT_WIDTH;

class OmnigenCamera
{
public:
    OmnigenCamera() = default;

    // Getters
    const auto& getPosition() const { return position; }
    const auto& getPitch() const { return pitch; }
    const auto& getYaw() const { return yaw; }
    QQuaternion getRotation() const;
    const auto& getViewDistance() const { return viewDistance; }
    const auto& getViewMinDistance() const { return viewMinDistance; }
    const auto& getViewAngle() const { return viewAngle; }
    const auto& getCameraSpeed() const { return cameraSpeed; };
    const auto& getCameraItemPosition() const { return cameraItemPosition; };
    const auto& getCameraThumbnail() const { return thumbnail; };
    QVector3D makeRayFromCursor(int x, int y);

    const QVector3D& getLookAt() const;
    const QMatrix4x4& getViewMatrix() const;
    const QMatrix4x4& getProjectionMatrix() const;
    const auto getViewProjectionMatrix() const { return getProjectionMatrix() * getViewMatrix(); };
    const auto& getViewportSize() const { return viewportSize; }

    void returnToSavedPosition();

    // Setters
    virtual void setPosition(const QVector3D& newPos);
    void setPitch(double newPitch);
    void setYaw(double newYaw);
    void setViewDistance(float newDistance);
    void setViewMinDistance(float newDistance);
    void setViewAngle(float newAngle);
    void setCameraSpeed(float newSpeed);
    void setCameraItemPosition(int pos);
    void setCameraThumbnail(QPixmap tn);
    void setReturnPosition();
    void setViewportSize(const QVector2D& newSize);

    void reset();
    bool isPointInFrustum(const QVector3D& p) const;
    bool isBoxInFrustum(const BoundingBox& bb) const;

    void showFrustum();

protected:
    // @returns {x,y,z}, where each component is:
    //      0: ok, in frustum by this coord
    //      -1: outside frustum, on the negative side
    //      1: outside frustum, on the positive side
    QVector3D testPointAgainstFrustum(const QVector3D& p) const;

    void updateViewMtx() const;
    void updateProjectionMtx() const;

    QVector3D position = STARTING_CAMERA_POSITION;
    double pitch = STARTING_CAMERA_PITCH;
    double yaw = STARTING_CAMERA_YAW;
    float viewDistance = VIEW_DISTANCE;
    float viewMinDistance = VIEW_MIN_DISTANCE;
    float viewAngle = VIEW_ANGLE;
    double angleTang = tan(VIEW_ANGLE * std::numbers::pi / 360.0);
    float cameraSpeed = CAMERA_SPEED;
    int cameraItemPosition;
    QPixmap thumbnail = QPixmap(QSize(75,50)); 

    std::tuple<QVector3D, double, double> returnPosition = std::make_tuple(STARTING_CAMERA_POSITION, STARTING_CAMERA_PITCH, STARTING_CAMERA_YAW);
    QVector2D viewportSize = {0, 0};

    mutable bool bNeedsViewUpdate = true;
    mutable bool bNeedsProjectionUpdate = true;

    mutable QVector3D lookAt;
    mutable QMatrix4x4 viewMatrix;
    mutable QMatrix4x4 projectionMatrix;
    mutable float ratio;
    mutable QVector3D axes[3];

    friend class OmnigenCameraMgr;

    FRIEND_OMNIBIN(OmnigenCamera);
};

void omniSave(const OmnigenCamera& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenCamera& object, OmniBin<std::ios::in>& omniBin);

// Many cameras <=> many viewports
class OmnigenCameraMgr : public QObject
{
    Q_OBJECT

    static QScopedPointer<OmnigenCameraMgr> sInstance;

public:
    OmnigenCameraMgr();

    static OmnigenCameraMgr* get(bool reset = false)
    {
        if (!sInstance || reset)
            sInstance.reset(new OmnigenCameraMgr);

        return sInstance.get();
    }

    OmnigenCamera* getCullingCamera(int idx);
    OmnigenCamera* getActiveCamera(int idx);
    QString getActiveCameraName(int idx);
    OmnigenCamera* getCameraForActiveViewport();
    const auto& getAllActiveCameras() const { return activeCameraMap; }

    bool changeActiveCameraForViewport(int viewportIdx, const QString& newCamera);
    void setThumbnailForCamera(const QString& camName);

    QSharedPointer<OmnigenCamera> getCamera(const QString& name);
    const auto& getAllCameras() const { return cameras; }
    bool addCamera(const QString& name, QSharedPointer<OmnigenCamera> newCamera = QSharedPointer<OmnigenCamera>::create());
    void removeCamera(const QString& name);
    void changeCameraName(const QString& newName, const QString& oldName);
    bool cloneCamera(const QString& newName, const QString& camToClone);

    TODO("Move all queries to Camera class")
    // Find point on given height in acceptable grid geometry that the cursor is pointing at 
    std::optional<GVector2D> findPointInWorld(float height, int mouseX, int mouseY);

    // Find grid point in acceptable grid geometry that the cursor is pointing at 
    std::optional<GPoint> findGridPoint(int mouseX, int mouseY);

    // Check if cursor is pointing at sprite
    bool isSpriteHit(const QVector3D& spritePosition, float spriteSize, int mouseX, int mouseY);

    OmnigenCamera* cullCamera = nullptr;

signals:
    void cameraStateChanged();

private:
    QMap<QString, QSharedPointer<OmnigenCamera>> cameras;
    QMap<int, QString> activeCameraMap;

    FRIEND_OMNIBIN(OmnigenCameraMgr);
};

void omniSave(const OmnigenCameraMgr& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenCameraMgr& object, OmniBin<std::ios::in>& omniBin);
