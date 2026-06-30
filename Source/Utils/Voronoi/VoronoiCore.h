#pragma once
#include "Utils/Polygon.h"

class Polygon2D;
struct BoundingBox;

namespace Voronoi
{
    struct VoronoiData
    {
        GVector2D center;
        Polygon2D poly;
        bool isBounded;
        QMap<int, std::array<GVector2D, 2>> neighbors;
    };

    class VoronoiCore
    {
    public:
        VoronoiCore() = default;
        VoronoiCore(const std::vector<GVector2D>& inSeeds, const BoundingBox& boxBounds);
        VoronoiCore(const std::vector<GVector2D>& inSeeds, const Polygon2D& polygonBounds);

        ~VoronoiCore();

        [[nodiscard]] std::vector<VoronoiData> constructData() const;

    private:
        std::vector<GVector2D> centers;

        void* diagram = nullptr;
    };
}
