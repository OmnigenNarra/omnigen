#pragma once
#include "../SelectionMgr.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"

namespace Design
{
    enum class ELandmassSelection
    {
        Shoreline,
        Landmass,

        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(ELandmassSelection, ELandmassSelection::None);

    using LandmassSelectionMgr = SelectionMgr<ELandmassSelection>;

    template<>
    class Selection<ELandmassSelection, ELandmassSelection::Landmass> : public SelectionBase, public QEnableSharedFromThis<Selection<ELandmassSelection, ELandmassSelection::Landmass>>
    {
    public:
        using DataType = QSharedPointer<DLandmassMarker>;

        static bool findOnScene(QMap<ELandmassSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive);
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inLandmasses);
        static bool isLandmassHovered(const DataType& landmass);

        Selection(const std::any& inLandmass);

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

    protected:
        Selection() = default;

        QSharedPointer<DLandmassMarker> landmassPtr;

        static inline QWeakPointer<DLandmassMarker> currentHover = {};

        template<typename T>
        friend class QSharedPointer;
    };
    using LandmassSelection = Selection<ELandmassSelection, ELandmassSelection::Landmass>;

    template<>
    class Selection<ELandmassSelection, ELandmassSelection::Shoreline> : public SelectionBase, public QEnableSharedFromThis<Selection<ELandmassSelection, ELandmassSelection::Shoreline>>
    {
    public:
        using DataType = QSharedPointer<DShorelineMarker>;

        static QWeakPointer<DShorelineMarker> getCurrentHover() { return currentHover; }

        static bool findOnScene(QMap<ELandmassSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive);
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inShorelines);
        static bool isShorelineHovered(const QSharedPointer<DShorelineMarker>& shoreline);

        Selection(const std::any& inShorelines);

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

    protected:
        Selection() = default;

        QSharedPointer<DShorelineMarker> shorelinePtr;

        static inline QWeakPointer<DShorelineMarker> currentHover = {};

        template<typename T>
        friend class QSharedPointer;

        template<EGenerationStage>
        friend class StageTools;
    };
    using ShorelineSelection = Selection<ELandmassSelection, ELandmassSelection::Shoreline>;
}