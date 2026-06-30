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
#include "FramelessWindow.h"
#include <QApplication>
#include <QDesktopWidget>
#include <QGraphicsDropShadowEffect>
#include <QScreen>
#include <QWindow>

#include "ui_framelesswindow.h"

const quint8 CONST_DRAG_BORDER_SIZE = 15;

QFramelessWindow::QFramelessWindow(QWidget* parent, bool hideMinimize)
    : QMainWindow(parent),
    ui(new Ui::FramelessWindow),
    mousePressed(false),
    bDragTop(false),
    bDragLeft(false),
    bDragRight(false),
    bDragBottom(false) 
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    // append minimize button flag in case of windows,
    // for correct windows native handling of minimize function
#if defined(Q_OS_WIN)
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);
#endif
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);

    ui->setupUi(this);
    ui->restoreButton->setVisible(false);

    connect(ui->windowTitlebar, SIGNAL(moveStarted()), this, SLOT(on_windowTitlebar_moveStarted()));

    // Default style
    static const QString titleBarStyle = QStringLiteral(
        "#windowTitlebar{border: 0px none palette(dark); "
        "border-top-left-radius:0px; border-top-right-radius:0px; "
        "background-color:palette(dark); height:20px;}"
    );

    static const QString windowStyle = QStringLiteral(
        "#windowFrame{border:1px solid palette(dark); border-radius:0px 0px 0px 0px;" 
        "background-color:palette(Window);}"
    );

    layout()->setMargin(0);
//     ui->windowTitlebar->setStyleSheet(titleBarStyle);
//     ui->windowFrame->setStyleSheet(windowStyle);

    // shadow under window title text
    QGraphicsDropShadowEffect* textShadow = new QGraphicsDropShadowEffect;
    textShadow->setBlurRadius(4.0);
    textShadow->setColor(QColor(0, 0, 0));
    textShadow->setOffset(0.0);
    //ui->titleText->setGraphicsEffect(textShadow);

    setMouseTracking(true);

    // important to watch mouse move from all child widgets
    QApplication::instance()->installEventFilter(this);

    setCentralWidget(ui->windowFrame);

    if (hideMinimize)
        ui->minimizeButton->close();
}

void QFramelessWindow::on_restoreButton_clicked()
{
    ui->restoreButton->setVisible(false);
    ui->maximizeButton->setVisible(true);

    static const QString windowStyle =
        "#windowFrame{ border:1px solid palette(highlight);"
        "border-radius:5px 5px 5px 5px;"
        "background-color:palette(Window); }";

    //ui->windowFrame->setStyleSheet(windowStyle);
    setWindowState(Qt::WindowNoState);

    // on MacOS this hack makes sure the
    // background window is repaint correctly
  //   hide();
  //   show();
}

void QFramelessWindow::on_maximizeButton_clicked() 
{
    maximize();
}

void QFramelessWindow::changeEvent(QEvent* event) 
{
    if (event->type() == QEvent::WindowStateChange) 
    {
        if (windowState().testFlag(Qt::WindowNoState)) 
        {
            ui->restoreButton->setVisible(false);
            ui->maximizeButton->setVisible(true);
            event->ignore();
        }
        else if (windowState().testFlag(Qt::WindowMaximized)) 
        {
            ui->restoreButton->setVisible(true);
            ui->maximizeButton->setVisible(false);
            event->ignore();
        }
    }
    event->accept();
}

void QFramelessWindow::setContent(QWidget* w) 
{
    ui->windowContent->layout()->addWidget(w);
}

QWidget* QFramelessWindow::getContent()
{
    return ui->windowContent;
}

void QFramelessWindow::maximize()
{
    ui->restoreButton->setVisible(true);
    ui->maximizeButton->setVisible(false);

    static const QString fsStyle = "#windowFrame{background-color:palette(Window);}";
    ui->windowFrame->setStyleSheet(fsStyle);

    this->setWindowState(Qt::WindowMaximized);
    this->showMaximized();

    setCursor(Qt::ArrowCursor);
}

void QFramelessWindow::setWindowTitle(const QString& text) 
{
    ui->titleText->setText(text);
}

void QFramelessWindow::setWindowIcon(const QIcon& ico) 
{
    ui->icon->setPixmap(ico.pixmap(16, 16));
}

void QFramelessWindow::on_minimizeButton_clicked() 
{
    setWindowState(Qt::WindowMinimized);
}

void QFramelessWindow::on_closeButton_clicked() 
{ 
    emit BEGIN_CLOSE();
    close(); 
}

void QFramelessWindow::on_windowTitlebar_doubleClicked() 
{
    if (windowState().testFlag(Qt::WindowNoState)) 
    {
        on_maximizeButton_clicked();
    }
    else if (windowState().testFlag(Qt::WindowMaximized)) 
    {
        on_restoreButton_clicked();
    }
}

void QFramelessWindow::on_windowTitlebar_moveStarted()
{
    auto state = windowState();
    if (!windowState().testFlag(Qt::WindowMaximized))
        return;
    
    // While attempting to drag a maximized window, restore it first and make it fill the screen 'manually'.
    on_restoreButton_clicked();

    std::vector<QPoint> windowPoints =
    {
        windowHandle()->position() + QPoint(windowHandle()->size().width() / 2, windowHandle()->size().height() / 2), // center
        windowHandle()->position() + QPoint(windowHandle()->size().width() / 2, 0), // top center
        windowHandle()->position() + QPoint(windowHandle()->size().width() / 2, windowHandle()->size().height()), // bot center
        windowHandle()->position() + QPoint(0, windowHandle()->size().height() / 2), // left center
        windowHandle()->position() + QPoint(windowHandle()->size().width(), windowHandle()->size().height() / 2) // right center
    };

    for (int i = 0; i < windowPoints.size(); ++i)
    {
        if (auto* screen = QGuiApplication::screenAt(windowPoints[i]))
        {
            windowHandle()->setPosition(screen->availableGeometry().topLeft());
            windowHandle()->resize(screen->availableGeometry().size());
            break;
        }
    }
}

void QFramelessWindow::mouseDoubleClickEvent(QMouseEvent* event) 
{
    Q_UNUSED(event);
}

void QFramelessWindow::checkBorderDragging(QMouseEvent* event) 
{
    if (isMaximized())
        return;

    QPoint globalMousePos = event->globalPos();
    if (mousePressed) 
    {
        QScreen* screen = QGuiApplication::primaryScreen();
        // available geometry excludes taskbar
        QRect availGeometry = screen->availableGeometry();
        int h = availGeometry.height();
        int w = availGeometry.width();
        QList<QScreen*> screenlist = screen->virtualSiblings();
        if (screenlist.contains(screen)) 
        {
            QSize sz = QApplication::desktop()->size();
            h = sz.height();
            w = sz.width();
        }

        // top right corner
        if (bDragTop && bDragRight) 
        {
            int diffw = globalMousePos.x() - (startGeometry.x() + startGeometry.width());
            int neww = startGeometry.width() + diffw;
            int diffy = globalMousePos.y() - startGeometry.y();
            int newy = startGeometry.y() + diffy;
            if (neww > 0 && newy > 0 && newy < h - 50) 
            {
                QRect newg = startGeometry;
                newg.setWidth(neww);
                newg.setX(startGeometry.x());

                // Prevents movement after reaching minimum geometry
                if ((startGeometry.height() - diffy) >= this->minimumHeight())
                    newg.setY(newy);
                else
                    newg.setY(startGeometry.y() + startGeometry.height() - this->minimumHeight());

                setGeometry(newg);
            }
        }
        // top left corner
        else if (bDragTop && bDragLeft) 
        {
            int diffy = globalMousePos.y() - startGeometry.y();
            int newy = startGeometry.y() + diffy;
            int diffx = globalMousePos.x() - startGeometry.x();
            int newx = startGeometry.x() + diffx;
            if (newy > 0 && newx > 0) 
            {
                QRect newg = startGeometry;

                // Prevents movement after reaching minimum geometry
                if ((startGeometry.height() - diffy) >= this->minimumHeight())
                    newg.setY(newy);
                else
                    newg.setY(startGeometry.y() + startGeometry.height() - this->minimumHeight());

                if ((startGeometry.width() - diffx) >= this->minimumWidth())
                    newg.setX(newx);
                else
                    newg.setX(startGeometry.x() + startGeometry.width() - this->minimumWidth());

                setGeometry(newg);
            }
        }
        // bottom left corner
        else if (bDragBottom && bDragLeft) 
        {
            int diffh = globalMousePos.y() - (startGeometry.y() + startGeometry.height());
            int newh = startGeometry.height() + diffh;
            int diffx = globalMousePos.x() - startGeometry.x();
            int newx = startGeometry.x() + diffx;
            if (newh > 0 && newx > 0) 
            {
                QRect newg = startGeometry;

                // Prevents movement after reaching minimum geometry
                if ((startGeometry.width() - diffx) >= this->minimumWidth())
                    newg.setX(newx);
                else
                    newg.setX(startGeometry.x() + startGeometry.width() - this->minimumWidth());

                newg.setHeight(newh);
                setGeometry(newg);
            }
        }
        // bottom right corner
        else if (bDragBottom && bDragRight)
        {
            int diff = globalMousePos.y() - (startGeometry.y() + startGeometry.height());
            int newh = startGeometry.height() + diff;
            diff = globalMousePos.x() - (startGeometry.x() + startGeometry.width());
            int neww = startGeometry.width() + diff;
            if (newh > 0 && neww > 0)
            {
                QRect newg = startGeometry;
                newg.setWidth(neww);
                newg.setHeight(newh);
                setGeometry(newg);
            }
        }
        else if (bDragTop) 
        {
            int diff = globalMousePos.y() - startGeometry.y();
            int newy = startGeometry.y() + diff;
            if (newy > 0 && newy < h - 50 && (startGeometry.height() - diff) > this->minimumHeight())
            {
                QRect newg = startGeometry;
                newg.setY(newy);
                setGeometry(newg);
            }
        }
        else if (bDragLeft) 
        {
            int diff = globalMousePos.x() - startGeometry.x();
            int newx = startGeometry.x() + diff;
            if (newx > 0 && newx < w - 50 && (startGeometry.width() - diff) > this->minimumWidth())
            {
                QRect newg = startGeometry;
                newg.setX(newx);
                setGeometry(newg);
            }
        }
        else if (bDragRight) 
        {
            int diff = globalMousePos.x() - (startGeometry.x() + startGeometry.width());
            int neww = startGeometry.width() + diff;
            if (neww > 0) 
            {
                QRect newg = startGeometry;
                newg.setWidth(neww);
                newg.setX(startGeometry.x());
                setGeometry(newg);
            }
        }
        else if (bDragBottom) 
        {
            int diff = globalMousePos.y() - (startGeometry.y() + startGeometry.height());
            int newh = startGeometry.height() + diff;
            if (newh > 0) 
            {
                QRect newg = startGeometry;
                newg.setHeight(newh);
                newg.setY(startGeometry.y());
                setGeometry(newg);
            }
        }
    }
    else {
        // no mouse pressed
        if (leftBorderHit(globalMousePos) && topBorderHit(globalMousePos)) 
        {
            setCursor(Qt::SizeFDiagCursor);
        }
        else if (rightBorderHit(globalMousePos) && topBorderHit(globalMousePos)) 
        {
            setCursor(Qt::SizeBDiagCursor);
        }
        else if (leftBorderHit(globalMousePos) && bottomBorderHit(globalMousePos)) 
        {
            setCursor(Qt::SizeBDiagCursor);
        }
        else if (rightBorderHit(globalMousePos) && bottomBorderHit(globalMousePos))
        {
            setCursor(Qt::SizeFDiagCursor);
        }
        else 
        {
            if (topBorderHit(globalMousePos)) 
            {
                setCursor(Qt::SizeVerCursor);
            }
            else if (leftBorderHit(globalMousePos)) 
            {
                setCursor(Qt::SizeHorCursor);
            }
            else if (rightBorderHit(globalMousePos)) 
            {
                setCursor(Qt::SizeHorCursor);
            }
            else if (bottomBorderHit(globalMousePos)) 
            {
                setCursor(Qt::SizeVerCursor);
            }
            else 
            {
                bDragTop = false;
                bDragLeft = false;
                bDragRight = false;
                bDragBottom = false;
                setCursor(Qt::ArrowCursor);
            }
        }
    }
}

// pos in global virtual desktop coordinates
bool QFramelessWindow::leftBorderHit(const QPoint& pos) 
{
    const QRect& rect = this->geometry();
    if (pos.x() >= rect.x() && pos.x() <= rect.x() + CONST_DRAG_BORDER_SIZE) 
        return true;
    
    return false;
}

bool QFramelessWindow::rightBorderHit(const QPoint& pos) 
{
    const QRect& rect = this->geometry();
    int tmp = rect.x() + rect.width();
    if (pos.x() <= tmp && pos.x() >= (tmp - CONST_DRAG_BORDER_SIZE))
        return true;

    return false;
}

bool QFramelessWindow::topBorderHit(const QPoint& pos)
{
    const QRect& rect = this->geometry();
    if (pos.y() >= rect.y() && pos.y() <= rect.y() + CONST_DRAG_BORDER_SIZE)
        return true;

    return false;
}

bool QFramelessWindow::bottomBorderHit(const QPoint& pos)
{
    const QRect& rect = this->geometry();
    int tmp = rect.y() + rect.height();
    if (pos.y() <= tmp && pos.y() >= (tmp - CONST_DRAG_BORDER_SIZE))
        return true;

    return false;
}

void QFramelessWindow::mousePressEvent(QMouseEvent* event) 
{
    if (isMaximized())
        return;

    mousePressed = true;
    startGeometry = this->geometry();

    QPoint globalMousePos = mapToGlobal(QPoint(event->x(), event->y()));

    if (leftBorderHit(globalMousePos) && topBorderHit(globalMousePos)) 
    {
        bDragTop = true;
        bDragLeft = true;
        setCursor(Qt::SizeFDiagCursor);
    }
    else if (rightBorderHit(globalMousePos) && topBorderHit(globalMousePos)) 
    {
        bDragRight = true;
        bDragTop = true;
        setCursor(Qt::SizeBDiagCursor);
    }
    else if (leftBorderHit(globalMousePos) && bottomBorderHit(globalMousePos)) 
    {
        bDragLeft = true;
        bDragBottom = true;
        setCursor(Qt::SizeBDiagCursor);
    }
    else if (rightBorderHit(globalMousePos) && bottomBorderHit(globalMousePos))
    {
        bDragRight = true;
        bDragBottom = true;
        setCursor(Qt::SizeFDiagCursor);
    }
    else 
    {
        if (topBorderHit(globalMousePos)) 
        {
            bDragTop = true;
            setCursor(Qt::SizeVerCursor);
        }
        else if (leftBorderHit(globalMousePos)) 
        {
            bDragLeft = true;
            setCursor(Qt::SizeHorCursor);
        }
        else if (rightBorderHit(globalMousePos)) 
        {
            bDragRight = true;
            setCursor(Qt::SizeHorCursor);
        }
        else if (bottomBorderHit(globalMousePos)) 
        {
            bDragBottom = true;
            setCursor(Qt::SizeVerCursor);
        }
    }
}

void QFramelessWindow::mouseReleaseEvent(QMouseEvent* event) 
{
    Q_UNUSED(event);
    if (isMaximized())
        return;

    mousePressed = false;
    bool bSwitchBackCursorNeeded = bDragTop || bDragLeft || bDragRight || bDragBottom;
    bDragTop = false;
    bDragLeft = false;
    bDragRight = false;
    bDragBottom = false;

    if (bSwitchBackCursorNeeded)
        setCursor(Qt::ArrowCursor);
}

bool QFramelessWindow::eventFilter(QObject* obj, QEvent* event) 
{
    if (isMaximized())
        return QWidget::eventFilter(obj, event);

    // check mouse move event when mouse is moved on any object
    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent* pMouse = dynamic_cast<QMouseEvent*>(event);
        if (pMouse)
            checkBorderDragging(pMouse);
    }
    // press is triggered only on frame window
    else if (event->type() == QEvent::MouseButtonPress && obj == this) 
    {
        QMouseEvent* pMouse = dynamic_cast<QMouseEvent*>(event);
        if (pMouse)
            mousePressEvent(pMouse);
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        if (mousePressed)
        {
            QMouseEvent* pMouse = dynamic_cast<QMouseEvent*>(event);
            if (pMouse)
                mouseReleaseEvent(pMouse);
        }
    }

    return QWidget::eventFilter(obj, event);
}
