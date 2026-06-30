#pragma once
#include "Scene/HandleDrawable.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "Scene/Generation/Stages/UrbanLayout/UrbanSuggestion.h"
#include <QEnableSharedFromThis>

#include "UrbanLayoutSelection.h"

class DUrbanHandle : public DHandle, public QEnableSharedFromThis<DUrbanHandle>
{
    static inline ShaderPipeline shaderPipeline;

public:
    explicit DUrbanHandle(const QVector3D& vertex);

    // Drawable
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); }
    virtual void draw() override;
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void unbindShader() override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    // Handle
    virtual void update() override;
    virtual const char* getSpritePath() const override
    {
        return "Resources/Icons/sprite_urban_handle.png";
    }
    virtual float getSpriteSize() const override
    {
        return 3000.0;
    }
    virtual const char* getSpriteSizeShader() const override
    {
        //TODO: This is currently unused as I could not effectively replace the macro
        return "3000.0";
    }

    QWeakPointer<Generation::UrbanSuggestion> ownedSuggestion;
protected:
    // Drawable
    virtual void createShader() override;
    virtual void createShaderResources() override;

    static inline QOpenGLTexture* texture = nullptr;
    QVector3D cachedVertex;
    float cachedHeight = 0.f;

    friend Generation::UrbanSuggestion;
    FRIEND_OMNIBIN(DUrbanHandle);
};

inline void omniSave(const DUrbanHandle& object, OmniBin<std::ios::out>& omniBin)
{
}

inline void omniLoad(DUrbanHandle& object, OmniBin<std::ios::in>& omniBin)
{
    object.initialize();
}
