#pragma once
#include <QDialog>
#include "OmnigenPreferences.h"

namespace Ui 
{
    class OmnigenPreferencesDialog;
    class FramelessDialog;
}

// Interface for setting preferences
class QOmnigenPreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QOmnigenPreferencesDialog(QWidget* parent = nullptr);

private:
    void saveAndClose();

    Ui::OmnigenPreferencesDialog* ui;
    Ui::FramelessDialog* framelessDialogUi;

    OmnigenPreferences modifiedPreferences;
};
