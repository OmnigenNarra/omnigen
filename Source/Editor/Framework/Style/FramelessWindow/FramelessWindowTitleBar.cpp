/*
###############################################################################
#                                                                             #
# The MIT License                                                             #
#                                                                             #
# Copyright (C) 2017 by Juergen Skrotzky (JorgenVikingGod@gmail.com)          #
#               >> https://github.com/Jorgen-VikingGod                        #
#                                                                             #
# Sources: https://github.com/Jorgen-VikingGod/Qt-Frameless-Window-OmnigenStyle  #
#                                                                             #
# modified by Jakub 'Ryder' Mrowinski                                         #
#                                                                             #
###############################################################################
*/

#include "stdafx.h"
#include "FramelessWindowTitleBar.h"
#include <QPainter>
#include <QStyleOption>

QFramelessWindowTitleBar::QFramelessWindowTitleBar(QWidget* parent) 
    : QWidget(parent) 
{
    mousePressed = false;
}

void QFramelessWindowTitleBar::mousePressEvent(QMouseEvent* event) 
{
    mousePressed = true;
    mousePos = event->globalPos();

    QWidget* parent = parentWidget();
    if (parent) 
        parent = parent->parentWidget();

    if (parent) 
        wndPos = parent->pos();
}

void QFramelessWindowTitleBar::mouseMoveEvent(QMouseEvent* event) 
{
    QWidget* parent = parentWidget();

    if (parent) 
        parent = parent->parentWidget();

    if (parent && mousePressed)
    {
        emit moveStarted();
        parent->move(wndPos + (event->globalPos() - mousePos));
    }
}

void QFramelessWindowTitleBar::mouseReleaseEvent(QMouseEvent* event) 
{
    Q_UNUSED(event);
    mousePressed = false;
}

void QFramelessWindowTitleBar::paintEvent(QPaintEvent* event) 
{
    Q_UNUSED(event);
    QStyleOption styleOption;
    styleOption.initFrom(parentWidget());

    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &styleOption, &painter, this);
}

void QFramelessWindowTitleBar::mouseDoubleClickEvent(QMouseEvent* event) 
{
    Q_UNUSED(event);
    emit doubleClicked();
}
