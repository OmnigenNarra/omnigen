#include "stdafx.h"
#include "OmniLogger.h"
#include "OmnigenLogView/OmnigenLogMessageItemModel.h"
#include "OmnigenLogView/OmnigenLogSection.h"
#include "Utils/CoreUtils.h"

#include <QDebug>
#include <QDir>
#include <QTextCodec>
#include <QMessageLogger>

#include <string>
#include <string_view>
#include <qstring.h>
#include <qstringbuilder.h>

OmniLogger* OmniLogger::instance;

OmniLogger::OmniLogger()
{
    isOn_loggingOutput[0] = true;
    isOn_loggingOutput[1] = false;
    isOn_loggingOutput[2] = false;
    loggingLevel = ELoggingLevel::Debug;

    initFileOutput();
    log("OmniLogger initialised!", true);
    log("IDE output ON", true);
    log("File output ON", true);
}

OmniLogger::~OmniLogger()
{
    if (logFile.isOpen())
        logFile.close();
    
    if (programOutputModel)
        delete programOutputModel;
}

OmniLogger& OmniLogger::get(ELoggingLevel ll, int logId) //has default argument LOGGINGLEVEL::Debug and logId -1
{
    if (!instance)
        instance = new OmniLogger();

    // Check if Id was given as a parameter
    if (logId != -1)
        instance->messageId = logId;
    else
        instance->messageId = -1;

    instance->loggingLevel = ll;
    return *instance;
}

void OmniLogger::initFileOutput()
{
    auto currentTimeStamp = time(0);
    tm* ltm = localtime(&currentTimeStamp);
    QString qTimeSuffix = QString("%1%2%3%4%5").
        arg((1900 + ltm->tm_year), 4, 10, QLatin1Char('0')).
        arg((1 + ltm->tm_mon), 2, 10, QLatin1Char('0')).
        arg((ltm->tm_mday), 2, 10, QLatin1Char('0')).
        arg((ltm->tm_hour), 2, 10, QLatin1Char('0')).
        arg((ltm->tm_min), 2, 10, QLatin1Char('0'));

    logFileName = QString("logOutput_%1.txt").arg(qTimeSuffix);

    QDir directory;
    if (!directory.exists(logFilePath))
        directory.mkpath(logFilePath);

    QString fullPath = logFilePath + logFileName;
    logFile.setFileName(fullPath);
    if (!logFile.open(QFile::WriteOnly | QFile::Text))
        qCritical() << "Could not create log file!";

    fileOutputStream.setDevice(&logFile);
    fileOutputStream.setCodec(QTextCodec::codecForName("UTF-8"));

    isOn_loggingOutput[1] = true;
}

void OmniLogger::initProgramOutput(OmnigenLogMessageItemModel* olmim)
{
    programOutputModel = olmim;
    programOutputModelInitialized = true;
    isOn_loggingOutput[2] = true;
    log("Program output ON", true);
}

void OmniLogger::log(QString&& message, bool flush)
{
    if (flush)
    {
        auto messageBundle = createMessageBundle(message, loggingLevel);

        QString fullMessage = QString("%1 %2 %3").
            arg(toQString(messageBundle.level)).
            arg(messageBundle.timeStamp).
            arg(messageBundle.message);

        static std::mutex guard;
        std::scoped_lock lock(guard);

        writeToIdeOutput(fullMessage, messageBundle.level);
        writeToFileOutput(fullMessage);
        writeToProgramOutput(messageBundle);
    }
    else
    {
        messageBuffer.emplace_back(message);
    }
}

void OmniLogger::writeToIdeOutput(const QString& fullMsg, ELoggingLevel level)
{
    if (isOn_loggingOutput[0])
    {
        if ((level == ELoggingLevel::Trace) || (level == ELoggingLevel::Debug))
            qDebug() << fullMsg;
        if (level == ELoggingLevel::Info)
            qInfo() << fullMsg;
        if (level == ELoggingLevel::Warn)
            qWarning() << fullMsg;
        if ((level == ELoggingLevel::Error) || (level == ELoggingLevel::Critical))
            qCritical() << fullMsg;
    }
}

void OmniLogger::writeToFileOutput(const QString& fullMsg)
{
    if (isOn_loggingOutput[1])
    {
        fileOutputStream << fullMsg << endl;
        fileOutputStream.flush();
    }
}

void OmniLogger::writeToProgramOutput(const OmniLoggerMesasgeBundle& messageBundle)
{
    if (programOutputModelInitialized && isOn_loggingOutput[2])
    {
        std::vector<QString> row = { messageBundle.timeStamp, toQString(messageBundle.level), messageBundle.message, toQString(messageBundle.id) };

        // If Id is passed as a parameter, try to change log entry with that Id
        if (messageId != -1)
        {
            programOutputModel->changeRow(row);
            return;
        }

        programOutputModel->insertRow(row);
    }
}

OmniLoggerMesasgeBundle OmniLogger::createMessageBundle(const QString& message, ELoggingLevel lvl)
{
    QString newMessage;

    if (messageBuffer.empty())
    {
        newMessage = message;
    }
    else
    {
        for (auto&& msg : messageBuffer)
            newMessage.push_back(msg);

        newMessage.push_back(message);
        messageBuffer.clear();
    }

    return { getCurrentTimestamp(), lvl, newMessage, messageId };
}

ELoggingLevel OmniLogger::getLoggingLevel()
{
    return loggingLevel;
}

QString OmniLogger::getCurrentTimestamp()
{
    auto currentTimeStamp = time(0);
    tm* ltm = localtime(&currentTimeStamp);

    //arg 1st argument - value to parse, 2nd argument - number of characters for value, 3rd - blank space filler
    QString qTimeStamp = QString("[%1-%2-%3 %4:%5:%6]").
        arg((1900 + ltm->tm_year), 4, 10, QLatin1Char('0')).    //years are 1900 based
        arg((1 + ltm->tm_mon), 2, 10, QLatin1Char('0')).    //months are zero-based
        arg((ltm->tm_mday), 2, 10, QLatin1Char('0')).
        arg((ltm->tm_hour), 2, 10, QLatin1Char('0')).
        arg((ltm->tm_min), 2, 10, QLatin1Char('0')).
        arg((ltm->tm_sec), 2, 10, QLatin1Char('0'));

    return qTimeStamp;
}
