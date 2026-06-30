#pragma once
#include <QProgressDialog>

#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

class QOmnigenProgressDialogBase : public QProgressDialog
{
    Q_OBJECT

public:
    QOmnigenProgressDialogBase(const QString& label, quint64 tw);
    ~QOmnigenProgressDialogBase() noexcept = default;

    void addProgress(quint64 workChunk);

    mutable bool wasCanceled = false;

public slots:
    void cancel();

protected:

    tbb::task_group taskGroup;

    mutable double progress = 0.0;
    mutable std::mutex progressMutex;
    mutable quint64 workDone = 0;

    const quint64 totalWork;
};

template<typename L>
class QOmnigenProgressDialog : public QOmnigenProgressDialogBase
{
public:
    QOmnigenProgressDialog(const QString& label, quint64 tw, const L& l)
        : QOmnigenProgressDialogBase(label, tw)
        , lambda(l)
    {
    }

    bool executeProgressDialog()
    {
        taskGroup.run([&] { lambda(this); });
        setValue(0);
        exec();

        bool result = !wasCanceled;
        taskGroup.wait();
        return result;
    }

protected:
    L lambda;
};
