#pragma once
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Scene/OmnigenDrawable.h"

// A camera with applied gravity and collisions.
class TestPlayer : public OmnigenCamera
{
public:
    explicit TestPlayer(const QVector3D& initialPos);
    virtual ~TestPlayer();

    void tick(const float delta);

    bool requiresLODUpdate() const { return isDirty; }
    GPoint getCameraGridPosition() const;
    void startJumping();

    static constexpr float getPlayerHeight() { return 150.f; }

    //TODO: Separate jump momentum from gravity
    static constexpr float getJumpMomentum() { return 1.2f; }
    static constexpr float getJumpHeight() { return 25.f; }

    //TODO: Make these configurable through properties
    float walkSpeed = 300.f;
    float runModifier = 1.5f;
    float speedrunModifier = 3.f;
    float jumpOffset = 0.f;
private:
    void updateBounds();
    void updateJumpState();

    //BoundingBox bounds;

    bool isDirty = false;
    bool isJumping = false;

    float maxWalkSlope = 20.f;

    QVector3D movementOffset;
};
