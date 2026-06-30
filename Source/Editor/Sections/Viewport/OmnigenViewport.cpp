#include "stdafx.h"
#include "OmnigenViewport.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLShader>
#include <QVector4D>
#include <QPushButton>
#include <QtMath>
#include <QPainter>
#include <QGuiApplication>
#include <QApplication>

#include "Utils/PlatformMisc.h"
#include "Scene/Core/EditorGridDrawable.h"
#include "Omnigen.h"
#include "Editor/Sections/History/History.h"
#include "Utils/CoreUtils.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "Editor/Dialogs/Preferences/OmnigenPreferences.h"
#include "OmnigenCameraManager.h"
#include "Editor/Sections/CameraSystem/TestPlayer.h"
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Scene/Generation/Stages/Layout/DomainSquareDrawable.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

#include <tbb/parallel_reduce.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

QOmnigenViewport::QOmnigenViewport(QWidget *parent, bool focus, int index) 
    : QOpenGLWidget(parent)
    , isActiveViewport(focus)
    , viewportIndex(index)
{
    setAttribute(Qt::WA_AlwaysStackOnTop);
}

void QOmnigenViewport::initialize()
{
    if (bInitialized)
        return;

    OmnigenCameraMgr::get()->getActiveCamera(viewportIndex)->setViewportSize({ float(width()), float(height()) });

    // Setup tick function
    connect(&ticker, &QTimer::timeout, this, QOverload<>::of(&QOmnigenViewport::tick));
    ticker.setInterval(frameMs); // 60 FPS
    ticker.start();

    connect(&scarceTicker, &QTimer::timeout, this, QOverload<>::of(&QOmnigenViewport::scarceTick));
    scarceTicker.setInterval(100);
    scarceTicker.start();
    
    bInitialized = true;
}

void QOmnigenViewport::registerDrawable(QSharedPointer<OmnigenDrawable> drawable)
{
    ERenderPriority prio = drawable->getRenderPriority();
    drawables[prio][drawable->getShaderLabel()].push_back(drawable);

    QOmnigenViewportSection::repaintAll();
}

void QOmnigenViewport::unregisterDrawable(QSharedPointer<OmnigenDrawable> drawable)
{
    auto&& target = drawables[drawable->getRenderPriority()][drawable->getShaderLabel()];
    remove_single_if(target, [&drawable](auto&& ptr) { return ptr == drawable; });

    QOmnigenViewportSection::repaintAll();
}

void QOmnigenViewport::updateDrawable(QSharedPointer<OmnigenDrawable> drawable)
{
    drawablesToUpdate.push_back(drawable);
}

void QOmnigenViewport::clearDrawables()
{
    while (true)
    {
        auto noncore_it = std::find_if(drawables.keyBegin(), drawables.keyEnd(), 
            [](auto&& key) 
            { 
                return (key != ERenderPriority::Skybox) && (key != ERenderPriority::Grid) && (key != ERenderPriority::Gizmo);
            });

        if (noncore_it == drawables.keyEnd())
            break;

        drawables.remove(*noncore_it);
    }
}

void QOmnigenViewport::clearDrawables(const ERenderPriority& inCategory)
{
    const auto drawablesCategory = drawables.find(inCategory);

    if (drawablesCategory != drawables.end())
    {
        drawablesCategory.value().clear();
    }
}

OmnigenCamera* QOmnigenViewport::getActiveCamera()
{
        return OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);
}

void QOmnigenViewport::initializeGL()
{
    initializeOpenGLFunctions();

    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D_ARRAY);
    glDepthFunc(GL_LESS);
    glClearDepth(1);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_GEOMETRY_SHADER);
    glLineWidth(5);
    glPointSize(5);
    glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);

    initialize();
}

void QOmnigenViewport::paintGL()
{
    if (Omnigen::get()->isGenerating())
        return;

    if (!shouldRepaint)
        if (!isActiveViewport)
            return;

	QPainter painter;
	painter.begin(this);
	painter.beginNativePainting();
    ///////////////////////////////////////////////////////////////////////////////////
    // BEGIN DRAW SECTION

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    drawScene();

    // END DRAW SECTION
    ////////////////////////////////////////////////////////////////////////////////////
    painter.endNativePainting();
    painter.end();
	update();

    singleDrawEnd();
}

void QOmnigenViewport::drawScene()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto* camera = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);
    auto* cullCamera = OmnigenCameraMgr::get()->getCullingCamera(viewportIndex);		

    for (auto&& drawable : drawablesToUpdate)
        drawable->updateVbo(drawable->getActiveLOD());
    drawablesToUpdate.clear();

	// Parallel culling
	for (auto&& drawablesOfSamePriority : drawables)
        for (auto&& drawablesOfSameType : drawablesOfSamePriority)
        {
            auto representative = getDrawableRepresentative(drawablesOfSameType);
            if (!representative)
                continue;

            // Instanced geometry must be processed in a single thread
            if ((*representative)->getActiveBaseGeometry()->instanceSize() > 0)
            {
                for (auto&& drawable : drawablesOfSameType)
                    drawable->updateCullStatusInstanced(*cullCamera, viewportIndex);
            }
            else
            {
                tbb::parallel_for(0, int(drawablesOfSameType.size()), [&](int i)
                    {
                        drawablesOfSameType[i]->updateCullStatus(*cullCamera, viewportIndex);
                    });
            }
        }

	// Selection buffers size should match current viewport size etc
    updateSelectionBuffers();

    static const std::array<GLuint, 2> drawBuffers = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    static const std::array<float, 4> selectionClearColor = { 0.f, 0.f, 0.f, 0.f };
    static const std::array<float, 4> renderClearColor = { BG_COLOR.x(), BG_COLOR.y(), BG_COLOR.z(), 1.0f };

    // Draw to both standard frame buffer and selection buffer
    glDrawBuffers(drawBuffers.size(), drawBuffers.data());
    // Setup selection buffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, selectionTexture, 0);
    // Clear
    glClearBufferfv(GL_COLOR, 0, renderClearColor.data());
    glClearBufferfv(GL_COLOR, 1, selectionClearColor.data());
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    int objectID = 0;
    {
        std::scoped_lock lock(selectionDataGuard);
        selectionMap.clear();
        selectionMap.reserve(cachedObjectCount);
        for (auto&& drawablesOfSamePriority : drawables)
        {
            for (auto&& drawablesOfSameType : drawablesOfSamePriority)
            {
                auto representative = getDrawableRepresentative(drawablesOfSameType);
                if (!representative)
                    continue;

                (*representative)->bindShader(*camera);
                auto&& shader = (*representative)->getShaderPipeline();

                for (auto&& drawable : drawablesOfSameType)
                    if (!drawable->isCulled())
                    {
                        shader.setUniformValue(EShaderUniform::ObjectID, objectID);
                        selectionMap.push_back(drawable);
                        drawable->draw();
                        ++objectID;
                    }

                (*representative)->unbindShader();
            }
        }
    }
    cachedObjectCount = objectID;

 	grabSelectionData();
    glDisable(GL_BLEND);
}

void QOmnigenViewport::updateSelectionBuffers()
{
	QSize currentSize = { width(), height() };
	if (cachedViewportSize != currentSize)
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
		glDeleteTextures(1, &selectionTexture);
		glGenTextures(1, &selectionTexture);
		glBindTexture(GL_TEXTURE_2D, selectionTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, currentSize.width(), currentSize.height(), 0, GL_RGBA, GL_FLOAT, nullptr);
		glFinish();
		Q_ASSERT(selectionTexture > 0);

        cachedViewportSize = currentSize;
	}

    int selectionRadius = Design::getSelectionMgr()->getSelectionRadius();
    if (cachedSelectionRadius != selectionRadius)
    {
        std::scoped_lock lock(selectionDataGuard);
        selectionBuffer.resize(std::pow(2 * selectionRadius, 2));
        cachedSelectionRadius = selectionRadius;
    }
}

void QOmnigenViewport::grabSelectionData()
{
    // Get selection data from a small subsection of buffer around the cursor
	while (glGetError());

    auto cursorPos = mapFromGlobal(QCursor::pos());
    cursorPos.setY(height() - cursorPos.y());

    int minX = std::clamp(cursorPos.x() - cachedSelectionRadius, 0, width());
    int maxX = std::clamp(cursorPos.x() + cachedSelectionRadius, 0, width());
	int minY = std::clamp(cursorPos.y() - cachedSelectionRadius, 0, height());
    int maxY = std::clamp(cursorPos.y() + cachedSelectionRadius, 0, height());

    int sizeX = maxX - minX;
    int sizeY = maxY - minY;
    if (sizeX == 0 || sizeY == 0)
        return;
	
    // Output is mirrored here, but it doesn't matter
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    int selectionDiameter = 2 * cachedSelectionRadius;
    glReadPixels(minX, minY, sizeX, sizeY, GL_RGBA, GL_FLOAT, &selectionBuffer[0]);
}

void QOmnigenViewport::tick()
{
    if (Omnigen::get()->isGenerating() || !isActiveViewport)
        return;

    const auto pieCamera = dynamic_cast<TestPlayer*>(OmnigenCameraMgr::get()->getActiveCamera(viewportIndex));
    if (pieCamera && Omnigen::get()->hasFocus())
    {
        QPoint p = mapFromGlobal(QCursor::pos());

        const int devX = (width() / 2) - p.x();
        const int devY = p.y() - (height() / 2);

        /* apply the changes to pitch and yaw*/
        yaw += (float)devX / PIESens;
        pitch += (float)devY / PIESens;

        pitch = std::clamp(pitch, -60.f, 70.f);

        auto* camera = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);
        camera->setPitch(pitch);
        camera->setYaw(yaw);

        QPoint center = mapToGlobal(QPoint(this->width() / 2, this->height() / 2));
        QCursor::setPos(center);

        pieCamera->tick(frameMs);
        updateLODs();

        return;
    }

    // Viewport movement
    if (QGuiApplication::mouseButtons().testFlag(Qt::MouseButton::RightButton))
    {
        float viewportMoveSpeed = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex)->getCameraSpeed();

        if (isKeyDown(VK_KEY_A) || isKeyDown(VK_LEFT))
            cameraMovement.setX(cameraMovement.x() + (viewportMoveSpeed * frameMs * 0.001f));

        if (isKeyDown(VK_KEY_D) || isKeyDown(VK_RIGHT))
            cameraMovement.setX(cameraMovement.x() - (viewportMoveSpeed * frameMs * 0.001f));

        if (isKeyDown(VK_KEY_S) || isKeyDown(VK_DOWN))
            cameraMovement.setZ(cameraMovement.z() - (viewportMoveSpeed * frameMs * 0.001f));

        if (isKeyDown(VK_KEY_W) || isKeyDown(VK_UP))
            cameraMovement.setZ(cameraMovement.z() + (viewportMoveSpeed * frameMs * 0.001f));

        if (isKeyDown(VK_KEY_Q) || isKeyDown(VK_NEXT))
            cameraMovement.setY(cameraMovement.y() - (viewportMoveSpeed * frameMs * 0.001f));

        if (isKeyDown(VK_KEY_E) || isKeyDown(VK_PRIOR))
            cameraMovement.setY(cameraMovement.y() + (viewportMoveSpeed * frameMs * 0.001f));
    }

    if (!cameraMovement.isNull())
    {
        auto* camera = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);
        QQuaternion currentRotation = QQuaternion::fromEulerAngles(camera->getPitch(), camera->getYaw(), 0.0f);

        QVector3D delta = currentRotation.rotatedVector(cameraMovement);
        camera->setPosition(camera->getPosition() + delta);

        if (!delta.isNull())
            updateLODs();

        cameraMovement = QVector3D();
    }

    if (isAutoMoving)
    {
        static const float autoMoveSpeed = 2.0f;
        autoMoveProgress += frameMs * 0.001 * autoMoveSpeed;

        if (autoMoveProgress <= 1.0f)
        {
            auto* camera = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);

            camera->setPosition(std::lerp(currentMovePath.first, currentMovePath.second, autoMoveProgress));

            QQuaternion currentRotation = QQuaternion::slerp(currentRotationPath.first, currentRotationPath.second, autoMoveProgress);
            QVector3D newAngles = currentRotation.toEulerAngles();
            camera->setYaw(newAngles.y());
        }
        else
        {
            isAutoMoving = false;
        }
    }
}

void QOmnigenViewport::scarceTick()
{
    if (Omnigen::get()->isGenerating() || !isActiveViewport || PIECamera)
        return;

    QPoint cursorPos = mapFromGlobal(QCursor::pos());
    Design::getSelectionMgr()->hoverUpdate(cursorPos.x(), cursorPos.y());

    //OmniLog(ELoggingLevel::Trace) << "Objects drawn: " <<= objectsDrawn;
}

void QOmnigenViewport::updateLODs()
{
    /*auto&& camera = getActiveCamera();
    auto&& domainSquares = Generation::Data::get()->getDomainSquares();
    auto&& distanceMap = Omnigen::get()->getDistanceMap();

    auto drawablePos = ...;
    float dist = distance(camera->getPosition(), drawablePos);

    for (ELOD e = ELOD::Zero; e <= ELOD::Last; e = ELOD(int(e) + 1))
    {
        if (dist < distanceMap.at(e))
        {
            drawable->setActiveLOD(e);
            break;
        }
    }*/
}

void QOmnigenViewport::tryMoveToSelection()
{
    auto&& selection = Design::getSelectionMgr()->getAllSelection();
    if (selection.isEmpty())
        return;

    focusOnLocation(selection.begin().value().front()->getPosition());
}

void QOmnigenViewport::focusOnLocation(const QVector3D& location)
{
    const float lookFromDistance = 5 * GRID_SEGMENT_WIDTH;

    auto* camera = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);
    QVector3D movementVector = location - camera->getPosition();
    QVector3D endPos = location - movementVector.normalized() * lookFromDistance;

    currentMovePath = { camera->getPosition(), endPos };
    currentRotationPath = { camera->getRotation(), QQuaternion::fromDirection(movementVector, STARTING_CAMERA_UP) };
    autoMoveProgress = 0.0f;
    isAutoMoving = true;
}

void QOmnigenViewport::startPIE(const QVector3D& startingPoint)
{
    if (!isActiveViewport || Omnigen::get()->isGenerating())
        return;

    Omnigen::get()->toggleShortcut(false);

    resetCameraName = OmnigenCameraMgr::get()->getActiveCameraName(viewportIndex);
    PIECamera = QSharedPointer<TestPlayer>::create(startingPoint);

    OmnigenCameraMgr::get()->addCamera("Player", PIECamera);
    OmnigenCameraMgr::get()->getCamera("Player")->setCameraItemPosition(viewportIndex);
    OmnigenCameraMgr::get()->getCamera("Player")->setPosition(startingPoint);

    OmnigenCameraMgr::get()->changeActiveCameraForViewport(this->getViewportIndex(), "Player");

    if (!QApplication::overrideCursor())
        QApplication::setOverrideCursor(Qt::BlankCursor);

    QOmnigenViewportSection::changeActiveViewport(this);
    QPoint center = mapToGlobal(QPoint(this->width() / 2, this->height() / 2));
    QCursor::setPos(center);

    this->setMouseTracking(true);
    grabKeyboard();
    grabMouse();
}

void QOmnigenViewport::endPIE()
{
    if (!isActiveViewport || Omnigen::get()->isGenerating())
        return;

    OmnigenCameraMgr::get()->changeActiveCameraForViewport(this->getViewportIndex(), resetCameraName);
    OmnigenCameraMgr::get()->removeCamera("Player");
    PIECamera.reset();

    if (QApplication::overrideCursor())
    {
        QApplication::restoreOverrideCursor();
        QOmnigenViewportSection::repaintAll();
    }

    this->setMouseTracking(false);
    releaseKeyboard();
    releaseMouse();
    QOmnigenViewportSection::changeActiveViewport(this);

    Omnigen::get()->toggleShortcut(true);
}


void QOmnigenViewport::singleDrawBegin()
{
    if(!isActiveViewport)
    {
        setUpdatesEnabled(true);
        shouldRepaint = true;
    }
}


auto QOmnigenViewport::getSelectionData() const -> SelectionData
{
    std::scoped_lock lock(selectionDataGuard);
    return { selectionMap, selectionBuffer };
}

void QOmnigenViewport::singleDrawEnd()
{
    if (!isActiveViewport)
    {
        setUpdatesEnabled(false);
        shouldRepaint = false;
    }
}

std::optional<QSharedPointer<OmnigenDrawable>> QOmnigenViewport::getDrawableRepresentative(const std::vector<QSharedPointer<OmnigenDrawable>>& vec)
{
    for (auto&& d : vec)
        if (d)
            return d;

    return {};
}

void QOmnigenViewport::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (PIECamera)
        return;

    if (!isActiveViewport)
        QOmnigenViewportSection::changeActiveViewport(this);

    if (event->buttons().testFlag(Qt::LeftButton))
    {
        tryMoveToSelection();
    }
}

void QOmnigenViewport::mousePressEvent(QMouseEvent* event)
{
    if (PIECamera)
        return;

    if(!isActiveViewport)
        QOmnigenViewportSection::changeActiveViewport(this);

    if (event->buttons().testFlag(Qt::LeftButton))
    {
        // Selection
        Design::getSelectionMgr()->selectObjects(event->x(), event->y(), Design::ESelectionStep::Press);
    }

    if (event->buttons().testFlag(Qt::RightButton))
    {
        // Camera rotation
        lastMousePos = event->globalPos();
        grabKeyboard();
    }
}

void QOmnigenViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (PIECamera)
        return;

    if (event->buttons().testFlag(Qt::LeftButton))
    {
        // Selection
        if (Design::getSelectionMgr()->isSelecting())
            Design::getSelectionMgr()->selectObjects(event->x(), event->y(), Design::ESelectionStep::Move);
    }

    if (event->buttons().testFlag(Qt::RightButton))
    {
        if (!QApplication::overrideCursor())
            QApplication::setOverrideCursor(Qt::BlankCursor);

        int leftRight = event->globalX() - lastMousePos.x();
        int upDown = event->globalY() - lastMousePos.y();

        // Rotate the eye.
        auto* camera = OmnigenCameraMgr::get()->getActiveCamera(viewportIndex);
        camera->setPitch(camera->getPitch() + double(upDown) * 0.3);
        camera->setYaw(camera->getYaw() - double(leftRight) * 0.3);
    }

    lastMousePos = event->globalPos();
}

void QOmnigenViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (PIECamera)
        return;

    if (event->button() == Qt::LeftButton)
    {
        // Selection
        if (Design::getSelectionMgr()->isSelecting())
            Design::getSelectionMgr()->selectObjects(event->x(), event->y(), Design::ESelectionStep::Release);
    }

    if (event->button() == Qt::RightButton)
    {
        // End camera rotation
        if (QApplication::overrideCursor())
        {
            QApplication::restoreOverrideCursor();
            QOmnigenViewportSection::repaintAll();
        }
        releaseKeyboard();
    }

    if (event->button() == Qt::MiddleButton)
    {
        const QDateTime currentDT = QDateTime::currentDateTime();
        QDir dir("Screenshots");
        if (!dir.exists())
            dir.mkpath(".");
        const QString filename = QString("Screenshots/Omnigen_").append(currentDT.toString("dd.MM.yyyy hh_mm_ss")).append(".png");
        if (grab().save(filename))
            OmniLog() <<= "Screenshot saved";
    }
}

void QOmnigenViewport::keyPressEvent(QKeyEvent* event)
{
    if (PIECamera)
    {
        if (event->key() == Qt::Key_Escape)
            endPIE();

        if (event->key() == Qt::Key_Shift)
        {
            const auto pieCamera = dynamic_cast<TestPlayer*>(OmnigenCameraMgr::get()->getActiveCamera(viewportIndex));
            pieCamera->setCameraSpeed(pieCamera->walkSpeed * pieCamera->runModifier);
        }

        if (event->key() == Qt::Key_Space)
        {
            const auto pieCamera = dynamic_cast<TestPlayer*>(OmnigenCameraMgr::get()->getActiveCamera(viewportIndex));
            pieCamera->startJumping();
        }
    }
}

void QOmnigenViewport::keyReleaseEvent(QKeyEvent* event)
{
    if (PIECamera)
    {
        if (event->key() == Qt::Key_Shift)
        {
            const auto pieCamera = dynamic_cast<TestPlayer*>(OmnigenCameraMgr::get()->getActiveCamera(viewportIndex));
            pieCamera->setCameraSpeed(pieCamera->walkSpeed);
        }
    }
}

void QOmnigenViewport::wheelEvent(QWheelEvent* event)
{
    if (PIECamera)
        return;

    if (!isActiveViewport)
        QOmnigenViewportSection::changeActiveViewport(this);

    cameraMovement.setZ(cameraMovement.z() + event->angleDelta().y() * OmnigenCameraMgr::get()->getActiveCamera(viewportIndex)->getCameraSpeed() * 0.0005f);
}

void QOmnigenViewport::resizeEvent(QResizeEvent* event)
{
    QOpenGLWidget::resizeEvent(event);
    QOmnigenViewportSection::repaintAll(true);
}

void QOmnigenViewport::resetView()
{
    OmnigenCameraMgr::get()->getActiveCamera(viewportIndex)->reset();
    OmnigenCameraMgr::get()->changeActiveCameraForViewport(viewportIndex, OmnigenCameraMgr::get()->getActiveCameraName(viewportIndex));
}