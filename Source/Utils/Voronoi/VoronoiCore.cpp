#include "stdafx.h"
#include "VoronoiCore.h"

#include "Scene/OmnigenDrawable.h"
#include "Utils/Polygon.h"

#include "Utils/QuadTreeLite.h"

#define JC_VORONOI_IMPLEMENTATION
#include "jc_voronoi.h"

#define JC_VORONOI_CLIP_IMPLEMENTATION
#include <set>

#include "jc_voronoi_clip.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"

namespace Voronoi
{
    VoronoiCore::VoronoiCore(const std::vector<GVector2D>& inSeeds, const BoundingBox& boxBounds)
    {
        OmniProfile("Box Voronoi Core");

        diagram = new jcv_diagram;
        std::memset(static_cast<jcv_diagram*>(diagram), 0, sizeof(jcv_diagram));

        centers = inSeeds;

        std::vector<jcv_point> internalSeeds;
        for (const auto& point : inSeeds)
        {
            jcv_point seed{};
            seed.x = point.x;
            seed.y = point.z;

            internalSeeds.push_back(seed);
        }

        jcv_point p1{};
        p1.x = boxBounds.nbl.x();
        p1.y = boxBounds.nbl.z();
        jcv_point p2{};
        p2.x = boxBounds.nbl.x() + boxBounds.sizes.x();
        p2.y = boxBounds.nbl.z() + boxBounds.sizes.z();

        jcv_rect rect;
        rect.min = p1;
        rect.max = p2;

        jcv_diagram_generate(inSeeds.size(), internalSeeds.data(), &rect, nullptr, static_cast<jcv_diagram*>(diagram));
    }

    VoronoiCore::VoronoiCore(const std::vector<GVector2D>& inSeeds, const Polygon2D& polygonBounds)
    {
        diagram = new jcv_diagram;
        std::memset(static_cast<jcv_diagram*>(diagram), 0, sizeof(jcv_diagram));

        centers = inSeeds;

        std::vector<jcv_point> internalSeeds;
        for (const auto& point : inSeeds)
        {
            jcv_point seed{};
            seed.x = point.x;
            seed.y = point.z;

            internalSeeds.push_back(seed);
        }

        jcv_clipping_polygon polygon;
        polygon.num_points = polygonBounds.getPts().size();
        std::vector<jcv_point> internalPolyPoints;
        for (const auto& point : polygonBounds.getPts())
        {
            jcv_point seed{};
            seed.x = point.x;
            seed.y = point.z;

            internalPolyPoints.push_back(seed);
        }
        polygon.points = internalPolyPoints.data();

        jcv_clipper polygonClipper;
        polygonClipper.test_fn = jcv_clip_polygon_test_point;
        polygonClipper.clip_fn = jcv_clip_polygon_clip_edge;
        polygonClipper.fill_fn = jcv_clip_polygon_fill_gaps;
        polygonClipper.ctx = &polygon;

        auto boxBounds = polygonBounds.getEnclosingBB();
        jcv_point p1{};
        p1.x = boxBounds.nbl.x();
        p1.y = boxBounds.nbl.z();
        jcv_point p2{};
        p2.x = boxBounds.nbl.x() + boxBounds.sizes.x();
        p2.y = boxBounds.nbl.z() + boxBounds.sizes.z();

        jcv_rect rect;
        rect.min = p1;
        rect.max = p2;

        jcv_diagram_generate(internalSeeds.size(), internalSeeds.data(), &rect, &polygonClipper, static_cast<jcv_diagram*>(diagram));
    }

    VoronoiCore::~VoronoiCore()
    {
        jcv_diagram_free(static_cast<jcv_diagram*>(diagram));
    }

    std::vector<VoronoiData> VoronoiCore::constructData() const
    {
        OmniProfile("Box Voronoi Data Reconstruction");

        std::vector<VoronoiData> results;
        tml::qtree<float, int> tree(0, 0, GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH, GRID_SEGMENT_COUNT * GRID_SEGMENT_WIDTH);

        auto checkPt = [](const GVector2D& p) -> bool
        {
            if ((p.x < 0 || p.x > getMaxGridCoord()) || (p.z < 0 || p.z > getMaxGridCoord()))
                return false;

            return true;
        };

        const auto addToCell = [&checkPt](std::vector<GVector2D>* cell, const GVector2D& v) -> GVector2D 
        {
            for (auto&& entry : *cell)
            {
                if (entry.x == v.x && entry.z == v.z)
                    return entry;

                if (qAbs(entry.x - v.x) < 2 && qAbs(entry.z - v.z) < 2)
                {
                    entry = GVector2D(std::min(entry.x, v.x), std::min(entry.z, v.z));
                    return entry;
                }
            }

            cell->push_back(v);
            return v;
        };

        const jcv_site* sites = jcv_diagram_get_sites(static_cast<jcv_diagram*>(diagram));

        std::vector<const jcv_site*> sortedSites;
        sortedSites.resize(centers.size());

        // Align sites with centers
        for (int i = 0; i < static_cast<jcv_diagram*>(diagram)->numsites; i++)
            sortedSites[sites[i].index] = &sites[i];

        // Construct cell data
        for (const auto* site : sortedSites)
        {
            std::vector<GVector2D> polygonData;
            QMap<int, std::array<GVector2D, 2>> neighborsMap;

            const jcv_graphedge* e = site->edges;
            if (!e)
            {
                continue;
            }

            while (e)
            {
                if (jcv_point_eq(&e->pos[0], &e->pos[1]))
                {
                    continue;
                }

                // Edge points
                auto pt1 = GVector2D((int)floor(e->pos[1].x), (int)floor(e->pos[1].y));
                auto pt2 = GVector2D((int)floor(e->pos[0].x), (int)floor(e->pos[0].y));
               
                // Duplication prevention
                if (!contains(polygonData, pt1))
                {
                    std::vector<const decltype(tree)::node_type*> results;

                    bool treeMatch = tree.search(pt1.x, pt1.z, 2.f, results);
                    if (!treeMatch)
                    {
                        tree.add_node(pt1.x, pt1.z, 0);
                    }
                    else
                    {
                        pt1 = GVector2D(results[0]->x, results[0]->y);
                    }
                }

                if (!contains(polygonData, pt2))
                {
                    std::vector<const decltype(tree)::node_type*> results2;

                    bool treeMatch2 = tree.search(pt2.x, pt2.z, 2.f, results2);
                    if (!treeMatch2)
                    {
                        tree.add_node(pt2.x, pt2.z, 0);
                    }
                    else
                    {
                        pt2 = GVector2D(results2[0]->x, results2[0]->y);
                    }
                }

                // Sanity checks
                if (checkPt(pt1) && checkPt(pt2))
                {
                    const auto& v1 = addToCell(&polygonData, pt1);
                    const auto& v2 = addToCell(&polygonData, pt2);

                    if (e->neighbor && polygonData.size() >= 2)
                        neighborsMap[e->neighbor->index] = { v1, v2 };
                }

                e = e->next;
            }

            //TODO: See jc_voronoi.h:430
            //Print domain bounds when this asserts
            Q_ASSERT(polygonData.size() > 2);

            auto newPoly = Polygon2D(polygonData);
            const auto center = newPoly.getCenter();

            auto sortPolygon = [center](const GVector2D& a, const GVector2D& b) -> bool {
                const double angleA = atan2(a.x - center.x, a.z - center.z);
                const double angleB = atan2(b.x - center.x, b.z - center.z);

                return angleA < angleB;
            };

            auto newPoints = newPoly.getPts();
            std::ranges::sort(newPoints, sortPolygon);

            // Finalize cell
            results.push_back({ 
                .center = { site->p.x, site->p.y },
                .poly = Polygon2D(newPoints),
                .isBounded = bool(site->isBounded), 
                .neighbors = neighborsMap 
            });
        }

        return results;
    }
}
