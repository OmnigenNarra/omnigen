#include "stdafx.h"
#include "OmnigenLogMessageItemModel.h"
#include "Omnigen.h"

#include <QTableView>
#include <QScrollBar>
#include <QHeaderView>

int OmnigenLogMessageItemModel::rowCount(const QModelIndex& parent) const
{
    int result = rowContent.size();
    return result;
}

int OmnigenLogMessageItemModel::columnCount(const QModelIndex& parent) const
{
    int result = columnNames.size();
    return result;
}

QVariant OmnigenLogMessageItemModel::data(const QModelIndex& index, int role) const
{
    // Below is a precaution preventing model trying to access non-existing data
    if (index.column() < 0 || index.column() > 2)
        return QVariant();

    switch (role)
    {
        case Qt::DisplayRole:
        {
            return rowContent[index.row()][index.column()];
        }
        case Qt::TextAlignmentRole:
        {
            return (Qt::AlignLeft + Qt::AlignVCenter);
        }
        case Qt::BackgroundColorRole:
        {
            return QVariant();
        }
        case Qt::TextColorRole:
        {
            if (rowContent[index.row()][1].startsWith('C')) // Critical
            {
                return QVariant(QColor(255, 0, 255));
            }
            else if (rowContent[index.row()][1].startsWith('E')) // Error
            {
                return QVariant(QColor(255, 100, 100));
            }
            else if (rowContent[index.row()][1].startsWith('W')) // Warning
            {
                return QVariant(QColor(255, 255, 0));
            }
            else if (rowContent[index.row()][1].startsWith('I')) // Info
            {
                return QVariant(QColor(200, 255, 200));
            }
            else if (rowContent[index.row()][1].startsWith('D')) // Debug
            {
                return QVariant(QColor(255, 255, 255));
            }
            else if (rowContent[index.row()][1].startsWith('T')) // Trace
            {
                return QVariant(QColor(150, 150, 150));
            }
            else
            {
                return QVariant();
            }
        }
        default:
        {
            return QVariant();
        }
    }
}

QVariant OmnigenLogMessageItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    // Below is a precaution preventing model trying to access non-existing data
    if (section > 2 || section < 0)
        return QVariant();

    switch (role)
    {
        case Qt::DisplayRole:
            return columnNames[section];
        case Qt::SizeHintRole:
        {
            auto* p = qobject_cast<QTableView*>(QObject::parent());
            if (p == nullptr) 
                return QVariant();

            // Parent total width.
            const int parentWidth = p->viewport()->size().width() - p->verticalScrollBar()->sizeHint().width();
            QSize qSize;
            // Default height.
            qSize.setHeight(p->verticalHeader()->defaultSectionSize());
            // Width per column.

            switch (section) 
            {
                case 0:
                    qSize.setWidth(parentWidth * 0.25);
                    //OmniLog() <<= parentWidth * 0.15;
                    return QVariant(qSize);
                case 1:
                    qSize.setWidth(parentWidth * 0.15);
                    //OmniLog() <<= parentWidth * 0.15;
                    return QVariant(qSize);
                case 2: 
                    qSize.setWidth(parentWidth * 0.6);
                    //OmniLog() <<= parentWidth * 0.7;
                    return QVariant(qSize);
                default:
                    return QVariant();
            }
        }
        default:
            return QVariant();
    }
}


void OmnigenLogMessageItemModel::insertRowsFromBuffer()
{
    if (Omnigen::get()->isGenerating() || inputBuffer.empty())
        return;

    layoutAboutToBeChanged();
    int rowCount = rowContent.size();
    beginInsertRows(QModelIndex(), rowCount, rowCount + inputBuffer.size());
    for (auto rowMessage : inputBuffer)
        rowContent.push_back(rowMessage);

    inputBuffer.clear();
    layoutChanged();
    endInsertRows();
}

void OmnigenLogMessageItemModel::insertRow(std::vector<QString> input)
{
    inputBuffer.push_back(input);
}

void OmnigenLogMessageItemModel::changeRow(std::vector<QString> input)
{
    // Check if log entry with given Id exists
    if (rowId.contains(input.at(3)))
    {
        int rowNumber = rowId[input.at(3)];
        rowContent[rowNumber] = input;
    }
    else
    {
        insertRow(input);
        rowId.insert(input.at(3), int(rowContent.size()) - 1);
    }
}

void OmnigenLogMessageItemModel::clear()
{
    int lastIndex = (rowContent.size()>0)?(rowContent.size()-1):(rowContent.size());
    if (lastIndex <= 0)
        return;

    beginRemoveRows(QModelIndex(), 0, lastIndex);

    rowContent.clear();
    rowId.clear();

    endRemoveRows();
}

QString OmnigenLogMessageItemModel::getContentString()
{
    QString result = "";

    for (auto&& rowIterator : rowContent)
    {
        for (auto&& columnIterator : rowIterator)
        {
            result.append(columnIterator).append("\t");
        }
        result.push_back("\n");
    }

    return result;
}
