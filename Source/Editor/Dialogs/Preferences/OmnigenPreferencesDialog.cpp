#include "stdafx.h"
#include "OmnigenPreferencesDialog.h"
#include "Editor/Framework/Style/FramelessWindow/FramelessWindowTitleBar.h"
#include <QDialogButtonBox>
#include "Omnigen.h"

#include "ui_OmnigenPreferencesDialog.h"
#include "ui_FramelessDialog.h"

#include <QPushButton>

QOmnigenPreferencesDialog::QOmnigenPreferencesDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::OmnigenPreferencesDialog)
    , framelessDialogUi(new Ui::FramelessDialog)
    , modifiedPreferences(*OmnigenPreferences::get())
{
    // Layout
    framelessDialogUi->setupUi(this);
    layout()->setContentsMargins(1, 1, 1, 1);
    setWindowFlags(Qt::FramelessWindowHint);
    ui->setupUi(framelessDialogUi->windowContent);

    // Window controls
    framelessDialogUi->titleText->setText("Preferences");
    connect(ui->exitControls, &QDialogButtonBox::accepted, this, &QOmnigenPreferencesDialog::saveAndClose);
    connect(ui->exitControls, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(framelessDialogUi->closeButton, &QPushButton::clicked, this, &QDialog::close);

    // Input controls
    //ui->generateTBInput->setCheckState(modifiedPreferences.shouldGenerateTerrainBlocks() ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
    //connect(ui->generateTBInput, &QCheckBox::stateChanged, this, [this](int newState) { modifiedPreferences.setGenerateTerrainBlocks(newState); });
}

void QOmnigenPreferencesDialog::saveAndClose()
{
    *OmnigenPreferences::get() = modifiedPreferences;
    Omnigen::get()->saveConfig();

    close();
}
