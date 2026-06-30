#pragma once
#include "../SelectionMgr.h"
#include "../Layout/LayoutSelection.h"

namespace Design
{
    enum class ERidgesSelection
    {
        Ridge,
        Domain,

        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(ERidgesSelection, ERidgesSelection::None);

    using RidgesSelectionMgr = SelectionMgr<ERidgesSelection>;

    template<>
    class Selection<ERidgesSelection, ERidgesSelection::Ridge> : public SelectionBase, public QEnableSharedFromThis<Selection<ERidgesSelection, ERidgesSelection::Ridge>>
    {
    public:
        using DataType = QSharedPointer<DRidgeMarker>;

        static bool findOnScene(QMap<ERidgesSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive);
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inRidges);
        static bool isRidgeHovered(const DataType& ridge);

        Selection(const std::any& inRidge);

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

        const auto& getRidgePtr() const { return ridgePtr; };

        void setSelectionFreeze(bool b) { bFrozenSelection = b; };

    protected:
        Selection() = default;

        QSharedPointer<DRidgeMarker> ridgePtr;

        static inline QWeakPointer<DRidgeMarker> currentHover = {};

        template<typename T>
        friend class QSharedPointer;
    };
    using RidgeSelection = Selection<ERidgesSelection, ERidgesSelection::Ridge>;

    template<>
    class Selection<ERidgesSelection, ERidgesSelection::Domain> : public DomainSelection, public QEnableSharedFromThis<Selection<ERidgesSelection, ERidgesSelection::Domain>>
    {
    public:
        using DataType = QSharedPointer<DDomainHandle>;

        static bool findOnScene(QMap<ERidgesSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static QMenu* requestContextMenu(const std::any& data);

        Selection(const std::any& inHandle);

    protected:
        Selection() = default;

        template<typename T>
        friend class QSharedPointer;

        template<EGenerationStage>
        friend class StageTools;
    };
    using RidgeDomainSelection = Selection<ERidgesSelection, ERidgesSelection::Domain>;
}