#include "stdafx.h"
#include "OmnigenPreferences.h"
#include "Scene/Core/EditorGridDrawable.h"
#include <QSettings>

#define SaveValue(var) settings.setValue(#var, var)
#define LoadValue(var) var = settings.value(#var)

OmnigenPreferences* OmnigenPreferences::get()
{
    static OmnigenPreferences instance;
    return &instance;
}

void OmnigenPreferences::save(QSettings& settings)
{
    //SaveValue(var);
}

void OmnigenPreferences::load(const QSettings& settings)
{
    //LoadValue(var).toBool();
}

#undef SaveValue
#undef LoadValue