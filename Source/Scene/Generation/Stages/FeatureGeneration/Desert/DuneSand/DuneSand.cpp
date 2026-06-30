#include "stdafx.h"
#include "DuneSand.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"

namespace Generation
{
    DesertClusterSubData<EDesertBlockSubtype::DuneSand>::DesertClusterSubData(ClusterData<ETerrainBlock::Desert>* inBaseData)
        : DesertClusterSubDataBase(inBaseData)
    {
    }

    std::unordered_set<int> DesertClusterSubData<EDesertBlockSubtype::DuneSand>::customGrow(const std::unordered_set<int>& candidates)
    {
        baseData->cells += candidates;
        return candidates;
    }

    QSharedPointer<DesertSubClusterBase> DesertClusterSubData<EDesertBlockSubtype::DuneSand>::createSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* cluster)
    {
        return QSharedPointer<DesertSubCluster<EDesertBlockSubtype::DuneSand>>::create(cluster);
    }

    void DesertSubCluster<EDesertBlockSubtype::DuneSand>::generate()
    {
        auto&& dem = Data::get()->getDEM();
        auto&& diagram = Data::get()->getTerrainCells();

        const Polygon2D clusterPolygon = Utils::makeBoundingPolygon(cluster->cells).front();

        GeometryData<TerrainMeshVertex> geometry;
        auto [geom2D, unused] = meshPolygon2(clusterPolygon.getPts());
        auto& verts = geom2D.vertices;
        auto& indices = geom2D.indices;

        geometry.vertices.reserve(verts.size());

        for (auto&& vert : verts)
        {
            TerrainMeshVertex finalPoint = { get3dSandPoint(vert), {}, *cluster };
            geometry.vertices <<= std::move(finalPoint);
        }

        geometry.indices = std::move(indices);
        cluster->section = spawnBatched(std::move(geometry), cluster->makeBatchParams());
    }

    DesertSubCluster<EDesertBlockSubtype::DuneSand>::DesertSubCluster(TerrainBlockCluster<ETerrainBlock::Desert>* inCluster)
        : DesertSubClusterBase(inCluster, EDesertBlockSubtype::DuneSand)
    {
    }
}

void omniSave(const Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneSand>& object, OmniBin<std::ios::out>& omniBin)
{

}

void omniLoad(Generation::DesertSubCluster<Generation::EDesertBlockSubtype::DuneSand>& object, OmniBin<std::ios::in>& omniBin)
{

}