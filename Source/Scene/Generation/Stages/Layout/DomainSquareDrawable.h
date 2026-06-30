#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"

class DDomainSquare : public OmnigenDrawable
{
    inline static ShaderPipeline shaderPipeline;

public:
    DDomainSquare(int gx, int gz);

    const auto& getSquare() const { return gridID; };

    // Drawable
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Terrain; };
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual bool shouldDraw(int vIdx) const override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

private:
    DDomainSquare() = default;
    virtual void createShader() override;
    virtual void createDefaultLodLevel() override;

    const GPoint gridID;

    FRIEND_OMNIBIN(DDomainSquare);
};

inline void omniSave(const DDomainSquare& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.gridID;
}

inline void omniLoad(DDomainSquare& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> const_cast<GPoint&>(object.gridID);
    object.initialize();
}