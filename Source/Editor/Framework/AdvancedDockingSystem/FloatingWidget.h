#ifndef FLOATINGWIDGET_H
#define FLOATINGWIDGET_H

#include <QWidget>
class QBoxLayout;

#include "API.h"
#include "SectionContent.h"

ADS_NAMESPACE_BEGIN
class ContainerWidget;
class SectionTitleWidget;
class SectionContentWidget;
class InternalContentData;

// FloatingWidget holds and displays SectionContent as a floating window.
// It can be resized, moved and dropped back into a SectionWidget.
class FloatingWidget : public QWidget
{
	Q_OBJECT

	friend class ContainerWidget;

public:
	FloatingWidget(ContainerWidget* container, SectionContent::RefPtr sc, SectionTitleWidget* titleWidget, SectionContentWidget* contentWidget, QWidget* parent = NULL);
	virtual ~FloatingWidget();

	SectionContent::RefPtr content() const { return _content; }

public://private:
	bool takeContent(InternalContentData& data);

protected:
	// Resize
	virtual void checkBorderDragging(QMouseEvent* event);
	virtual void mousePressEvent(QMouseEvent* event);
	virtual void mouseReleaseEvent(QMouseEvent* event);
	virtual bool eventFilter(QObject* obj, QEvent* event);

private slots:
	void onCloseButtonClicked();

private:
	bool leftBorderHit(const QPoint& pos);
	bool rightBorderHit(const QPoint& pos);
	bool topBorderHit(const QPoint& pos);
	bool bottomBorderHit(const QPoint& pos);

	// Resize
	QRect startGeometry;
	bool mousePressed;
	bool bDragTop;
	bool bDragLeft;
	bool bDragRight;
	bool bDragBottom;

	ContainerWidget* _container;
	SectionContent::RefPtr _content;
	SectionTitleWidget* _titleWidget;
	SectionContentWidget* _contentWidget;

	QBoxLayout* _titleLayout;
};

const QString& getDockHeaderStyle();

ADS_NAMESPACE_END
#endif