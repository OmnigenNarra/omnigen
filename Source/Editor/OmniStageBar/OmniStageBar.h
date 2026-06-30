#pragma once
#include "Omnigen.h"

// Top bar presenting current generation stage info.
// Provides interface to navigate through the stages.
// LMB stage to the right of the current: Generate up to that stage including it.
// LMB current stage: Regenerate current stage.
// LMB stage to the left of the current: Clear all stages to the right of the target (does not clear target stage).
class QOmniStageBar : public QWidget
{
    Q_OBJECT

public:
    QOmniStageBar(QWidget* parent);
    void updateStageBar();

private:
    QWidget* stageBar = nullptr;
    std::vector<QPushButton*> stageButtons;

    void stageHighlight(int hoverIdx);
    void endHighlight();
    bool eventFilter(QObject* obj, QEvent* event);
    void paintEvent(QPaintEvent* event) override;

    bool drawArrow = false;
    bool reverseDraw = false;
    int hoverIdx = 0;
};