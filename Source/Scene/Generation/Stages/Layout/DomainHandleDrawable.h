#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include <QOpenGLFunctions>
#include <QEnableSharedFromThis>

#include "Scene/HandleDrawable.h"

#define DOMAIN_HANDLE_SIZE_SHADER "5000.0"

class DDomain;

class DDomainHandle : public DHandle, public QEnableSharedFromThis<DDomainHandle>
{
    static inline ShaderPipeline shaderPipeline;

public:
    DDomainHandle() = default;

    // Drawable
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); }
    virtual void draw() override;
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void unbindShader() override;
    virtual bool shouldDraw(int vIdx) const override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    // Handle
    virtual void prePositionChange() override;
    virtual void postPositionChange() override;
    virtual void update() override;
    virtual const char* getSpritePath() const override
    {
        return "Resources/Icons/sprite_domain_handle.png";
    }
    virtual float getSpriteSize() const override
    {
        return 5000.0;
    }
    virtual const char* getSpriteSizeShader() const override
    {
        //TODO: This is currently unused as I could not effectively replace the macro
        return "5000.0";
    }

    const auto isRainbow() const { return rainbowMode; }
    void setRainbowMode(bool inRaimbowMode);

    const auto& getDomain() const { return ownedDomain; }
protected:
    // Drawable
    virtual void createShader() override;
    virtual void createShaderResources() override;

    QWeakPointer<DDomain> ownedDomain;
    static inline QOpenGLTexture* texture = nullptr;
    bool rainbowMode = false;

    friend class DDomain;
    FRIEND_OMNIBIN(DDomainHandle);
};

inline void omniSave(const DDomainHandle& object, OmniBin<std::ios::out>& omniBin)
{
}

inline void omniLoad(DDomainHandle& object, OmniBin<std::ios::in>& omniBin)
{
    object.initialize();
}