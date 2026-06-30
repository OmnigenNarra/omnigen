#pragma once
#include "../SelectionMgr.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "../ManipulationGizmoSelection.h"

namespace Design
{
    enum class ELayoutSelection
    {
        Domain,
        Grid,

        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(ELayoutSelection, ELayoutSelection::None);

    using LayoutSelectionMgr = SelectionMgr<ELayoutSelection>;

    template<>
    class Selection<ELayoutSelection, ELayoutSelection::Grid> : public SelectionBase, public QEnableSharedFromThis<Selection<ELayoutSelection, ELayoutSelection::Grid>>
    {
    public:
        using DataType = GPoint;
    
        static bool findOnScene(QMap<ELayoutSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive) {};
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<GPoint>& inSquares);

        Selection(const std::any& sq);

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

    protected:
        Selection() = default;

        void apply(std::vector<QSharedPointer<SelectionBase>>* currentSelections);

        QSet<GPoint> squares;
        GPoint startCoord, endCoord, lowerCoord, higherCoord;
        bool bDiagonal = false;

        template<typename T>
        friend class QSharedPointer;
    };
    using GridSelection = Selection<ELayoutSelection, ELayoutSelection::Grid>;

    template<>
    class Selection<ELayoutSelection, ELayoutSelection::Domain> : public SelectionManipulationGizmo, public QEnableSharedFromThis<Selection<ELayoutSelection, ELayoutSelection::Domain>>
    {
    public:
        using Self = Selection<ELayoutSelection, ELayoutSelection::Domain>;
        using DataType = QSharedPointer<DDomainHandle>;

        static bool findOnScene(QMap<ELayoutSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive);
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inHandle);
        static bool isDomainHovered(const QSharedPointer<DDomainHandle>& hnd);

        Selection(const std::any& inHandle);

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;

        virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

        virtual void grabGizmo(int mousePosX, int mousePosY) override;
        virtual void moveObject(int mousePosX, int mousePosY) override;
        virtual void endGizmoMove() override;

    protected:
        Selection() = default;
        void rainbowSplit() const;

        bool useGizmo = true;

        QSharedPointer<DDomainHandle> handle;
        bool bRainbow = false;

        static inline QWeakPointer<DDomainHandle> currentHover = {};
        static inline QSharedPointer<DDomain> contextMenuDomain;
        static inline std::vector<QSharedPointer<DDomain>> otherSelectedDomains;

        template<typename T>
        friend class QSharedPointer;

        template<EGenerationStage>
        friend class StageTools;
    };
    using DomainSelection = Selection<ELayoutSelection, ELayoutSelection::Domain>;
}