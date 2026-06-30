#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Utils/Polygon.h"

struct BuildingData
{
    float length = 0.f;
    float width = 0.f;
    float height = 0.f;

    BuildingData() = default;
    BuildingData(const float l, const float w, const float h)
        : length(l), width(w), height(h) {}
};

class Building
{
public:
    Building(const int id, const QVector3D& inOriginPt, const BuildingData& inSizes);
    Building(const int id, const QVector3D& inOriginPt, const Polygon2D& bbBounds, const float height);

    void drawWireframe(const QVector4D& inColor = Colors::azure);
    void computeForwardVector();
    void clearWireframe();

    [[nodiscard]] std::vector<Segment2D> getBoundSegments() const;
    [[nodiscard]] Polygon2D getBounds() const { return bounds; }
    [[nodiscard]] const auto& getSizes() const { return sizes; }
    [[nodiscard]] QVector3D getForwardVector() const { return forwardVector; }

private:
    BuildingData sizes;
    Polygon2D bounds;
    int districtId;

    QVector3D originPt;

    std::vector<std::vector<QVector3D>> markerLines;
    std::vector<qint64> markerIds;

    QVector3D forwardVector;
    Segment2D forwardSegment;
};

//TODO: Convert to a proper building pool based on imported models.
class BuildingSizes
{
    inline static std::uniform_int_distribution<int> dist = std::uniform_int_distribution<int>(0, 5);
    inline static std::random_device rd{};
    inline static std::mt19937 eng = std::mt19937(rd());

    inline static std::array<BuildingData, 6> sizes = {
        BuildingData{ 320.f, 150.f, 200.f }, { 620.f, 150.f, 150.f },
        { 320.f, 120.f, 600.f }, { 1000.f, 550.f, 180.f },
        { 800.f, 450.f, 350.f }, { 300.f, 150.f, 280.f }
    };
public:
    //Buildings pool
    inline static const BuildingData& _0 = sizes[0];
    inline static const BuildingData& _1 = sizes[1];
    inline static const BuildingData& _2 = sizes[2];
    inline static const BuildingData& _3 = sizes[3];
    inline static const BuildingData& _4 = sizes[4];
    inline static const BuildingData& _5 = sizes[5];

    static BuildingData getRandomSize();
    static Polygon2D getBoundsFromData(const GVector2D& originPt, const BuildingData& data);

    static BuildingData getSmallestAreaSize();
};

