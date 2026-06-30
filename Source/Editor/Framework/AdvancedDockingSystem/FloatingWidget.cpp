#include "stdafx.h"
#include "FloatingWidget.h"

#include <QBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QMouseEvent>
#include <QStyle>
#include <QApplication>
#include <QScreen>
#include <QDesktopWidget>

#include "ContainerWidget.h"
#include "SectionTitleWidget.h"
#include "SectionContentWidget.h"
#include "Internal.h"

ADS_NAMESPACE_BEGIN

FloatingWidget::FloatingWidget(ContainerWidget* container, SectionContent::RefPtr sc, SectionTitleWidget* titleWidget, SectionContentWidget* contentWidget, QWidget* parent) :
	QWidget(parent, Qt::FramelessWindowHint | Qt::Tool),
	_container(container),
	_content(sc),
	_titleWidget(titleWidget),
	_contentWidget(contentWidget),
	mousePressed(false),
	bDragTop(false),
	bDragLeft(false),
	bDragRight(false),
	bDragBottom(false)
{
	QBoxLayout* l = new QBoxLayout(QBoxLayout::TopToBottom);
	l->setContentsMargins(2, 2, 2, 2);
	l->setSpacing(0);
	setLayout(l);
	setFocus(Qt::PopupFocusReason);
	setMouseTracking(true);
    setMinimumSize(300, 200);

    QApplication::instance()->installEventFilter(this);

	// Title + Controls
	auto* headerWidget = new QWidget;
	//headerWidget->setStyleSheet(getDockHeaderStyle());
	_titleLayout = new QBoxLayout(QBoxLayout::LeftToRight, headerWidget);
	_titleLayout->setContentsMargins(0, 0, 0, 0);
	_titleLayout->setSpacing(0);
	_titleLayout->addWidget(titleWidget);
	l->addWidget(headerWidget);
	titleWidget->setActiveTab(false);

	if (sc->flags().testFlag(SectionContent::Closeable))
	{
		QPushButton* closeButton = new QPushButton();
		closeButton->setObjectName("closeButton");
		closeButton->setFlat(true);
		closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
		closeButton->setToolTip(tr("Close"));
		closeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		_titleLayout->addWidget(closeButton);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
		QObject::connect(closeButton, &QPushButton::clicked, this, &FloatingWidget::onCloseButtonClicked);
#else
		QObject::connect(closeButton, SIGNAL(clicked(bool)), this, SLOT(onCloseButtonClicked()));
#endif
	}

	// Content
	l->addWidget(contentWidget, 1);
	contentWidget->show();

//	_container->_floatingWidgets.append(this);
}

FloatingWidget::~FloatingWidget()
{
	_container->_floatings.removeAll(this); // Note: I don't like this here, but we have to remove it from list...
}

bool FloatingWidget::takeContent(InternalContentData& data)
{
	data.content = _content;
	data.titleWidget = _titleWidget;
	data.contentWidget = _contentWidget;

	_titleLayout->removeWidget(_titleWidget);
	_titleWidget->setParent(_container);
	_titleWidget = NULL;

	layout()->removeWidget(_contentWidget);
	_contentWidget->setParent(_container);
	_contentWidget = NULL;

	return true;
}

void FloatingWidget::checkBorderDragging(QMouseEvent* event)
{
    QPoint globalMousePos = event->globalPos();
    if (mousePressed)
    {
        QScreen* screen = QGuiApplication::primaryScreen();
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
            int diffh = globalMousePos.y() - (startGeometry.y() + startGeometry.height());
            int newh = startGeometry.height() + diffh;
            int diffw = globalMousePos.x() - (startGeometry.x() + startGeometry.width());
            int neww = startGeometry.width() + diffw;
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

void FloatingWidget::mousePressEvent(QMouseEvent* event)
{
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

void FloatingWidget::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);

    mousePressed = false;
    bool bSwitchBackCursorNeeded = bDragTop || bDragLeft || bDragRight || bDragBottom;
    bDragTop = false;
    bDragLeft = false;
    bDragRight = false;
    bDragBottom = false;

    if (bSwitchBackCursorNeeded)
        setCursor(Qt::ArrowCursor);
}

bool FloatingWidget::eventFilter(QObject* obj, QEvent* event)
{
    // check mouse move event when mouse is moved on any object
    if (event->type() == QEvent::MouseMove)
    {
        setCursor(Qt::ArrowCursor);
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

void FloatingWidget::onCloseButtonClicked()
{
	_container->hideSectionContent(_content);
}


bool FloatingWidget::leftBorderHit(const QPoint& pos)
{
    const QRect& rect = this->geometry();
    if (pos.x() >= rect.x() && pos.x() <= rect.x() + 2)
        return true;

    return false;
}

bool FloatingWidget::rightBorderHit(const QPoint& pos)
{
    const QRect& rect = this->geometry();
    int tmp = rect.x() + rect.width();
    if (pos.x() <= tmp && pos.x() >= (tmp - 2))
        return true;

    return false;
}

bool FloatingWidget::topBorderHit(const QPoint& pos)
{
    const QRect& rect = this->geometry();
    if (pos.y() >= rect.y() && pos.y() <= rect.y() + 1)
        return true;

    return false;
}

bool FloatingWidget::bottomBorderHit(const QPoint& pos)
{
    const QRect& rect = this->geometry();
    int tmp = rect.y() + rect.height();
    if (pos.y() <= tmp && pos.y() >= (tmp - 2))
        return true;

    return false;
}

const QString& getDockHeaderStyle()
{
	static QString style =
		"background: qlineargradient(x1 : 0, y1 : 0, x2 : 0, y2 : 1, stop : 0 #444444, stop: 1 #2a2a2a);"
		"border-radius: 5px;"
		"padding-left: 1ex;"
		"text-align: center;"
		"font-weight: bold; color: #999999;";
		

    return style;
}

ADS_NAMESPACE_END
