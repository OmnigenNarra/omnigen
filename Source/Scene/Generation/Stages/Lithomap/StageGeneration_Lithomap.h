#pragma once
#include <QSet>
#include "Utils/Voronoi/Voronoi.h"
#include "../StageGenerationBase.h"

namespace Design
{
    template<EGenerationStage GS>
    class StageTools;
}

namespace Generation
{
    template<>
    class StageGen<EGenerationStage::Lithomap>
    {
    public:
        static void initialize();
        static constexpr bool hasAutoGen() { return true; }
        static bool autoGen();
        static void clear();
        static bool validate() { return true; };
        static void finalize();

        static auto getCellLithoType(int cell) { return cellToLithoType[cell]; };
        static void setCellLithoType(int cell, qint64 rockId);
        static void updateCellDataFromLithomap();
        static void updateCellColors();
        static void visualizeLithomap();

        static void reshapeTerrainCellsDiagram();

    private:
        static void generateTerrainCells();
        static void initLithoMap();
        static void generateLithoMap();
        static void finalizeLithoMap();
        static std::unordered_set<int> expandLithoPatch(const std::unordered_set<int>& edgeCellsIndices, const std::unordered_set<int>& allPatchCellsIndices, const std::vector<Voronoi::GVoronoiCell>& allCells);
        static void growCluster(qint64 lithoTypeId, QSet<int>* cluster, int targetSize, const Voronoi::Diagram& diagram, QSet<int>* assignedIndices, std::vector<qint64>* data);
        
        static void updateCellColor(int cell);

        static inline std::vector<qint64> cellToLithoType;
        static inline std::unordered_map<qint64, QVector4D> colorByLithoType;
    };

    namespace Utils
    {
        int findCell(const GVector2D& p);
        std::vector<Polygon2D> makeBoundingPolygon(const std::unordered_set<int>& cells);
        std::vector<std::unordered_set<int>> clusterCells(std::unordered_set<int>&& selectedCells, const std::optional<int>& maxSize = std::nullopt);

        template<typename DataType, typename TypeGeter>
        static QSet<int> createMetaCluster(const std::vector<DataType>& typeMap, const TypeGeter& getType, const Voronoi::BoxDiagram& diagram, int startIdx, std::unordered_set<int>* assignedIndices, std::optional<int> maxSize = {})
        {
            if (assignedIndices->contains(startIdx))
                return {};

            // Init cluster
            auto type = getType(typeMap[startIdx]);
            QSet<int> result = { startIdx };
            (*assignedIndices) << startIdx;

            auto hullCheck = [&](int idx)
            {
                return getType(typeMap[idx]) == type
                    && !result.contains(idx)
                    && !assignedIndices->contains(idx);
            };

            // Init hull
            QSet<int> hull = result;
            while (true)
            {
                // Get candidates to add
                for (int hid : QSet<int>(hull))
                    diagram.expandCellularCluster(&hull, hid, hullCheck);

                hull -= result;

                // Grow if able
                if (hull.isEmpty())
                    goto DONE;

                for (int id : hull)
                {
                    result << id;
                    (*assignedIndices) << id;

                    if (maxSize && result.size() == maxSize)
                        goto DONE;
                }
            }

            DONE:
            return result;
        }
    }
}
