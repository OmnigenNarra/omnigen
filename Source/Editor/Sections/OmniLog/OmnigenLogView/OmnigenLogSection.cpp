#include "stdafx.h"
#include "OmnigenLogSection.h"
#include "Omnigen.h"
#include "OmnigenLogMessageItemModel.h"
#include <QTableView>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QClipboard>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>

OmnigenLogSection::OmnigenLogSection(QWidget* parent, OmnigenLogMessageItemModel* model) : QWidget(parent)
{
    setLayout(new QVBoxLayout);
    rootElement = new QWidget(this);
    layout()->addWidget(rootElement);
    layout()->setSizeConstraint(QLayout::SizeConstraint::SetNoConstraint);
    layout()->setContentsMargins(0, 0, 0, 0);
    rootElement->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    rootElement->setMaximumWidth(1600);

    mainLayout = new QVBoxLayout(rootElement);
    mainLayout->setSizeConstraint(QLayout::SizeConstraint::SetNoConstraint);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    innerContent = new QTableView(rootElement);
    innerContent->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    innerContent->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);    //IMPORTANT If logger starts crashing with qvector index out of range exception then try commenting this line and uncommenting the line below. Somehow when resizing model tries accessing non-existing data. Turning row resizing off might provide a temporary fix
    //innerContent->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeMode::Fixed);    
    innerContent->verticalHeader()->setDefaultSectionSize(12);
    innerContent->verticalHeader()->hide();
    innerContent->setShowGrid(false);

    tableContainer = new QHBoxLayout(rootElement);
    tableContainer->setSizeConstraint(QLayout::SizeConstraint::SetNoConstraint);

    buttonContainer = new QHBoxLayout(rootElement);
    buttonContainer->setSizeConstraint(QLayout::SizeConstraint::SetNoConstraint);
    
    scrollBarArea = new QScrollArea(rootElement);
    scrollBarArea->setSizeAdjustPolicy(QAbstractScrollArea::SizeAdjustPolicy::AdjustToContentsOnFirstShow);
    scrollBarArea->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    scrollBarArea->setWidget(innerContent);
    scrollBarArea->setWidgetResizable(true);
    
    tableContainer->addWidget(scrollBarArea);

    mainLayout->addLayout(tableContainer);
    mainLayout->addLayout(buttonContainer);

    auto* logButtonX = new QPushButton("Copy");
    auto* logButtonY = new QPushButton("Clear");
    buttonContainer->addWidget(logButtonX);
    buttonContainer->addSpacerItem(new QSpacerItem(25, 5));
    buttonContainer->addWidget(logButtonY);

    connect(logButtonX, &QPushButton::clicked, this, &OmnigenLogSection::copy);
    connect(logButtonY, &QPushButton::clicked, this, &OmnigenLogSection::clear);

    logContent = model;
    innerContent->setModel(logContent);
    innerContent->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::ResizeToContents);
    innerContent->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeMode::ResizeToContents);
    innerContent->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);
    innerContent->horizontalHeader()->setStretchLastSection(true);
    innerContent->horizontalHeader()->setDefaultAlignment(Qt::Alignment::enum_type::AlignLeft | Qt::Alignment::enum_type::AlignBaseline);
    innerContent->setHorizontalScrollMode(QAbstractItemView::ScrollMode::ScrollPerPixel);
    resizeTable();
    innerContent->setTextElideMode(Qt::TextElideMode::ElideNone);   //elision set off (achtung, word wrap turns it on)
    innerContent->setWordWrap(false);   //must be false to avoid elision

    connect(logContent, &OmnigenLogMessageItemModel::rowsInserted, this, [this]()
        {
            QTimer::singleShot(0, [this]() 
            {
                // Move the section slider to the lowest position if new messages were added
                innerContent->verticalScrollBar()->setValue(innerContent->verticalScrollBar()->maximum());
            });
        });

    // Get log messages from buffer every 100ms (if applicable)
    connect(&ticker, &QTimer::timeout, this, [this]()
        {
            logContent->insertRowsFromBuffer();
        });

    ticker.start(100);
}

void OmnigenLogSection::set(OmnigenLogMessageItemModel* content)
{
  logContent = content;
  updateLogView();
}

void OmnigenLogSection::toggleTableUpdate(bool allow)
{
    innerContent->setUpdatesEnabled(allow);
}

void OmnigenLogSection::clear()
{
    logContent->clear();
    updateLogView();
}

void OmnigenLogSection::copy()
{
    QString content = logContent->getContentString();
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(content);
}

void OmnigenLogSection::populate()
{
    clear();

    updateLogView();
    innerContent->scrollToBottom();

    resizeTable();
}

void OmnigenLogSection::resizeTable()
{
    innerContent->resizeColumnToContents(0);
    innerContent->resizeColumnToContents(1);
    innerContent->resizeColumnToContents(2);
    if (innerContent->columnWidth(2) > 1024)
    {
        innerContent->horizontalHeader()->setStretchLastSection(false); //turn off stretching if column becomes too long - stretching invalidates scrolling
        innerContent->resizeColumnToContents(2);
    }
    else
    {
        innerContent->horizontalHeader()->setStretchLastSection(true);  //turn stretching back on (in case a long cell has been removed
    }
       
}

void OmnigenLogSection::updateLogView()
{
    innerContent->viewport()->update();
}

