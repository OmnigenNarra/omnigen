#pragma once
#include "Scene/Generation/OmnigenGenerationStage.h"
#include <any>
#include <QMenu>
#include <QMouseEvent>
#include <QEnableSharedFromThis>
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

class OmnigenPropertyListBase;

namespace Design
{
    enum class ESelectionStep
    {
        Press,
        Move,
        Release
    };

    // Selection objects define a set of data that can be manipulated in some way.
    // May represent a whole object or just a part of one. It's flexible.
    class SelectionBase
    {
    public:
        SelectionBase();
        virtual ~SelectionBase() = default;

        virtual QSharedPointer<OmnigenPropertyListBase> makePropertyList() { return nullptr; };

        virtual void update(const std::any& newData, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) = 0;
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) = 0;

        virtual void select() const = 0;
        virtual void deselect() const = 0;

        virtual QVector3D getPosition() const = 0;

        bool isSelectionFrozen() const { return bFrozenSelection; };

    protected:
        bool bAppend;
        bool bSubtract;
        bool bFrozenSelection = false;
    };

    // Template implementation
    template<typename SelectionEnum, SelectionEnum SE>
    class Selection : public SelectionBase
    {
    public:
        // Static interface, all specializations must provide these

        // Finds this selection's data under mouse cursor.
        // The data may later be used to 
        // - Construct a selection of this type 
        // - Perform a hover event on governed entity
        // - Request a context menu from governed entity
        // Appends to @output, if found.
        static bool findOnScene(QMap<SelectionEnum, std::any>* output, int x, int y, const QOmnigenViewport::SelectionData& screenData) { return false; };

        // Performs an update on governed data, if applicable
        static void hoverUpdate(const std::any& data, bool isLive) {};

        // Requests a context menu from governed data, if applicable
        static QMenu* requestContextMenu(const std::any& data) { return nullptr; }

        // Data type-driven interface
        using DataType = std::false_type;
        static void getData(const SelectionBase* obj, QSet<DataType>* data) {};
        static std::vector<QSharedPointer<SelectionBase>> createFromData(const QSet<DataType>& inRidges) { return {}; };

        // Constructs this selection from data from findOnScene
        Selection(const std::any& nothing) {};

        // Merges new data with existing selection data.
        // Called during mouse press and move
        virtual void update(const std::any& newData, std::vector<QSharedPointer<SelectionBase>>* currentSelections, const std::vector<QSharedPointer<SelectionBase>>& selectionsSnapshot) override {};

        // Moves work selection (temporary object being constructed between mouse press and mouse release) into (more) permanent storage.
        virtual void save(std::vector<QSharedPointer<SelectionBase>>* currentSelections) override {};

        // The real logic behind selecting and deselecting this selection's governed data.
        virtual void select() const override {};
        virtual void deselect() const override {};

        // Used mostly to move the camera to selection
        virtual QVector3D getPosition() const { return QVector3D(); }
    };

    class SelectionMgrBase : public QObject
    {
        Q_OBJECT;

    public:
        bool isSelecting() const { return bIsSelecting; }

        virtual int getSelectionRadius() const = 0;
        virtual void hoverUpdate(int x, int y) = 0;
        virtual void selectObjects(int x, int y, ESelectionStep ss) = 0;
        virtual void rightClick(QMouseEvent* me) = 0;
        void clearSelection();

        const auto& getAllSelection() const { return selections; }
        const auto& getSelectionType() const { return currentSelectionType; }

        template<typename DrawableType>
        struct ObjectData
        {
            QSharedPointer<DrawableType> object;
            IndexType instance;
            IndexType primitive;
        };

        template<typename DrawableType>
        static std::optional<ObjectData<DrawableType>> findObjectUnderCursor(const QOmnigenViewport::SelectionData& selectionData)
        {
            for (auto&& data : selectionData.selectionBuffer)
                if (data.w() == 1.0f && data.x() < selectionData.selectionMap.size())
                    if (auto object = selectionData.selectionMap[data.x()].dynamicCast<DrawableType>(); object)
                        return ObjectData<DrawableType>{ object, IndexType(data.y()), IndexType(data.z()) };

            return {};
        };

        template<typename DrawableType>
        static std::optional<ObjectData<DrawableType>> findObjectUnderCursor()
        {
            return findObjectUnderCursor<DrawableType>(QOmnigenViewportSection::getActiveViewport()->getSelectionData());
        };

    signals:
        // DON"T EMIT DIRECTLY, USE onSelectionChanged() instead
        void selectionChanged();

    protected:
        void onSelectionChanged();

        int currentSelectionType;
        QMap<int, std::vector<QSharedPointer<SelectionBase>>> selections, selectionsSnapshot;
        QSharedPointer<SelectionBase> workSelection;

        bool bIsSelecting = false;
        bool bAppend = false;
        bool bSubtract = false;
        bool bIsMovingGizmo = false;

        template<typename SelectionEnum, SelectionEnum Value>
        friend class Selection;
        friend class SelectionBase;
    };

    SelectionMgrBase* getSelectionMgr();
}