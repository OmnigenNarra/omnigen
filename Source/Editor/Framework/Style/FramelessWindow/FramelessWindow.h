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
#include <QWidget>
#include <QMainWindow>

namespace Ui 
{
    class FramelessWindow;
}

class QFramelessWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit QFramelessWindow(QWidget* parent = Q_NULLPTR, bool hideMinimize = false);
    virtual ~QFramelessWindow() = default;
    void setContent(QWidget* w);
    QWidget* getContent();

    void maximize();

signals:
    void BEGIN_CLOSE();

private:
    bool leftBorderHit(const QPoint& pos);
    bool rightBorderHit(const QPoint& pos);
    bool topBorderHit(const QPoint& pos);
    bool bottomBorderHit(const QPoint& pos);

public slots:
    void setWindowTitle(const QString& text);
    void setWindowIcon(const QIcon& ico);

private slots:
    void on_minimizeButton_clicked();
    void on_restoreButton_clicked();
    void on_maximizeButton_clicked();
    void on_closeButton_clicked();
    void on_windowTitlebar_doubleClicked();
    void on_windowTitlebar_moveStarted();

protected:
    virtual void changeEvent(QEvent* event);
    virtual void mouseDoubleClickEvent(QMouseEvent* event);
    virtual void checkBorderDragging(QMouseEvent* event);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual bool eventFilter(QObject* obj, QEvent* event);

private:
    Ui::FramelessWindow* ui;
    QRect startGeometry;
    bool mousePressed;
    bool bDragTop;
    bool bDragLeft;
    bool bDragRight;
    bool bDragBottom;
};
