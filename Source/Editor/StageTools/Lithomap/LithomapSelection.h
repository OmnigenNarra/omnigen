#pragma once
#include "../SelectionMgr.h"
#include "../Layout/LayoutSelection.h"

namespace Design
{
    enum class ELithomapSelection
    {
        Cell,
        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(ELithomapSelection, ELithomapSelection::None);
    using LithomapSelectionMgr = SelectionMgr<ELithomapSelection>;

    template<>
    class Selection<ELithomapSelection, ELithomapSelection::Cell> : public SelectionBase, public QEnableSharedFromThis<Selection<ELithomapSelection, ELithomapSelection::Cell>>
    {
    public:
        using DataType = int;

        Selection(const std::any& inHandle);

        static bool findOnScene(QMap<ELithomapSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive);
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inCells);

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

    protected:
        Selection() = default;
        std::unordered_set<int> cellIds;

        static inline std::unordered_set<int> hoveredCells;

        template<typename T>
        friend class QSharedPointer;
    };
    using LithomapCellSelection = Selection<ELithomapSelection, ELithomapSelection::Cell>;
}