#pragma once
#include "../SelectionMgr.h"
#include "../Layout/LayoutSelection.h"

namespace Generation
{
    class DPlant;
}

namespace Design
{
    enum class EFoliageSelection
    {
        Instance,

        None
    };
    ENABLE_ENUM_AS_CONSTEXPR(EFoliageSelection, EFoliageSelection::None);

    using FoliageSelectionMgr = SelectionMgr<EFoliageSelection>;

    struct FoliageInstance
    {
        QSharedPointer<Generation::DPlant> plant;
        IndexType instanceIdx;

        bool operator==(const FoliageInstance& other) const noexcept
        {
            return plant == other.plant && instanceIdx == other.instanceIdx;
        }
    };

    inline quint32 qHash(FoliageInstance key, uint seed)
    {
        return qHash(std::pair{key.plant, key.instanceIdx}, seed);
    }

    template<>
    class Selection<EFoliageSelection, EFoliageSelection::Instance> : public SelectionBase, public QEnableSharedFromThis<Selection<EFoliageSelection, EFoliageSelection::Instance>>
    {
    public:
        using DataType = FoliageInstance;

        static bool findOnScene(QMap<EFoliageSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
        static void hoverUpdate(const std::any& data, bool isLive);
        static QMenu* requestContextMenu(const std::any& data);
        static void getData(const SelectionBase* obj, QSet<DataType>* data);
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inRidges);

        Selection(const std::any& inInstance);

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;

        virtual void update(const std::any& newInstance, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;

        virtual void select() const override;
        virtual void deselect() const override;

        virtual QVector3D getPosition() const override;

    protected:
        Selection() = default;

        QSet<FoliageInstance> instances;

        template<typename T>
        friend class QSharedPointer;
    };
    using FoliageInstanceSelection = Selection<EFoliageSelection, EFoliageSelection::Instance>;
}