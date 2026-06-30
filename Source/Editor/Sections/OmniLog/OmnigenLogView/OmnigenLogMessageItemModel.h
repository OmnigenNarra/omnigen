#pragma once
#include <QAbstractTableModel>
#include "../OmniLogger.h"

class OmnigenLogMessageItemModel: public QAbstractTableModel
{
    Q_OBJECT

public:
    OmnigenLogMessageItemModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {};
    int rowCount(const QModelIndex& parent = QModelIndex()) const;
    int columnCount(const QModelIndex& parent = QModelIndex()) const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

    void insertRowsFromBuffer();
    void insertRow(std::vector<QString> input);
    void changeRow(std::vector<QString> input);
    void clear();
    QString getContentString();

private:
    const std::vector<QString> columnNames = { "Timestamp", "Category", "Message" };
    std::vector<std::vector<QString>> rowContent;
    QMap<QString, int>  rowId;

    std::vector<std::vector<QString>> inputBuffer;
};

