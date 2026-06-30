#pragma once

namespace Ui
{
    class QStageProgressConfirmation;
    class FramelessDialog;
}

class QStageProgressConfirmation : public QDialog
{
    Q_OBJECT

public:
    explicit QStageProgressConfirmation(QWidget* parent = nullptr);

private:
    Ui::FramelessDialog* framelessDialogUi;

    QDialogButtonBox* exitControls;
};
