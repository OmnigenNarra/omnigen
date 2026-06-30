#include "stdafx.h"
#include "StageProgressConfirmation.h"
#include "ui_FramelessDialog.h"

QStageProgressConfirmation::QStageProgressConfirmation(QWidget* parent)
    : QDialog(parent)
    , framelessDialogUi(new Ui::FramelessDialog)
{
    // Layout
    framelessDialogUi->setupUi(this);
    layout()->setContentsMargins(1, 1, 1, 1);
    setWindowFlags(Qt::FramelessWindowHint);
    framelessDialogUi->windowContent->resize(548, 359);

    exitControls = new QDialogButtonBox(framelessDialogUi->windowContent);
    exitControls->setObjectName(QString::fromUtf8("exitControls"));
    exitControls->setGeometry(QRect(380, 330, 156, 23));
    exitControls->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    QMetaObject::connectSlotsByName(framelessDialogUi->windowContent);
    exitControls->button(QDialogButtonBox::Ok)->setText("Continue");
    exitControls->button(QDialogButtonBox::Cancel)->setText("Revert");

    // Window controls
    framelessDialogUi->titleText->setText("Your changes will affect generated elements on next stages");
    connect(exitControls, &QDialogButtonBox::accepted, this, [this]() { done(1); });
    connect(exitControls, &QDialogButtonBox::rejected, this, [this]() { done(2); });
    //connect(exitControls, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(framelessDialogUi->closeButton, &QPushButton::clicked, this, &QDialog::close);
}