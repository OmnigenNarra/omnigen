#include "stdafx.h"
#include "OmnigenOutlineSection.h"
#include "Omnigen.h"
#include "Utils/PlatformMisc.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"

#include <QVBoxLayout>
#include <QMouseEvent>

QOmnigenOutlineSection::QOmnigenOutlineSection(QWidget* parent, Omnigen* omni)
    : QWidget(parent)
    , mainLayout(new QVBoxLayout(this))
{
    resize(INT_MAX, INT_MAX);
    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum));
}

void QOmnigenOutlineSection::fillSection(const std::vector<QWidget*> widgets)
{
    for (auto* widget : widgets)
        mainLayout->addWidget(widget);
}

void QOmnigenOutlineSection::clearSection()
{
    clearLayout(mainLayout);
}

void QOmnigenOutlineSection::applyTreeStyle(QTreeView* treeView)
{
    treeView->setHeaderHidden(true);
    treeView->setStyleSheet("padding: 0ex;");
    treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
}

void OutlineTreeView::mousePressEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::RightButton))
        return;

    QTreeView::mousePressEvent(event);
}

void OutlineTreeView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::RightButton))
    {
        emit customContextMenuRequested(event->pos());
        return;
    }

    QTreeView::mouseReleaseEvent(event);
}