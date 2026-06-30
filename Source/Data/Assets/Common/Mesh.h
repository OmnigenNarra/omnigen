#pragma once
#include "../AssetBase.h"
#include "Scene/OmnigenDrawable.h"
#include "MeshData.h"

using MeshDesc = std::map<EShaderAttribute, VertexAttributeDesc>;

struct MeshComponent
{
    const auto& getGeometry() const { return geometry; };
    const auto& getScaleRange() const { return scaleRange; }

    void setGeometry(ELOD lod, const QSharedPointer<MeshAssetGeometry>& inGeometry);
    void setScaleRange(const std::array<float, 2>& inRange);

private:
    std::map<ELOD, QSharedPointer<MeshAssetGeometry>> geometry;
    std::array<float, 2> scaleRange = { 1.0f - 0.5f / 3.0f, 1.0f + 0.5f / 3.0f };

    FRIEND_OMNIBIN(MeshComponent);

    friend struct MeshAssetBase;
    friend class QAssetCompilerDialog;
};

void omniSave(const MeshComponent& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(MeshComponent& object, OmniBin<std::ios::in>& omniBin);

struct MeshAssetBase : OmnigenAssetBase
{
    const auto& getMesh() const { return mesh; }
    void setMesh(const MeshComponent& inMesh);

protected:
    MeshComponent mesh;

    FRIEND_OMNIBIN(MeshAssetBase);
};

void omniSave(const MeshAssetBase& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(MeshAssetBase& object, OmniBin<std::ios::in>& omniBin);