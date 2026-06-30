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
#pragma once
#include <QMouseEvent>
#include <QWidget>

class QFramelessWindowTitleBar : public QWidget 
{
    Q_OBJECT

public:
    explicit QFramelessWindowTitleBar(QWidget* parent = Q_NULLPTR);
    virtual ~QFramelessWindowTitleBar() = default;

signals:
    void doubleClicked();
    void moveStarted();

protected:
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void mouseDoubleClickEvent(QMouseEvent* event);
    void paintEvent(QPaintEvent* event);

protected:
    QPoint mousePos;
    QPoint wndPos;
    bool mousePressed;
    bool moveEnabled = true;
};
