#include "stdafx.h"
#include "OmnigenFieldBase.h"

QWidget* OmnigenFieldBase::createFieldLabelWidget()
{
    return new QLabel(label);
}
