#pragma once
#include <QWidget>

// Adaptor, implements a generic valueChanged()
class QFieldImplBase : public QWidget
{
    Q_OBJECT

signals:
    void valueChanged();
};
