#pragma once
#include "Utils/OmniBin/OmniBinQt.h"
#include "UrbanUtils.h"

namespace Generation {
    class UrbanSuggestion;
}

class DUrbanHandle;

void omniSave(const Generation::UrbanSuggestion& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(Generation::UrbanSuggestion& object, OmniBin<std::ios::in>& omniBin);

namespace Generation
{
    class UrbanSuggestion : public QEnableSharedFromThis<UrbanSuggestion>
    {
    public:
        UrbanSuggestion() = default;
        explicit UrbanSuggestion(int inId);
        ~UrbanSuggestion();

        void initializeHandle();
        auto& getHandle() const { return handle; }
        void showHandle(const bool bShow) const;

        static void generateSuggestions();
        const int getInitialCellId() const { return initialCellId; }
        const qint64 getGuid() const { return guid; }

        bool getIsSelected() const { return isSelected; }
        bool getShouldGenerate() const { return shouldGenerate; }

        void setIsSelected(const bool val) { isSelected = val; }
        void setShouldGenerate(const bool val) { shouldGenerate = val; }

        EUrbanSize getAreaSize() const { return areaSize; }
        void setAreaSize(const EUrbanSize& val) { areaSize = val; }

        EUrbanSize getMaxAreaSize() const { return maxAreaSize; }
        void setMaxAreaSize(const EUrbanSize& val);

        const QString& getName() const { return name; }
        void setName(const QString& inName);

        bool getGenPerimeterRoads() const { return generatePerimeterRoads; }
        void setGenPerimeterRoads(const bool val) { generatePerimeterRoads = val; }

        QVector3D getCenterPoint3D() const;
    private:
        int initialCellId = -1;
        bool isSelected = false;
        bool shouldGenerate = true;
        bool generatePerimeterRoads = false;

        qint64 guid;
        QSharedPointer<DUrbanHandle> handle;
        QVector3D cachedVertex;
        EUrbanSize areaSize = EUrbanSize::Town;
        EUrbanSize maxAreaSize = EUrbanSize::Last;
        QString name;
     
        FRIEND_OMNIBIN_NS(UrbanSuggestion);
    };
}

inline void omniSave(const Generation::UrbanSuggestion& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.initialCellId;
    omniBin << object.shouldGenerate;
    omniBin << object.guid;
    omniBin << object.areaSize;
    omniBin << object.name;
    omniBin << object.generatePerimeterRoads;
    omniBin << object.cachedVertex;
}

inline void omniLoad(Generation::UrbanSuggestion& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.initialCellId;
    omniBin >> object.shouldGenerate;
    omniBin >> object.guid;
    omniBin >> object.areaSize;
    omniBin >> object.name;
    omniBin >> object.generatePerimeterRoads;
    omniBin >> object.cachedVertex;
}

