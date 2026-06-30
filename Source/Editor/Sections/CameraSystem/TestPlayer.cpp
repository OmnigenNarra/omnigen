#include "stdafx.h"
#include "TestPlayer.h"

#include "Omnigen.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Scene/Generation/Stages/FeatureGeneration/StageGeneration_FeatureGeneration.h"
#include "Utils/PlatformMisc.h"

TestPlayer::TestPlayer(const QVector3D& initialPos)
{
    this->setCameraSpeed(walkSpeed);
}

TestPlayer::~TestPlayer()
{

}

void TestPlayer::tick(const float delta)
{
    //TODO: prevent this from running whenever conditions are not met

    isDirty = false;
    movementOffset = {};

    if (isKeyDown(VK_KEY_A) || isKeyDown(VK_LEFT))
        movementOffset.setX(movementOffset.x() + (getCameraSpeed() * delta * 0.001f));

    if (isKeyDown(VK_KEY_D) || isKeyDown(VK_RIGHT))
        movementOffset.setX(movementOffset.x() - (getCameraSpeed() * delta * 0.001f));

    if (isKeyDown(VK_KEY_S) || isKeyDown(VK_DOWN))
        movementOffset.setZ(movementOffset.z() - (getCameraSpeed() * delta * 0.001f));

    if (isKeyDown(VK_KEY_W) || isKeyDown(VK_UP))
        movementOffset.setZ(movementOffset.z() + (getCameraSpeed() * delta * 0.001f));

    if (isJumping)
    {
        updateJumpState();
        setPosition(QVector3D(position.x(), position.y() + jumpOffset, position.z()));
    }

    if (!movementOffset.isNull())
    {
        const QQuaternion currentRotation = QQuaternion::fromEulerAngles(0.0f, getYaw(), 0.0f);
        QVector3D newPosition = getPosition() + currentRotation.rotatedVector(movementOffset);

        if (!isJumping)
        {
            auto pred = [&](auto&& a, auto&& b) { return std::abs(getPosition().y() - a.y()) < std::abs(getPosition().y() - b.y()); };
            auto surfacePts = Generation::Utils::castPointTo3D(newPosition, pred);
            newPosition.setY(surfacePts.begin()->y() + getPlayerHeight());
        }

        const GPoint oldGridPos = GVector2D(getPosition()).toGPoint();
        const GPoint newGridPos = GVector2D(newPosition).toGPoint();

        setPosition(newPosition);

        //Check if we require lod update
        if (oldGridPos != newGridPos)
            isDirty = true;

        movementOffset = {};
    }

    updateBounds();

}

GPoint TestPlayer::getCameraGridPosition() const
{
    return GVector2D(this->getPosition()).toGPoint();
}

void TestPlayer::startJumping()
{
    if (isJumping)
        return;

    isJumping = true;
}

void TestPlayer::updateJumpState()
{
    if (isJumping)
    {
        if (jumpOffset < getJumpHeight() && jumpOffset >= 0.f)
        {
            jumpOffset += getJumpMomentum();
        }
        else
        {
            if (jumpOffset > 0.f)
                jumpOffset = jumpOffset * -1;

            jumpOffset += getJumpMomentum();
            if (jumpOffset >= 0.f)
            {
                isJumping = false;
                jumpOffset = 0.f;
            }
        }
    }
}

void TestPlayer::updateBounds()
{

}
