#include "stdafx.h"
#include "OmnigenProfilerSection.h"
#include "Omnigen.h"

#include <QVBoxLayout>
#include <QTreeView>
#include <QHeaderView>

OmnigenProfiler* OmnigenProfiler::instance;

ProfilerTreeModel::ProfilerTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , rootItem(new ProfilerTreeItem({ "", "" }, 0))
{
}

QModelIndex ProfilerTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    ProfilerTreeItem* parentItem;

    if (!parent.isValid())
        parentItem = rootItem.get();
    else
        parentItem = static_cast<ProfilerTreeItem*>(parent.internalPointer());

    ProfilerTreeItem* childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex ProfilerTreeModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return QModelIndex();

    ProfilerTreeItem* childItem = static_cast<ProfilerTreeItem*>(index.internalPointer());
    ProfilerTreeItem* parentItem = childItem->getParentItem();

    if (parentItem == rootItem.get() || !parentItem)
        return QModelIndex();

    return createIndex(parentItem->childNumber(), 0, parentItem);
}

int ProfilerTreeModel::rowCount(const QModelIndex& parent) const
{
    ProfilerTreeItem* parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem.get();
    else
        parentItem = static_cast<ProfilerTreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

int ProfilerTreeModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return static_cast<ProfilerTreeItem*>(parent.internalPointer())->columnCount();
    return rootItem->columnCount();
}

ProfilerTreeItem* ProfilerTreeModel::getItem(const QModelIndex& index) const
{
    if (index.isValid()) {
        ProfilerTreeItem* item = static_cast<ProfilerTreeItem*>(index.internalPointer());
        if (item)
            return item;
    }
    return rootItem.get();
}

bool ProfilerTreeModel::removeRows(int position, int rows, const QModelIndex& parent)
{
    ProfilerTreeItem* parentItem = getItem(parent);
    if (!parentItem)
        return false;

    beginRemoveRows(parent, position, position + rows - 1);
    const bool success = parentItem->removeChildren(position, rows);
    endRemoveRows();

    return success;
}

QModelIndex ProfilerTreeModel::getIndex(ProfilerTreeItem* item)
{
    return createIndex(item->childNumber(), 0, item);
}

QVariant ProfilerTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    ProfilerTreeItem* item = static_cast<ProfilerTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole)
        return item->data(index.column());
    else if (role == Qt::ForegroundRole)
    {
        // Green color for main categories, cyan for multi thread entries, white for rest
        if (!index.parent().isValid())
            return QVariant(QColor(Qt::green));
        else if (item->getMultiThreading())
            return QVariant(QColor(Qt::cyan));
        else
            return QVariant(QColor(Qt::white));
    }
    else
        return QVariant();
}

Qt::ItemFlags ProfilerTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant ProfilerTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return columnNames[section];

    return QVariant();
}

ProfilerTreeItem::ProfilerTreeItem(const std::vector<QVariant>& data, ProfilerTreeItem* parent)
    : itemData(data)
    , parentItem(parent)
{}

ProfilerTreeItem::~ProfilerTreeItem()
{
    qDeleteAll(childItems);
}

void ProfilerTreeItem::appendChild(ProfilerTreeItem* item)
{
    childItems.push_back(item);
}

ProfilerTreeItem* ProfilerTreeItem::child(int row)
{
    if (row < 0 || row >= childItems.size())
        return nullptr;
    return childItems.at(row);
}

int ProfilerTreeItem::childCount() const
{
    return childItems.size();
}

int ProfilerTreeItem::columnCount() const
{
    return itemData.size();
}

QVariant ProfilerTreeItem::data(int column) const
{
    if (column < 0 || column >= itemData.size())
        return QVariant();

    return itemData.at(column);
}

ProfilerTreeItem* ProfilerTreeItem::getParentItem()
{
    return parentItem;
}

int ProfilerTreeItem::childNumber() const
{
    if (parentItem)
        return indexOf(parentItem->childItems, const_cast<ProfilerTreeItem*>(this));

    return 0;
}

bool ProfilerTreeItem::removeChildren(int position, int count)
{
    if (position < 0 || position + count > childItems.size())
        return false;

    for (int row = 0; row < count; ++row)
        delete childItems[position + row];

    childItems.erase(childItems.begin() + position, childItems.begin() + position + count);

    return true;
}

bool ProfilerTreeItem::setData(int column, const QVariant& value)
{
    if (column < 0 || column >= itemData.size())
        return false;

    itemData[column] = value;
    return true;
}

QOmnigenProfilerSection::QOmnigenProfilerSection(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    treeView = new QTreeView;
    treeView->setStyleSheet("padding: 0ex; font: 12px;");
    treeView->setModel(&model);

    layout->addWidget(treeView);

    treeView->show();

    resize(INT_MAX, INT_MAX);
    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum));
    treeView->setColumnWidth(0, 200);
    treeView->setColumnWidth(1, 300);

    treeView->setSelectionMode(QAbstractItemView::NoSelection);
    treeView->setFocusPolicy(Qt::NoFocus);

    // Setup main sections which will be later populated with entries
    setupSections();
}

void QOmnigenProfilerSection::addProfilerEntry(const EntryInfo& entryData, const QString& messageTemplate, ProfilerTreeItem* par)
{
    model.beginResetModel();

    auto parentIndex = model.getIndex(par);

    //Check for duplicates inside one section
    auto matches = model.match(model.index(0, 0, parentIndex), Qt::DisplayRole, entryData.entryName, 1, Qt::MatchStartsWith);
    if (!matches.isEmpty())
    {
        return;
    }

    auto newItem = new ProfilerTreeItem({ entryData.entryName, messageTemplate }, par);
    newItem->setItemTime(entryData.operationTime);

    if (entryData.multiThread)
        newItem->setMultiThreading(true);

    OmnigenProfiler::get()->profilerItemMap.insert(entryData.entryId, newItem);

    par->appendChild(newItem);

    treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    model.endResetModel();
}

void QOmnigenProfilerSection::setupSections()
{
    model.beginResetModel();

    auto* generationItem = new ProfilerTreeItem({ "Generation", "" }, model.getRootItem());
    model.getRootItem()->appendChild(generationItem);
    generationSection = generationItem;

    for (int i = 0; i <= static_cast<int>(EGenerationStage::LastFunctional); i++)
    {
        auto* newItem = new ProfilerTreeItem({ toQString(static_cast<EGenerationStage>(i)), "" }, model.getRootItem());
        model.getRootItem()->appendChild(newItem);
        parentSections[EGenerationStage(i)] = newItem;
    }

    model.endResetModel();
}

void QOmnigenProfilerSection::calculateSectionTimes(double generationTime)
{
    auto gatheredTimes = OmnigenProfiler::get()->getGatheredTimes();

    ProfilerTreeItem* par = generationSection;
    QString messageTemplate;

    // Create Generation section entry (full generation time)
    messageTemplate = QString("(" + QString::number(generationTime / 1000000000, 'f', 2) + "s)");
    par->setData(1, messageTemplate);
    double sectionsCollectiveTime = 0.0f;

    // Create "Main section" entries (each sections times and it's % in whole generation)
    for (auto&& it = (*gatheredTimes).keyValueBegin(); it != (*gatheredTimes).keyValueEnd(); ++it)
    {
        auto section = (*it).first;
        sectionsCollectiveTime += (*it).second;

        par = parentSections.value(section);
        double generationFraction = ((*it).second / generationTime) * 100.0f;
        messageTemplate = QString("(" + QString::number((*it).second / 1000000000, 'f', 2) + "s  " + QString::number(generationFraction, 'f', 2) + "%)");

        par->setData(1, messageTemplate);
    }

    // Adds additional entry if time from each section =/= generation time
    double unaccountedTime = (generationTime - sectionsCollectiveTime) / 1000000000;

    if (unaccountedTime > 0.5f)
    {
        messageTemplate = QString("    (" + QString::number(unaccountedTime, 'f', 2) + "s " + QString::number((unaccountedTime / (generationTime / 1000000000)) * 100.0f, 'f', 2) + "%)");
        addProfilerEntry({ unaccountedTime, "Unaccounted Time", "Unaccounted Time" }, messageTemplate, generationSection);
    }
}

void QOmnigenProfilerSection::calculateOperationTimes(ProfilerTreeItem* parent, const EntryInfo& entryData, double parentTime, int idx)
{
    QString messageTemplate;
    double operationTime = 0.0f;
    bool multiThread = entryData.multiThread;

    // Check if the entry was multi threaded, and pick worst time
    if (entryData.multiThread)
    {
        double worstTime = entryData.operationTime;

        // If no other times were found, entry was wrongly marked as multi threaded
        if (entryData.threadTimes.size() == 1)
            multiThread = false;

        for (auto&& time : entryData.threadTimes)
            if (time > worstTime)
                worstTime = time;

        operationTime = worstTime;
    }
    else
        operationTime = entryData.operationTime;

    // Calculate operation time % of whole section time
    double sectionFraction = 0.0f;
    if (parentTime > 0.0f)
        sectionFraction = (operationTime / parentTime) * 100.0f;

    messageTemplate = QString(QString(((idx + 1) * 4), ' ') + "(" + QString::number(operationTime / 1000000000, 'f', 2) + "s  " + QString::number(sectionFraction, 'f', 2) + "%)");

    addProfilerEntry({operationTime, entryData.entryName, entryData.entryId, multiThread}, messageTemplate, parent);
}

void QOmnigenProfilerSection::populateProfiler()
{
    // Each section (Tree View 'parent') has a vector of struct EntryInfo (operation times, entry name, entry Id, multi threading) which has been added during generation
    auto* gatheredData = OmnigenProfiler::get()->getGatheredData();

    for (auto&& kv = (*gatheredData).keyValueBegin(); kv != (*gatheredData).keyValueEnd(); ++kv)
    {
        auto idx = (*kv).first;
        auto& section = (*kv).second;

        for (auto&& it = section.keyValueBegin(); it != section.keyValueEnd(); ++it)
        {
            auto& parentId = (*it).first;
            for (int i = 0; i < (*it).second.size(); i++)
            {
                ProfilerTreeItem* par;
                double sectionTime = 0;

                if (idx == 0)
                {
                    EGenerationStage profilerSection = static_cast<EGenerationStage>(parentId.toInt());
                    par = parentSections[profilerSection];

                    sectionTime = OmnigenProfiler::get()->getGatheredTimes()->value(profilerSection);
                    calculateOperationTimes(par, (*it).second.at(i), sectionTime, idx);
                }
                else
                {
                    par = OmnigenProfiler::get()->profilerItemMap.value(parentId);

                    sectionTime = par->getItemTime();
                    calculateOperationTimes(par, (*it).second.at(i), sectionTime, idx);
                }
            }
        }
    }
}

void QOmnigenProfilerSection::clearProfiler()
{
    model.beginResetModel();

    // Clearing the Generation section
    generationSection->setData(1, "");

    if (model.hasChildren(model.index(0, 0)))
        model.removeRows(0, generationSection->childCount(), model.getIndex(generationSection));

    foreach(ProfilerTreeItem * item, parentSections)
    {
        auto parentIndex = model.getIndex(item);
        item->setData(1, "");

        if (model.hasChildren(parentIndex))
            model.removeRows(0, item->childCount(), parentIndex);
    }

    model.endResetModel();
}

OmnigenProfiler* OmnigenProfiler::get()
{
    if (!instance)
        instance = new OmnigenProfiler();

    return instance;
}

void OmnigenProfiler::clearData()
{
    gatheredEntries.clear();
    gatheredMainTimes.clear();
    profilerItemMap.clear();
    entryStack.clear();
    threadStack.clear();
}

OmnigenProfiler::OmnigenProfiler()
{
}

void OmnigenProfiler::gatherData(EGenerationStage section, const EntryInfo& entryData)
{
    // Gather data for idx 0 entries (these will go to the main sections)
    QString sectionName = toQString(static_cast<int>(section));

    if (!gatheredEntries[0].contains(sectionName))
    {
        gatheredEntries[0].insert(sectionName, { entryData });
        gatheredMainTimes.insert(section, entryData.operationTime);
    }
    else
    {
        gatheredEntries[0][sectionName] << entryData;
        gatheredMainTimes[section] = gatheredMainTimes[section] + entryData.operationTime;
    }
}

void OmnigenProfiler::gatherData(const QString& sectionId, const EntryInfo& entryData, int idx)
{
    // All other idx entries
    if (!gatheredEntries[idx].contains(sectionId))
        gatheredEntries[idx].insert(sectionId, { entryData });
    else
        gatheredEntries[idx][sectionId] << entryData;
}

void OmnigenProfiler::appendMultiThreadData(const QString& entryId, const QString& entryText, double operationTime, std::thread::id threadId /*= {}*/)
{
    std::scoped_lock<std::mutex> lock(m_Mutex);

    int idx = static_cast<int>(getEntryStack().size()) + indexOf(getThreadEntries(threadId), entryId);
    Q_ASSERT(idx >= 0);
    popThreadStack(threadId);

    QString parentId;
    auto lastEntry = getLastThreadEntry(threadId);

    if (!lastEntry.isEmpty())
        parentId = lastEntry;
    else if (getEntryStack().size() > 0)
        parentId = getEntryStack().back();
    else
        parentId = toQString(static_cast<int>(*Generation::Data::get()->getStageBeingGenerated()));

    // Search for existing 'entryId' entry
    auto* gatheredData = getGatheredData();

    for (int i = 0; i < (*gatheredData)[idx][parentId].size(); i++)
    {
        if ((*gatheredData)[idx][parentId][i].checkId(entryId))
        {
            (*gatheredData)[idx][parentId][i].appendThreadTime(threadId, operationTime);
            return;
        }
    }

    // Add a new entry if none found
    QMap<std::thread::id, double> threadTimes;
    threadTimes.insert(threadId, operationTime);
    gatherData(parentId, { 0, entryText, entryId, true, threadTimes }, idx);
}

// For Profiler debugging purposes
void OmnigenProfiler::sendMultiThreadLogMessage(const QString& entryName)
{
    std::scoped_lock<std::mutex> lock(m_Mutex);

    if (contains(multiThreadLogCheck, entryName))
        return;

    multiThreadLogCheck << entryName;
    OmniLog(ELoggingLevel::Warn) << "Profiling attempt of multi thread operation: " << entryName <<= " - without declaring OmniProfileMultiThread first!";
}