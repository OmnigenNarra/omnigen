#pragma once
#include <QTreeView>
#include "Utils/CoreUtils.h"
#include "Scene/Generation/OmnigenGenerationStage.h"

#include <thread>
#include <mutex>

class ProfilerTreeItem;
class OmnigenLogSection;

// This sections measures generation times for all stages
// Can also be used for generation-unrelated measurements (dev-only)
struct EntryInfo
{
    double operationTime = 0;
    QString entryName = "";
    QString entryId = "";
    bool multiThread = false;
    QMap<std::thread::id, double> threadTimes = {};

    double getOperationTime() { return operationTime; };
    const QString& getEntryName() { return entryName; };
    const QString& getEntryId() { return entryId; };
    void appendTime(double timeToAdd) { operationTime += timeToAdd; };
    void appendThreadTime(std::thread::id thread, double time) { threadTimes[thread] += time; };
    bool checkId(const QString& entryIdToCheck) { return entryId == entryIdToCheck; };
    bool checkIfMultiThreading() { return multiThread; };
};

class ProfilerTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    ProfilerTreeModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    ProfilerTreeItem* getItem(const QModelIndex& index) const;
    bool removeRows(int position, int rows, const QModelIndex& parent);
    QModelIndex getIndex(ProfilerTreeItem* item);

    ProfilerTreeItem* getRootItem() const { return rootItem.get(); };

private:
    std::array<QString, 2> columnNames = { "Section", "Time" };
    QScopedPointer<ProfilerTreeItem> rootItem;

    friend class QOmnigenProfilerSection;
};

class ProfilerTreeItem
{
public:

    explicit ProfilerTreeItem(const std::vector<QVariant>& data, ProfilerTreeItem* parent = nullptr);
    ~ProfilerTreeItem();

    void appendChild(ProfilerTreeItem* child);

    ProfilerTreeItem* child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    ProfilerTreeItem* getParentItem();
    int childNumber() const;
    bool removeChildren(int position, int count);
    bool setData(int column, const QVariant& value);
    void setItemTime(double time) { itemTime = time; };
    double getItemTime() { return itemTime; };
    void setMultiThreading(bool mt) { multiThreading = mt; };
    bool getMultiThreading() { return multiThreading; };

private:
    std::vector<ProfilerTreeItem*> childItems;
    std::vector<QVariant> itemData;
    ProfilerTreeItem* parentItem;
    double itemTime;
    bool multiThreading = false;
};

class QOmnigenProfilerSection : public QWidget
{
    Q_OBJECT

public:
    QOmnigenProfilerSection(QWidget* parent);
    void populateProfiler();
    void clearProfiler();

private:
    void addProfilerEntry(const EntryInfo& entryData, const QString& messageTemplate, ProfilerTreeItem* par);
    void setupSections();
    void calculateSectionTimes(double generationTime);
    void calculateOperationTimes(ProfilerTreeItem* parent, const EntryInfo& entryData, double parentTime, int idx);

    QTreeView* treeView = nullptr;
    ProfilerTreeModel model;
    QMap<EGenerationStage, ProfilerTreeItem*> parentSections = {};
    ProfilerTreeItem* generationSection;

    friend class OmnigenProfilerStart;
};

class OmnigenProfiler
{
private:
    OmnigenProfiler();

    static OmnigenProfiler* get();
    void gatherData(EGenerationStage section, const EntryInfo& entryData);
    void gatherData(const QString& sectionId, const EntryInfo& entryData, int idx);
    void appendMultiThreadData(const QString& entryId, const QString& entryText, double operationTime, std::thread::id threadId = {});

    void clearData();
    QMap<int, QMap<QString, std::vector<EntryInfo>>>* getGatheredData() { return &gatheredEntries; };
    QMap<EGenerationStage, double>* getGatheredTimes() { return &gatheredMainTimes; };
    bool isProfiling() { return bIsProfiling; };
    void setIsProfiling(bool started) { bIsProfiling = started; };

    const std::vector<QString>& getEntryStack() { return entryStack; };
    void appendEntryStack(const QString& newEntry) { entryStack.push_back(newEntry); };
    void popEntryStack() { entryStack.pop_back(); };

    //Multi Threading
    void setMainThread(std::thread::id thread) { mainThread = thread; };
    std::thread::id getMainThread() { return mainThread; };

    QString getLastThreadEntry(const std::thread::id& id)
    {
        std::scoped_lock<std::mutex> lock(m_StackMutex);
        if (auto it = threadStack.find(id); it != threadStack.end() && it->size() > 0)
            return it->back();

        return {};
    }

    std::vector<QString> getThreadEntries(const std::thread::id& id)
    {
        std::scoped_lock<std::mutex> lock(m_StackMutex);
        if (auto it = threadStack.find(id); it != threadStack.end())
            return *it;

        return {};
    }

    void appendThreadStack(const std::thread::id& threadId, const QString& entryId)
    {
        std::scoped_lock<std::mutex> lock(m_StackMutex);
        threadStack[threadId] << entryId;
    };

    void popThreadStack(const std::thread::id& threadId)
    {
        std::scoped_lock<std::mutex> lock(m_StackMutex);
        threadStack[threadId].pop_back();
    };

    void clearThreadStack() 
    { 
        std::scoped_lock<std::mutex> lock(m_StackMutex);
        threadStack.clear(); 
    };

    static OmnigenProfiler* instance;

    // <idx, <section name, entry info>>
    QMap<int, QMap<QString, std::vector<EntryInfo>>> gatheredEntries = {};
    QMap<EGenerationStage, double> gatheredMainTimes = {};
    std::vector<QString> entryStack = {};
    QMap<QString, ProfilerTreeItem*> profilerItemMap = {};

    //Multi Threading
    std::mutex m_Mutex;
    std::mutex m_StackMutex;
    QMap<std::thread::id, std::vector<QString>> threadStack;

    void sendMultiThreadLogMessage(const QString& entryName);
    std::vector<QString> multiThreadLogCheck = {};
    QMap<QString, QMap<QString, QPair<QString, QMap<std::thread::id, double>>>> multiThreadEntries;


    std::thread::id mainThread;

    friend class QOmnigenProfilerSection;
    friend class OmnigenProfilerSegment;
    friend class OmnigenProfilerStart;
    friend class OmnigenProfilerMultiThread;

    bool bIsProfiling = false;
};

