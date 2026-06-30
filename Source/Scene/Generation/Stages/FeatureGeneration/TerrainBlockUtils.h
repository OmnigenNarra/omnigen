#pragma once
#include <QVector3D>
#include "Utils/Triangulation/Triangulation.h"
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"
#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockData.h"
#include "ClusterMeshMarker.h"

const inline float gMaxTriangleSideLength = 100.0f;
const inline float gMidTriangleSideLength = 75.0f;
const inline float gMinTriangleSideLength = 50.0f;

namespace Generation
{
    struct ClusterConstructionParams;
}

int getMeshSegments(float edgeLength);
int getMeshSegmentsAdv(const GVector2D& p1, const GVector2D& p2);
int getMeshSegments3D(const GVector2D& p1, const GVector2D& p2);

struct MeshingParams
{
    // Functor used for division inner edges of polygon - param is distance, returns parts count
    SplitFunction innerSplitFunc;

    // Functor for splitting outer edges - param is edge, defined by 2 points and result is vector of new points (excluding start and end)
    // Important! Split outer must match restrictions for whole mesh borders to correctly connect cluster's borders
    SplitFunction outerSplitFunc;

    // Should outer boundary be divided
    bool splitOuter = true;
};
const MeshingParams& getDefaultMeshingParams();

std::tuple<GeometryData<GVector2D>, IndexType>
meshPolygon2(const std::vector<GVector2D>& polygon, const MeshingParams& meshingParams = getDefaultMeshingParams(), std::optional<std::vector<GVector2D>> forcedDiagonal = {});

// Returns polygon with points on each edge of initial polygon.
Polygon2D getSegmentedPolygon(const Polygon2D& polygon);

// returns vector of triangles
std::vector<Polygon2D> triangulatePolygonWithHole(const Polygon2D& area, const Polygon2D& hole);

std::array<std::vector<Polygon2D>, 2> splitPolygonByMultiLine(const Polygon2D& area, const std::vector<GVector2D>& line);
std::tuple<Polygon2D, Polygon2D> splitPolygonByFittedMultiLine(const Polygon2D& area, const std::vector<GVector2D>& line);
std::tuple<Polygon2D, Polygon2D> splitPolygonByPointFittedMultiLine(const Polygon2D& area, const std::vector<GVector2D>& line);

std::tuple<const GVector2D&, const GVector2D&> getEdgeBetweenNeighbors(int n1, int n2);

template<typename T = GVector2D, template<typename T> typename C = std::vector, typename SEG = Segment2D>
C<T> splitSegment(const SEG& S, FFirstLastPolicy inclusionPolicy, bool mustPreserveOrder, std::optional<int> forceSegmentCount = {})
{
    auto [a, b] = S;

    constexpr bool isResultOrdered = std::is_same_v<C<int>, std::vector<int>>;

    bool swapped = (a > b);
    if constexpr (isResultOrdered)
        if (swapped)
            std::swap(a, b);

    int segments = forceSegmentCount ? *forceSegmentCount : getMeshSegmentsAdv(a, b);
    C<T> pts;

    for (int s = 1; s < segments; ++s)
        pts << std::lerp(a, b, float(s) / float(segments));

    if constexpr (isResultOrdered)
        if (swapped && mustPreserveOrder)
            std::reverse(pts.begin(), pts.end());

    if constexpr (std::is_same_v<C<int>, std::vector<int>>)
    {
        if (!!(inclusionPolicy & FFirstLastPolicy::First))
            pts.insert(pts.begin(), S.first);

        if (!!(inclusionPolicy & FFirstLastPolicy::Last))
            pts.push_back(S.second);
    }
    else
    {
        if (!!(inclusionPolicy & FFirstLastPolicy::First))
            pts << S.first;

        if (!!(inclusionPolicy & FFirstLastPolicy::Last))
            pts << S.second;
    }

    return pts;
}

enum class ENoiseUsage
{
    TerrainHeight,
    BeachWidth,
    Temperature,
    Humidity
};

double getGlobalNoiseValue(float x, float z, ENoiseUsage nu);


// Struct for edge, defined by start and end indices, and maybe some points on it - defined by start and count
// so, intermediate points must be next to each other in ascending order
struct EdgeIndexData
{
    int start = 0;
    int end = 0;

    int intermediatePointsCount = 0;
    int intermediatePointsStart = 0;

    int getIntermediateEnd() const
    {
        return intermediatePointsCount == 0 ? end : intermediatePointsStart + intermediatePointsCount - 1;
    }
};

template<typename T>
concept VectorConcept = std::is_convertible_v<T, QVector3D>;

class MeshConnector
{
public:

    std::vector<QVector3D> vertices;
    std::vector<IndexType> indices;

    MeshConnector() = default;

    MeshConnector(const std::vector<QVector3D>& vert, const std::vector<IndexType>& ind)
        :vertices(vert)
        ,indices(ind)
    {
        registerPoints();
    }

    MeshConnector(std::vector<QVector3D>&& vert, std::vector<IndexType>&& ind)
        :vertices(std::move(vert))
        ,indices(std::move(ind))
    {
        registerPoints();
    }

    void addTriangles(const std::vector<IndexType>& ind, const std::vector<IndexType>& indexRemap)
    {
        for (int i = 0; i < ind.size(); i+=3)
        {
            const IndexType i1 = indexRemap[ind[i]];
            const IndexType i2 = indexRemap[ind[i + 1]];
            const IndexType i3 = indexRemap[ind[i + 2]];
            if (i1 == i2 || i1 == i3 || i2 == i3)
                continue;
            indices << i1 << i2 << i3;
        }
    }

    // Merging meshes, and replace indices, if found in new mesh vertices, that are already present in current mesh
    // create_point_func - for adding custom height at point
    template<VectorConcept T = QVector3D>
    void addMesh(const std::vector<T>& vert, const std::vector<IndexType>& ind, const std::function<QVector3D(const QVector3D&)> create_point_func = [](const QVector3D& vec){ return vec; })
    {
        vertices.reserve(vertices.size() + vert.size());
        indices.reserve(indices.size() + ind.size());

        std::vector<IndexType> indexRemap(vert.size());

        for (int i = 0; i < vert.size(); ++i)
        {
            const QVector3D vertex = create_point_func((QVector3D)vert[i]);
            const auto iter = vertexMap.find(vertex);
            if (iter == vertexMap.end())
            {
                indexRemap[i] = vertices.size();
                vertexMap[vertex] = vertices.size();
                vertices << vertex;
            }
            else
                indexRemap[i] = iter->second;
        }

        addTriangles(ind, indexRemap);
    }

    void addMesh3D(const std::vector<QVector3D>& vert, const std::vector<IndexType>& ind)
    {
        vertices.reserve(vertices.size() + vert.size());
        indices.reserve(indices.size() + ind.size());

        std::vector<IndexType> indexRemap(vert.size());

        for (int i = 0; i < vert.size(); ++i)
        {
            const QVector3D& vertex = vert[i];
            const auto iter = vertexMap.find(vertex);
            if (iter == vertexMap.end())
            {
                indexRemap[i] = vertices.size();
                vertexMap[vertex] = vertices.size();
                vertices << vertex;
            }
            else
                indexRemap[i] = iter->second;
        }

        addTriangles(ind, indexRemap);
    }

    // Simply merge meshes, without checking if new points are existing in current mesh
    template<VectorConcept T = QVector3D>
    void addMeshWithoutCheckingDublicates(const std::vector<T>& vert, const std::vector<IndexType>& ind, const std::function<QVector3D(const QVector3D&)> create_point_func = [](const QVector3D& vec){ return vec; })
    {
        vertices.reserve(vertices.size() + vert.size());
        indices.reserve(indices.size() + ind.size());

        const IndexType indexOffset = vertices.size();

        for (int i = 0; i < vert.size(); ++i)
        {
            const QVector3D vertex = create_point_func((QVector3D)vert[i]);
            vertices << (QVector3D)vertex;
        }

        for (const IndexType index: ind)
            indices << index + indexOffset;
    }

    void shrink()
    {
        vertices.shrink_to_fit();
        indices.shrink_to_fit();
    }

private:

    struct VectorHash2D
    {
        std::size_t operator()(QVector3D const& v) const noexcept
        {
            return qHash(GVector2D(v));
        }
    };

    struct VectorComparator2D
    {
        bool operator()(const QVector3D& lhs, const QVector3D& rhs) const
        {
            return (lhs.x() == rhs.x() && lhs.z() == rhs.z());
        }
    };

    void registerPoints()
    {
        vertexMap.reserve(vertices.size());
        for (int i = 0; i < vertices.size(); ++i)
            vertexMap[vertices[i]] = i;
    }

private:

    std::unordered_map<QVector3D, IndexType, VectorHash2D, VectorComparator2D> vertexMap;

};
