#pragma once
#include <QWidget>
#include <QTimer>

class QTableView;
class QHBoxLayout;
class QVBoxLayout;
class OmnigenLogMessageItemModel;
class QScrollArea;

class OmnigenLogSection : public QWidget
{
    Q_OBJECT

public:
    OmnigenLogSection(QWidget* parent, OmnigenLogMessageItemModel* model);


    void set(OmnigenLogMessageItemModel* content); 
    void toggleTableUpdate(bool allow);
    void clear();
    void copy();
    void populate();
    void resizeTable();

private:
    void updateLogView();

    QWidget* rootElement = nullptr;
    QVBoxLayout* mainLayout = nullptr;
    QHBoxLayout* buttonContainer = nullptr;
    QHBoxLayout* tableContainer = nullptr;
    QTableView* innerContent = nullptr;
    QScrollArea* scrollBarArea = nullptr;

    QTimer ticker;

    OmnigenLogMessageItemModel* logContent;
};