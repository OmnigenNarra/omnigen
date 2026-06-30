#pragma once
#include "Utils/CoreUtils.h"
#define QT_USE_QSTRINGBUILDER
#include <QFile>
#include <QTextStream>

// Something about this log is extremely slow.
// Probably the view

#define OmniLog OmniLogger::get

class OmnigenLogMessageItemModel;

enum class ELoggingLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

struct OmniLoggerMesasgeBundle
{
    QString timeStamp;
    ELoggingLevel level;
    QString message;
    int id;
};

class OmniLogger
{
private:
    static OmniLogger* instance;
    ELoggingLevel loggingLevel;
    bool isOn_loggingOutput[3]; // 0 - visual studio debug output 1 - file 2 - logWindow
    std::vector<QString> messageBuffer;

    QString logFileName = "log";
    const QString logFilePath = "Output/logs/";
    QFile logFile;
    QTextStream fileOutputStream;

    int messageId = -1;

    OmnigenLogMessageItemModel* programOutputModel;
    bool programOutputModelInitialized = false;

    OmniLogger();
    ~OmniLogger();

    void initFileOutput();

    void writeToIdeOutput(const QString& fullMsg, ELoggingLevel level);
    void writeToFileOutput(const QString& fullMsg);
    void writeToProgramOutput(const OmniLoggerMesasgeBundle& messageBundle);

    ELoggingLevel getLoggingLevel();
    QString getCurrentTimestamp();
    OmniLoggerMesasgeBundle createMessageBundle(const QString& message, ELoggingLevel lvl);

public:
    static OmniLogger& get(ELoggingLevel ll = ELoggingLevel::Debug, int logId = -1);
    void initProgramOutput(OmnigenLogMessageItemModel* olmim);
    
    void log(QString&& message, bool flush = true);

    template<typename T>
    friend OmniLogger& operator<<(OmniLogger&, T&&);

    friend OmniLogger& operator<<(OmniLogger&, const char*);
    friend OmniLogger& operator<<(OmniLogger&, std::string_view&&);

    template<typename T>
    friend OmniLogger& operator<<=(OmniLogger&, T&&);

    friend OmniLogger& operator<<=(OmniLogger&, const char*);
    friend OmniLogger& operator<<=(OmniLogger&, std::string_view&&);
};

template<typename T>
inline OmniLogger& operator<<(OmniLogger& tl, T&& message)
{
    tl.log(toQString(message), false);
    return (*tl.instance);
}

inline OmniLogger& operator<<(OmniLogger& tl, const char* message)
{
    tl.log(message, false);
    return (*tl.instance);
};

inline OmniLogger& operator<<(OmniLogger& tl, std::string_view&& message)
{
    tl.log(QString::fromStdString(message.data()), false);
    return (*tl.instance);
}

template<typename T>
inline OmniLogger& operator<<=(OmniLogger& tl, T&& message)
{
    tl.log(toQString(message), true);
    return (*tl.instance);
}

inline OmniLogger& operator<<=(OmniLogger& tl, const char* message)
{
    tl.log(message, true);
    return (*tl.instance);
};

inline OmniLogger& operator<<=(OmniLogger& tl, std::string_view&& message)
{
    tl.log(QString::fromStdString(message.data()), true);
    return (*tl.instance);
}