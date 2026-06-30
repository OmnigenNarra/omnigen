#pragma once
#include "Scene/OmnigenDrawable.h"

enum class ERenderType
{
    Wireframe,
    Filled
};

class DMarker : public OmnigenDrawable
{
public:
    // Drawable
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Marker; };
    virtual void draw() override;

    FRIEND_OMNIBIN(DMarker);
};

inline void omniSave(const DMarker& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << reinterpret_cast<const QMap<ELOD, QSharedPointer<RenderGeometryData<>>>&>(object.geometry);
    omniBin << object.guid;
}

inline void omniLoad(DMarker& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> reinterpret_cast<QMap<ELOD, QSharedPointer<RenderGeometryData<>>>&>(object.geometry);
    omniBin >> object.guid;
}