#pragma once
#include "UrbanHandleDrawable.h"
#include "../SelectionMgr.h"

namespace Design
{
    enum class EUrbanLayoutSelection
    {
        SuggestionHandle,

        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(EUrbanLayoutSelection, EUrbanLayoutSelection::None);

    using UrbanLayoutSelectionMgr = SelectionMgr<EUrbanLayoutSelection>;

    template<>
    class Selection<EUrbanLayoutSelection, EUrbanLayoutSelection::SuggestionHandle>
    : public SelectionBase, public QEnableSharedFromThis<Selection<EUrbanLayoutSelection, EUrbanLayoutSelection::SuggestionHandle>>
    {
    public:
        using DataType = QSharedPointer<DUrbanHandle>;

        Selection(const std::any& inHandle);
        static bool findOnScene(QMap<EUrbanLayoutSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive) {};
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<QSharedPointer<DUrbanHandle>>& inHandles);

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    protected:
        Selection() = default;

        DataType handlePtr;
        mutable QSharedPointer<Generation::UrbanSuggestion> suggestionPtr;

        template<typename T>
        friend class QSharedPointer;
    };

    using SuggestionSelection = Selection<EUrbanLayoutSelection, EUrbanLayoutSelection::SuggestionHandle>;
}

