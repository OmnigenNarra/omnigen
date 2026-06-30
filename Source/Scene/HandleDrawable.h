#pragma once
#include "Scene/OmnigenDrawable.h"
#include <QOpenGLFunctions>

class DHandle : public OmnigenDrawable
{
public:
    // Drawable
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::DomainHandle; }

    virtual void cacheBoundingBox() override;

    bool isSelected() const { return bIsSelected; }
    void setSelected(bool b);

    const QVector3D& getPosition() const;

    virtual void prePositionChange() {}
    virtual void postPositionChange() {}
    virtual void adjustPosition(const QVector3D& delta);

    virtual void update() = 0;

    virtual constexpr float getSpriteSize() const = 0;
    virtual constexpr const char* getSpriteSizeShader() const = 0;
    virtual constexpr const char* getSpritePath() const = 0;
protected:
    virtual void createDefaultLodLevel() override;

    bool bIsSelected = false;
    bool hasAdjustedPosition = false;
};