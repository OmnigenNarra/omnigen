#pragma once
#include "Editor/StageTools/SelectionMgr.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"

namespace Design
{
    enum class EPIESelection
    {
        SpawnSelector,

        None
    };

    ENABLE_ENUM_AS_CONSTEXPR(EPIESelection, EPIESelection::None);

    using PIESelectionMgr = SelectionMgr<EPIESelection>;

    template<>
    class Selection<EPIESelection, EPIESelection::SpawnSelector>
        : public SelectionBase, public QEnableSharedFromThis<Selection<EPIESelection, EPIESelection::SpawnSelector>>
    {
    public:
        using DataType = QVector3D;

        Selection(const std::any& inHandle);
        static bool findOnScene(QMap<EPIESelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive) {};
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<QVector3D>& inHandles);

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    protected:
        Selection() = default;

        DataType spawnLocation;
        mutable QSharedPointer<DLineMarker> debugMarker;

        template<typename T>
        friend class QSharedPointer;
    };

    using PIESelection = Selection<EPIESelection, EPIESelection::SpawnSelector>;
}

