#include "stdafx.h"
#include "OmnigenCameraManager.h"
#include <QObject>
#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

QQuaternion OmnigenCamera::getRotation() const
{
    return QQuaternion::fromEulerAngles(pitch, yaw, 0.0f);
}

QVector3D OmnigenCamera::makeRayFromCursor(int x, int y)
{
    int vpIdx = QOmnigenViewportSection::getActiveViewport()->getViewportIndex();

    // Clamp to [-1; 1]
    float screenSpaceX = ((2.0f * x) / getViewportSize().x()) - 1.0f;
    float screenSpaceY = (((2.0f * y) / getViewportSize().y()) - 1.0f) * -1.0f;
    QVector4D rayClip = QVector4D(screenSpaceX, screenSpaceY, -1.0, 1.0);

    // Projection -> View
    QVector4D rayEye = getProjectionMatrix().inverted() * rayClip;
    // Go forward.
    rayEye.setZ(-1.0f);
    // This is a direction, not a position.
    rayEye.setW(0.0f);

    // View -> World
    QVector4D rayWorld = getViewMatrix().inverted() * rayEye;
    return QVector3D(rayWorld.x(), rayWorld.y(), rayWorld.z()).normalized();
}

const QVector3D& OmnigenCamera::getLookAt() const
{
    if (bNeedsViewUpdate)
        updateViewMtx();

    return lookAt;
}

const QMatrix4x4& OmnigenCamera::getViewMatrix() const
{
    if (bNeedsViewUpdate)
        updateViewMtx();

    return viewMatrix;
}

const QMatrix4x4& OmnigenCamera::getProjectionMatrix() const
{
    if (bNeedsProjectionUpdate)
        updateProjectionMtx();

    return projectionMatrix;
}

void OmnigenCamera::returnToSavedPosition()
{
    bNeedsViewUpdate = true;
    position = std::get<0>(returnPosition);
    pitch = std::get<1>(returnPosition);
    yaw = std::get<2>(returnPosition);
}

void OmnigenCamera::setPosition(const QVector3D& newPos)
{
    bNeedsViewUpdate = true;
    position = newPos;
}

void OmnigenCamera::setPitch(double newPitch)
{
    bNeedsViewUpdate = true;
    pitch = std::clamp(newPitch, -89.0, 89.0);
}

void OmnigenCamera::setYaw(double newYaw)
{
    bNeedsViewUpdate = true;
    yaw = newYaw;
}

void OmnigenCamera::setViewDistance(float newDistance)
{
    bNeedsProjectionUpdate = true;
    viewDistance = newDistance;
}

void OmnigenCamera::setViewMinDistance(float newDistance)
{
    bNeedsProjectionUpdate = true;
    viewMinDistance = newDistance;
}

void OmnigenCamera::setViewAngle(float newAngle)
{
    bNeedsProjectionUpdate = true;
    viewAngle = newAngle;
    angleTang = tan(viewAngle * std::numbers::pi / 360.0);
}

void OmnigenCamera::setCameraSpeed(float newSpeed)
{
    cameraSpeed = newSpeed;
}

void OmnigenCamera::setCameraItemPosition(int pos)
{
    cameraItemPosition = pos;
}

void OmnigenCamera::setCameraThumbnail(QPixmap tn)
{
    thumbnail = tn;
}

void OmnigenCamera::setReturnPosition()
{
    returnPosition = std::make_tuple(position, pitch, yaw);
}

void OmnigenCamera::reset()
{
    *this = OmnigenCamera();
}

bool OmnigenCamera::isPointInFrustum(const QVector3D& p) const
{
    return testPointAgainstFrustum(p).isNull();
}

bool OmnigenCamera::isBoxInFrustum(const BoundingBox& bb) const
{
    // xyz
    auto xyz = testPointAgainstFrustum(bb.nbl);
    if (xyz.isNull())
        return true;

    // Xyz 
    auto Xyz = testPointAgainstFrustum(bb.nbl + QVector3D(bb.sizes.x(), 0, 0));
    if (Xyz.isNull())
        return true;

    // xYz 
    auto xYz = testPointAgainstFrustum(bb.nbl + QVector3D(0, bb.sizes.y(), 0));
    if (xYz.isNull())
        return true;

    // Xyz 
    auto xyZ = testPointAgainstFrustum(bb.nbl + QVector3D(0, 0, bb.sizes.z()));
    if (xyZ.isNull())
        return true;

    // XYz
    auto XYz = testPointAgainstFrustum(bb.nbl + QVector3D(bb.sizes.x(), bb.sizes.y(), 0));
    if (XYz.isNull())
        return true;

    // xYZ
    auto xYZ = testPointAgainstFrustum(bb.nbl + QVector3D(0, bb.sizes.y(), bb.sizes.z()));
    if (xYZ.isNull())
        return true;

    // XyZ
    auto XyZ = testPointAgainstFrustum(bb.nbl + QVector3D(bb.sizes.x(), 0, bb.sizes.z()));
    if (XyZ.isNull())
        return true;

    // XYZ
    auto XYZ = testPointAgainstFrustum(bb.nbl + bb.sizes);
    if (XYZ.isNull())
        return true;

    auto sidesCount = xyz + Xyz + xYz + xyZ + XYz + xYZ + XyZ + XYZ;
    if (std::abs(sidesCount.x()) == 8 || std::abs(sidesCount.y()) == 8 || std::abs(sidesCount.z()) == 8)
        return false;

    return true;
}

QVector3D OmnigenCamera::testPointAgainstFrustum(const QVector3D& p) const
{
    QVector3D result;

    // compute vector from camera position to p
    QVector3D v = p - position;

    // compute and test the Z coordinate
    float pcz = QVector3D::dotProduct(v, -axes[2]);
    if (pcz > viewDistance)
        result.setZ(1);
    else if (pcz < viewMinDistance)
        result.setZ(-1);

    // compute and test the Y coordinate
    float pcy = QVector3D::dotProduct(v, axes[1]);
    float aux = pcz * angleTang;
    if (pcy > aux)
        result.setY(1);
    else if (pcy < -aux)
        result.setY(-1);

    // compute and test the X coordinate
    float pcx = QVector3D::dotProduct(v, axes[0]);
    aux = aux * ratio;
    if (pcx > aux)
        result.setX(1);
    else if (pcx < -aux)
        result.setX(-1);

    return result;
}

void OmnigenCamera::updateViewMtx() const
{
    lookAt = getRotation().rotatedVector(QVector3D(0, 0, 1));
    QVector3D cameraFocus = position + lookAt;

    viewMatrix.setToIdentity();
    viewMatrix.lookAt(position, cameraFocus, QVector3D(0,1,0));

    // Z axis
    axes[2] = (position - cameraFocus).normalized();
    // X axis
    axes[0] = QVector3D::crossProduct(QVector3D(0, 1, 0), axes[2]).normalized();
    // Y axis
    axes[1] = QVector3D::crossProduct(axes[2], axes[0]);

    bNeedsViewUpdate = false;
}

void OmnigenCamera::updateProjectionMtx() const
{
    projectionMatrix.setToIdentity();
    ratio = viewportSize.x() / viewportSize.y();
    projectionMatrix.perspective(VIEW_ANGLE, viewportSize.x() / viewportSize.y(), viewMinDistance, viewDistance);

    bNeedsProjectionUpdate = false;
}

void OmnigenCamera::showFrustum()
{
    const float halfVSide = viewDistance * tanf(viewAngle * .5f);
    const float halfHSide = halfVSide * ratio;

    QVector3D farCenter = position + -axes[2] * viewDistance;

    QVector3D topLeft = farCenter - axes[0] * halfHSide + axes[1] * halfVSide;
    QVector3D topRight = farCenter + axes[0] * halfHSide + axes[1] * halfVSide;
    QVector3D botLeft = farCenter - axes[0] * halfHSide - axes[1] * halfVSide;
    QVector3D botRight = farCenter + axes[0] * halfHSide - axes[1] * halfVSide;
    spawn<DLineMarker>(std::vector{ position, topLeft });
    spawn<DLineMarker>(std::vector{ position, topRight });
    spawn<DLineMarker>(std::vector{ position, botRight });
    spawn<DLineMarker>(std::vector{ position, botLeft });

    auto&& ihs = gBatchingMarkerInstance<IsohypseBatchParams>;
    auto&& [batches, guard] = ihs->getBatches();
    auto&& batch = batches.begin()->second;
    auto&& bbox = batch.getBoundingBox();

    auto ftr = bbox.nbl + bbox.sizes;
    std::vector<std::vector<QVector3D>> lines
    {
        { bbox.nbl, bbox.nbl + QVector3D(bbox.sizes.x(), 0, 0) },
        { bbox.nbl, bbox.nbl + QVector3D(0, bbox.sizes.y(), 0) },
        { bbox.nbl, bbox.nbl + QVector3D(0, 0, bbox.sizes.z()) },

        { ftr, ftr - QVector3D(bbox.sizes.x(), 0, 0) },
        { ftr, ftr - QVector3D(0, bbox.sizes.y(), 0) },
        { ftr, ftr - QVector3D(0, 0, bbox.sizes.z()) },
    };
    spawn<DMultiLineMarker>(lines, Colors::yellow);
}

void OmnigenCamera::setViewportSize(const QVector2D& newSize)
{
    viewportSize = newSize;

    bNeedsProjectionUpdate = true;
}

QScopedPointer<OmnigenCameraMgr> OmnigenCameraMgr::sInstance;

void omniSave(const OmnigenCameraMgr& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.cameras;
    omniBin << object.activeCameraMap;
}

void omniLoad(OmnigenCameraMgr& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.cameras;
    omniBin >> object.activeCameraMap;
}

void omniSave(const OmnigenCamera& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.position;
    omniBin << object.pitch;
    omniBin << object.yaw;
    omniBin << object.viewDistance;
    omniBin << object.viewMinDistance;
    omniBin << object.viewAngle;
    omniBin << object.angleTang;
    omniBin << object.cameraSpeed;
    omniBin << object.cameraItemPosition;
    omniBin << object.thumbnail;
    omniBin << object.returnPosition;
}

void omniLoad(OmnigenCamera& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.position;
    omniBin >> object.pitch;
    omniBin >> object.yaw;
    omniBin >> object.viewDistance;
    omniBin >> object.viewMinDistance;
    omniBin >> object.viewAngle;
    omniBin >> object.angleTang;
    omniBin >> object.cameraSpeed;
    omniBin >> object.cameraItemPosition;
    omniBin >> object.thumbnail;
    omniBin >> object.returnPosition;
}

void omniSave(const QPixmap& object, OmniBin<std::ios::out>& omniBin)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    object.save(&buffer, "PNG");

    omniSave(bytes.size(), omniBin);
    buffer.close();
    omniBin.stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void omniLoad(QPixmap& object, OmniBin<std::ios::in>& omniBin)
{
    QByteArray bytes;
    int s;
    omniLoad(s, omniBin);
    bytes.resize(s);
    omniBin.stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    object.loadFromData(bytes, "PNG");
}

OmnigenCameraMgr::OmnigenCameraMgr()
{
    std::vector<QString> initCameras = { "Camera #1", "Camera #2", "Camera #3", "Camera #4" };
    for (auto&& camName : initCameras)
    {
        addCamera(camName);
        getCamera(camName)->setCameraItemPosition(int(cameras.size()) - 1);
        activeCameraMap.insert(indexOf(initCameras, camName), camName);
    }
}

OmnigenCamera* OmnigenCameraMgr::getCullingCamera(int idx)
{
    if (cullCamera)
        return cullCamera;

    return getActiveCamera(idx);
}

OmnigenCamera* OmnigenCameraMgr::getActiveCamera(int idx)
{
    return getCamera(activeCameraMap.value(idx)).get();
}


QString OmnigenCameraMgr::getActiveCameraName(int idx)
{
    auto it = activeCameraMap.find(idx);
    return it != activeCameraMap.end() ? (*it) : QString();
}


OmnigenCamera* OmnigenCameraMgr::getCameraForActiveViewport()
{
    return getActiveCamera(QOmnigenViewportSection::getActiveViewport()->getViewportIndex());
}

bool OmnigenCameraMgr::changeActiveCameraForViewport(int viewportIdx, const QString& newCamera)
{
    auto it = cameras.find(newCamera);
    if (it == cameras.end())
        return false;

    activeCameraMap.insert(viewportIdx, newCamera);
    return true;
}


void OmnigenCameraMgr::setThumbnailForCamera(const QString& camName)
{
    auto viewport = Omnigen::get()->getAllViewports().value(activeCameraMap.key(camName));

    QPixmap snapshot = viewport->grab().scaled(QSize(75, 42), Qt::IgnoreAspectRatio);
    QIcon thumbnail = QIcon(snapshot);

    getCamera(camName)->setCameraThumbnail(snapshot);
}

QSharedPointer<OmnigenCamera> OmnigenCameraMgr::getCamera(const QString& name)
{
    auto it = cameras.find(name);
    return it != cameras.end() ? *it : nullptr;
}

bool OmnigenCameraMgr::addCamera(const QString& name, QSharedPointer<OmnigenCamera> newCamera)
{
    auto it = cameras.find(name);
    if (it != cameras.end())
        return false;

    cameras[name] = std::move(newCamera);

    emit cameraStateChanged();
    return true;
}

void OmnigenCameraMgr::removeCamera(const QString& name)
{
    cameras.remove(name);
}

void OmnigenCameraMgr::changeCameraName(const QString& newName, const QString& oldName)
{
    auto it = cameras.find(oldName);

    cameras.insert(newName, it.value());

    for (auto&& it = activeCameraMap.keyValueBegin(); it != activeCameraMap.keyValueEnd(); ++it)
        if ((*it).second == oldName)
            activeCameraMap.insert((*it).first, newName);

    cameras.remove(oldName);
}

bool OmnigenCameraMgr::cloneCamera(const QString& newName, const QString& camToClone)
{
    if (!addCamera(newName))
        return false;

    auto newCam = getCamera(newName);
    auto prevCam = getCamera(camToClone);

    newCam->setCameraItemPosition(getAllCameras().size());
    newCam->setPosition(prevCam->getPosition());
    newCam->setPitch(prevCam->getPitch());
    newCam->setYaw(prevCam->getYaw());
    newCam->setViewDistance(prevCam->getViewDistance());
    newCam->setCameraSpeed(prevCam->getCameraSpeed());
    newCam->setReturnPosition();

    changeActiveCameraForViewport(getAllActiveCameras().key(camToClone), newName);
    newCam->returnToSavedPosition();

    QOmnigenViewportSection::repaintAll(true);
    setThumbnailForCamera(newName);

    return true;
}

std::optional<GVector2D> OmnigenCameraMgr::findPointInWorld(float height, int mouseX, int mouseY)
{
    auto* camera = getCameraForActiveViewport();
    auto&& cameraPos = camera->getPosition();
    const auto rayVec = camera->makeRayFromCursor(mouseX, mouseY);

    if (qAbs(rayVec.y()) < 0.001)
        return {};

    const float lookDiff = (height - cameraPos.y());
    if (lookDiff > 0)
        return {};

    const auto factor = lookDiff / rayVec.y();
    GVector2D point = GVector2D(cameraPos) + (GVector2D(rayVec) * factor);

    const float maxCoord = getMaxGridCoord();

    // Discard result if point found outside allowed area
    if (point.x > maxCoord || point.z > maxCoord || point.x < 0 || point.z < 0)
        return {};

    return point;
}

std::optional<GPoint> OmnigenCameraMgr::findGridPoint(int mouseX, int mouseY)
{
    if (auto result = findPointInWorld(0.0f, mouseX, mouseY); result)
        return (*result).toGPoint();

    return {};
}

bool OmnigenCameraMgr::isSpriteHit(const QVector3D& spritePosition, float spriteSize, int mouseX, int mouseY)
{
    auto* camera = getCameraForActiveViewport();

    float screenSpaceX = ((2.0f * mouseX) / camera->getViewportSize().x()) - 1.0f;
    float screenSpaceY = (((2.0f * mouseY) / camera->getViewportSize().y()) - 1.0f) * -1.0f;

    const QMatrix4x4& viewMatrix = camera->getViewMatrix();
    const QMatrix4x4& projectionMatrix = camera->getProjectionMatrix();

    QVector3D center = viewMatrix * spritePosition;
    QVector2D corner;

    // left-bottom
    corner = QVector2D(center.x(), center.y()) + QVector2D(-0.5, -0.5) * spriteSize;
    QVector3D left_bot = projectionMatrix * QVector3D(corner.x(), corner.y(), center.z());
    // right-top
    corner = QVector2D(center.x(), center.y()) + QVector2D(0.5, 0.5) * spriteSize;
    QVector3D right_top = projectionMatrix * QVector3D(corner.x(), corner.y(), center.z());

    bool belowMinX = screenSpaceX <= left_bot.x();
    bool belowMinY = screenSpaceY <= left_bot.y();
    bool aboveMaxX = screenSpaceX >= right_top.x();
    bool aboveMaxY = screenSpaceY >= right_top.y();
    if (belowMinX || belowMinY || aboveMaxX || aboveMaxY)
        return false;

    return true;
}
