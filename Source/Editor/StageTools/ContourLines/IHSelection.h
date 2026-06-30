#pragma once
#include "../SelectionMgr.h"

namespace Design
{
	enum class EIHSelection
	{
		IHs,
		Points,
		None
	};
	ENABLE_ENUM_AS_CONSTEXPR(EIHSelection, EIHSelection::None);

	using RidgesSelectionMgr = SelectionMgr<EIHSelection>;

	template<>
	class Selection<EIHSelection, EIHSelection::IHs> : public SelectionBase, public QEnableSharedFromThis<Selection<EIHSelection, EIHSelection::IHs>>
	{
	public:
		// Interface
		using DataType = QSharedPointer<Isohypse>;

		static bool findOnScene(QMap<EIHSelection, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData);
		static void hoverUpdate(const std::any& data, bool isLive);
		static QMenu* requestContextMenu(const std::any& data);
		static void getData(const SelectionBase* obj, QSet<DataType>* data);
		static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inIH);

		Selection(const std::any& inIH);
		virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() override;
		virtual void update(const std::any& newSquare, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override;
		virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override;
		virtual void select() const override;
		virtual void deselect() const override;
		virtual QVector3D getPosition() const override;

	protected:
		Selection() = default;

		template<typename T>
		friend class QSharedPointer;

		// Custom
	public:
		const auto& getIH() const { return ih; };

	protected:
		DataType ih;
	};
	using IHSelection = Selection<EIHSelection, EIHSelection::IHs>;
}