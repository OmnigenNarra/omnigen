#include "stdafx.h"
#include "OmnigenProgressDialog.h"

#include <mutex>

QOmnigenProgressDialogBase::QOmnigenProgressDialogBase(const QString& label, quint64 tw)
    : QProgressDialog(label, "Abort Generation", 0, 100, nullptr)
    , totalWork(tw)
{
    setWindowModality(Qt::WindowModal);
    setMinimumDuration(500);

    connect(this, &QProgressDialog::canceled, this, &QOmnigenProgressDialogBase::cancel);
}

void QOmnigenProgressDialogBase::addProgress(quint64 workChunk)
{
    std::scoped_lock lock(progressMutex);
    workDone += workChunk;

    if (workDone < totalWork)
        QTimer::singleShot(0, Qt::TimerType::VeryCoarseTimer, this, [this]()
            {
                setValue(int(floor(100.0 * double(workDone) / double(totalWork)))); 
            });
    else
        QTimer::singleShot(0, Qt::TimerType::VeryCoarseTimer, this, [this]()
            {
                setValue(100);
            });
}

void QOmnigenProgressDialogBase::cancel()
{
    wasCanceled = true;
    taskGroup.cancel();
}
