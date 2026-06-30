#pragma once
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"

class DShorelineMarker;
class DLandmassMarker;
class DSeamassMarker;

class DLandmassMarker : public DPolygonWithHolesMarker, public QEnableSharedFromThis<DLandmassMarker>
{
public:
    DLandmassMarker(const std::vector<QSharedPointer<DShorelineMarker>>& inShorelines, const std::vector<QSharedPointer<DShorelineMarker>>& inInnerSeaShorelines);

    const auto& getName() const { return name; }
    const auto& isCoast() const { return coast; }
    const auto& getSquares() const { return squares; }
    const auto& getShorelines() const { return shorelines; }
    const auto& getInnerSeaShorelines() const { return innerSeaShorelines; }
    const auto& getShorelinesGuids() const { return shorelinesGuids; }
    const auto& getInnerSeaShorelinesGuids() const { return innerSeaShorelinesGuids; }
    const auto& isLocked() const { return lock; }

    void setName(const QString& newName);
    void setCoast(bool isCoast);
    void setSquares(const QSet<GPoint>& newSquares);
    void setShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& newShorelines);
    void setInnerSeaShoreline(const std::vector<QSharedPointer<DShorelineMarker>>& newInnerSeaShorelines);
    void setPolygons(const std::vector<QVector3D>& mainPolygon, const std::vector<std::vector<QVector3D>>& cutPolygons);

    void setLocked(const bool& isLocked);

    template<typename F>
    void forEachShoreline(const F& func) const
    {
        for (auto&& shoreline : shorelines)
            func(shoreline, false);

        for (auto&& shoreline : innerSeaShorelines)
            func(shoreline, true);
    }

    QSet<GPoint> findSeasideDomainSquares();
    QSet<GPoint> findTerrainDomainSquares();

    bool contains(const QSharedPointer<DShorelineMarker>& shoreline) const;
    bool removeShoreline(const QSharedPointer<DShorelineMarker>& shoreline);
    void clearShorelines();

    bool addShoreline(const QSharedPointer<DShorelineMarker>& shoreline);
    bool addInnerSeaShoreline(const QSharedPointer<DShorelineMarker>& shoreline);

    // recalculate polygons using current shorelines
    void recalculateLandmassPolygons(bool updateVerts = true);

    bool isInside(const GVector2D& point, bool ignoreInnerSeas = false, float minDistanceFromShoreline = 2.0f);
    bool isInside(const Segment2D& segment, bool ignoreInnerSeas = false, float minDistanceFromShoreline = 2.0f);
    bool isInside(const std::vector<QVector3D>& path, bool ignoreInnerSeas = false, float minDistanceFromShoreline = 2.0f);

    static bool isInsideLand(const GVector2D& point, bool ignoreInnerSeas = false, float minDistanceFromShoreline = 2.0f);
    static bool isInsideLand(const Segment2D& segment, bool ignoreInnerSeas = false, float minDistanceFromShoreline = 2.0f);

    IMPLEMENT_SHOULD_DRAW();

protected:
    DLandmassMarker() = default;

    virtual void draw() override;

    void updateGeometry();

    QString name;

    bool coast;

    QSet<GPoint> squares;

    std::vector<QSharedPointer<DShorelineMarker>> shorelines;

    std::vector<QSharedPointer<DShorelineMarker>> innerSeaShorelines;

    // Whenever landmass should be kept on auto gen
    bool lock;

    // Guids for save purpose
    mutable std::vector<qint64> shorelinesGuids;

    mutable std::vector<qint64> innerSeaShorelinesGuids;

    static std::vector<QVector3D> createLandmassPolygon(const std::vector<QSharedPointer<DShorelineMarker>>& shorelines, bool* isCoast);
    
    static std::vector<std::vector<QVector3D>> createInnerSeamassPolygons(const std::vector<QSharedPointer<DShorelineMarker>>& innerSeaShorelines);

    static QSet<GPoint> findInsideSquares(std::vector<QVector3D>& landmassPolygon, const std::vector<QSharedPointer<DShorelineMarker>>& shorelines, const std::vector<QSharedPointer<DShorelineMarker>>& innerSeaShorelines);

    void makeName();

    FRIEND_OMNIBIN(DLandmassMarker);
};

void omniSave(const DLandmassMarker& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(DLandmassMarker& object, OmniBin<std::ios::in>& omniBin);