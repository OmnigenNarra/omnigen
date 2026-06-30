#pragma once
#include "../Common/Mesh.h"

struct BuildingPlacementData
{
    BoundingBox bb;
    std::vector<GVector2D> hull;

    QVector3D forwardVector;

    // 2D center <-- 2D "Door" (local space)
    Segment2D forwardSegment;

    float height;

    BuildingPlacementData convertToWorldSpace(const QVector3D& centerPt, const float scale) const;
    float getMaxGroundExtent() const;
    float getArea() const;
};

template<>
struct OmnigenAsset<EAsset::Structure> : MeshAssetBase
{
    OmnigenAsset();

    static std::vector<QSharedPointer<OmnigenAssetBase>> newAsset();

    virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    virtual void makeUniqueName() override;

    const auto& getPlacementData() const { return placementData; }
    const auto& getTextureIds() const { return textureAssetIds; }
    
    void addTexture(qint64 texId);

private:
    // For autogen
    void computePlacementData();

    //TODO: Categorize better?
    std::unordered_set<qint64> textureAssetIds;
    BuildingPlacementData placementData;
};

using OmnigenAsset_Structure = OmnigenAsset<EAsset::Structure>;

void omniSave(const OmnigenAsset_Structure& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(OmnigenAsset_Structure& object, OmniBin<std::ios::in>& omniBin);