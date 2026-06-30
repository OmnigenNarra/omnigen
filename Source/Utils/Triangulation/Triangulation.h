#pragma once
#include "Scene/OmnigenDrawable.h"
#include "../Polygon.h"

enum class FFirstLastPolicy
{
    None = 0,
    First = 1 << 0,
    Last = 1 << 1,

    Both = First | Last
};
DECLARE_FLAG_OPERATORS(FFirstLastPolicy);

using SplitFunction = std::function<std::vector<GVector2D>(const GVector2D&, const GVector2D&, FFirstLastPolicy)>;

std::vector<IndexType> triangulate2D(const std::vector<GVector2D>& pts, const Polygon2D& bounds = {});

std::tuple<std::vector<GVector2D>, std::vector<IndexType>> constrainedTriangulation2D(const std::vector<GVector2D>& pts, bool addCenter = false, bool needRefinement = false,
    const std::vector<GVector2D>& additionalPoints = std::vector<GVector2D>(), bool needDuplicateAdditionalPoints = false);

std::vector<GVector2D> filterDuplicatedPoints(const std::vector<GVector2D>& pts);

namespace triangulation
{
    struct Edge
    {
        int start = 0;
        int end = 0;

        bool operator==(const triangulation::Edge& other) const { return (start == other.start && end == other.end) || (start == other.end && end == other.start); }
    };

    struct EdgeDivisionData
    {
        int start = 0;
        int end = 0;
        int count = 0;
        float length = 0.f;
        GVector2D direction;
        std::vector<IndexType> indices;
    };
}

bool operator==(const triangulation::Edge& v1, const triangulation::Edge& v2);

namespace std
{
    template<>
    struct std::hash<triangulation::Edge>
    {
        std::size_t operator()(triangulation::Edge const& edge) const noexcept
        {
            std::size_t h1 = std::hash<int>{}(edge.start);
            std::size_t h2 = std::hash<int>{}(edge.end);
            return std::hash<std::size_t>{}(h1 + h2);
        }
    };
}
