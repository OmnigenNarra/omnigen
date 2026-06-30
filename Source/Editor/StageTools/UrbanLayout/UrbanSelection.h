#pragma once
#include "../SelectionMgr.h"

class DLineMarker;

namespace Design
{
    enum class EUrbanSelection
    {
        Site,

        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(EUrbanSelection, EUrbanSelection::None);

    using UrbanSelectionMgr = SelectionMgr<EUrbanSelection>;

    template<>
    class Selection<EUrbanSelection, EUrbanSelection::Site> : public SelectionBase, public QEnableSharedFromThis<Selection<EUrbanSelection, EUrbanSelection::Site>>
    {
    public:
        using DataType = QSharedPointer<Generation::UrbanSite>;

        Selection(const std::any& site);
        static bool findOnScene(QMap<EUrbanSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive) {};
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<QSharedPointer<Generation::UrbanSite>>& inSites);

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
    protected:
        Selection() = default;

        QSharedPointer<Generation::UrbanSite> sitePtr;

        mutable QSharedPointer<DLineMarker> debugMarker;

        template<typename T>
        friend class QSharedPointer;
    };
    using SiteSelection = Selection<EUrbanSelection, EUrbanSelection::Site>;
}
