#include "stdafx.h"
#include "OmniStageBar.h"
#include "Omnigen.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QAction>
#include <QEvent>
#include <QPainter>

#include "Utils/PlatformMisc.h"
#include "Scene/Generation/Stages/StageGeneration.h"
#include "Editor/StageTools/StageTools.h"

QOmniStageBar::QOmniStageBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout();

    setLayout(layout);
    layout->setSpacing(10);

    for (int i = 0; i <= int(EGenerationStage::LastFunctional); i++)
    {
        stageButtons.emplace_back(new QPushButton(QIcon("Resources/Icons/SaveIcon.png"), toQString(static_cast<EGenerationStage>(i))));
        stageButtons[i]->setCheckable(true);
        stageButtons[i]->installEventFilter(this);
        layout->addWidget(stageButtons[i]);

        // Check the generation's first step
        if (i == 0)
            stageButtons[i]->setChecked(true);

        connect(stageButtons[i], &QPushButton::clicked, this, [this, i]()
            { 
                auto currentStage = Generation::Data::get()->getGenerationStage();
                bool process = (int(currentStage) <= i) || isKeyDown(VK_CONTROL);

                if (int(currentStage) > i && isKeyDown(VK_CONTROL))
                {
                    for (int stageIdx = int(EGenerationStage::LastFunctional); stageIdx > int(i); --stageIdx)
                    {
                        EGenerationStageConstexpr::UseIn<EAC::ClearStage>(EGenerationStage(stageIdx));
                        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(stageIdx))->clearNodes();
                    }
                    EGenerationStageConstexpr::UseIn<EAC::InvalidateStage>(EGenerationStage(EGenerationStage(i)));
                }

                Generation::Data::get()->setGenerationStage(static_cast<EGenerationStage>(i), process, true);
            });
    }

    setMouseTracking(true);
    installEventFilter(this);
    show();
}

void QOmniStageBar::updateStageBar()
{
    int currentStage = static_cast<int>(Generation::Data::get()->getGenerationStage());
    for (int i = 0; i < static_cast<int>(EGenerationStage::Last); i++)
    {
        if (i < currentStage)
        {
            stageButtons[i]->setChecked(true);
            stageButtons[i]->setStyleSheet("QPushButton:checked {background-color: green ;}");
        }
        else if (i == currentStage)
        {
            stageButtons[i]->setChecked(true);
            stageButtons[i]->setStyleSheet("QPushButton:checked {background-color: #21AEF8 ;}");
        }
        else
        {
            stageButtons[i]->setChecked(false);
            stageButtons[i]->setStyleSheet("");
        }
    }

    Omnigen::get()->getActions()[EOmnigenAction::ToggleStageBar]->setText(QString("Current Stage: ") + toQString(static_cast<EGenerationStage>(currentStage)));
}

void QOmniStageBar::stageHighlight(int hoverIdx)
{
    int stageIdx = static_cast<int>(Generation::Data::get()->getGenerationStage());

    for (int i = 0; i <= static_cast<int>(EGenerationStage::LastFunctional); ++i)
    {
        if (i <= hoverIdx)
        {
            if (i == hoverIdx && i >= stageIdx)
            {
                // Blue hover current hovered
                reverseDraw = false;
                stageButtons[i]->setStyleSheet("QPushButton { border: 1px solid #DEDEDE; background-color: #21AEF8; }");
            }
            else if (!stageButtons[i]->isChecked() || (hoverIdx > stageIdx && i == stageIdx))
            {
                // Gray highlight all unchecked to hovered
                reverseDraw = false;
                stageButtons[i]->setStyleSheet("QPushButton { border: 1px solid #DEDEDE; background-color: #F4D03F; color: #808080}");
            }
            else if (stageIdx > i )
            {
                if (i == hoverIdx)
                {
                    // Red highlight the current hovered (if reverting)
                    reverseDraw = true;
                    stageButtons[i]->setStyleSheet("QPushButton { border: 1px solid #DEDEDE; background-color: red; }");
                }
                else
                    // Green highlight checked stages before hovered
                    stageButtons[i]->setStyleSheet("QPushButton:checked {background-color: green ;}");
            }
        }
        else
        {
            if ((stageIdx >= i))
            {
                // Red highlight all between current stage and hovered
                reverseDraw = true;
                stageButtons[i]->setStyleSheet("QPushButton { border: 1px solid #DEDEDE; background-color: red; }");
            }
            else
                // Set default colour for all buttons higher than hovered and current stage
                stageButtons[i]->setStyleSheet("");
        }
    }

    repaint();
}

void QOmniStageBar::endHighlight()
{
    drawArrow = false;
    updateStageBar();
    repaint();
}

bool QOmniStageBar::eventFilter(QObject* obj, QEvent* event)
{
    if (Omnigen::get()->isGenerating())
        return QWidget::eventFilter(obj, event);

    int stageIdx = static_cast<int>(Generation::Data::get()->getGenerationStage());

    if (contains(stageButtons, obj))
    {
        if (event->type() == QEvent::Enter)
        {
            hoverIdx = indexOf(stageButtons, obj);
            drawArrow = true;
            stageHighlight(indexOf(stageButtons, obj));
            repaint();
        }
        else if (event->type() == QEvent::MouseButtonPress)
            endHighlight();

        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseMove)
    {
        QPoint mPos = this->mapFromGlobal(QCursor::pos());
        drawArrow = true;

        // Check if button hovered has been changed
         if(hoverIdx == static_cast<int>(EGenerationStage::LastFunctional) && stageButtons[hoverIdx]->pos().x() <= mPos.x())
             return QWidget::eventFilter(obj, event);
         else if (stageButtons[hoverIdx]->pos().x() <= mPos.x() && mPos.x() < stageButtons[hoverIdx + 1]->pos().x())
             return QWidget::eventFilter(obj, event);

        // Find the button index according to mouse pos 
        for (int i = 0; i < static_cast<int>(EGenerationStage::LastFunctional); ++i)
        {
            if (stageButtons[i]->pos().x() <= mPos.x() && mPos.x() < stageButtons[i + 1]->pos().x())
            {
                hoverIdx = i;
                break;
            }
            else if (i == (static_cast<int>(EGenerationStage::LastFunctional) - 1))
                hoverIdx = static_cast<int>(EGenerationStage::LastFunctional);
        }

        stageHighlight(hoverIdx);
    }
    else if (event->type() == QEvent::Leave)
        endHighlight();

    return QWidget::eventFilter(obj, event);
}

void QOmniStageBar::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    if (!(drawArrow && static_cast<int>(Generation::Data::get()->getGenerationStage()) != hoverIdx))
        return;

    auto arrowTail = stageButtons[static_cast<int>(Generation::Data::get()->getGenerationStage())]->pos();
    auto tailButtonWidth = stageButtons[static_cast<int>(Generation::Data::get()->getGenerationStage())]->width();
    auto arrowButtonWidth = stageButtons[hoverIdx]->width();
    auto buttonHeight = stageButtons[hoverIdx]->height();

    QPoint arrowhead = {(stageButtons[hoverIdx]->pos().x()), stageButtons[hoverIdx]->pos().y()};

    QPoint arrow[7] = {
        QPoint((arrowTail.x()), arrowTail.y() + 1),
        QPoint((arrowTail.x() + 1), arrowTail.y()),
        QPoint((arrowhead.x() - 10), arrowTail.y()),
        QPoint(arrowhead.x(), (arrowhead.y() + (buttonHeight / 2) - 1)),
        QPoint((arrowhead.x() - 10 ), (arrowTail.y() + buttonHeight - 1)),
        QPoint((arrowTail.x()) + 1, (arrowTail.y() + buttonHeight - 1)),
        QPoint((arrowTail.x()), (arrowTail.y() + buttonHeight - 2))
    };

    QPoint reverseArrow[7] = {
        QPoint((arrowTail.x() + tailButtonWidth), arrowTail.y() + 1),
        QPoint((arrowTail.x() + tailButtonWidth - 1), arrowTail.y()),
        QPoint(arrowhead.x(), arrowTail.y()),
        QPoint((arrowhead.x() - 10), (arrowhead.y() + (buttonHeight / 2))),
        QPoint(arrowhead.x(), (arrowTail.y() + buttonHeight - 1)),
        QPoint((arrowTail.x() + tailButtonWidth - 1), (arrowTail.y() + buttonHeight - 1)),
        QPoint((arrowTail.x() + tailButtonWidth), (arrowTail.y() + buttonHeight - 2))
    };

    QPainter painter(this);
    painter.setPen(QPen(QColor("#DEDEDE"), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(reverseDraw ? Qt::red : QColor("#F4D03F"));
    painter.drawConvexPolygon(reverseDraw ? reverseArrow : arrow, 7);
    stageButtons[0]->pos();
}
