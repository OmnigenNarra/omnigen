#pragma once

class QSettings;

// Global settings are stored here
struct OmnigenPreferences
{
    static OmnigenPreferences* get();
    OmnigenPreferences() = default;

    void save(QSettings& settings);
    void load(const QSettings& settings);

private:
};