#pragma once
#include <QLineEdit>
#include "FieldImplBase.h"

// LineEdit field, works with any type convertible to/from QString
template<typename T>
class LineEditField : public QFieldImplBase
{
public:
    LineEditField()
        : lineEdit(new QLineEdit)
    {
        setLayout(new QHBoxLayout);
        layout()->addWidget(lineEdit);
        QObject::connect(lineEdit, &QLineEdit::editingFinished, this, &QFieldImplBase::valueChanged);
    }

    void set(const T& newValue)
    {
        lineEdit->setText(toQString(newValue));
    }

    T get()
    {
        return fromQString<T>(lineEdit->text());
    }

protected:
    QLineEdit* lineEdit = nullptr;
};
