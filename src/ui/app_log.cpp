#include "app_log.h"

#include <QMutex>
#include <QMutexLocker>

#include <cstdio>

namespace {

QMutex g_logMutex;
QtMessageHandler g_previousHandler = nullptr;

QString formatLogLine(QtMsgType type, const QString& message) {
    const char* tag = "???";
    switch (type) {
    case QtDebugMsg:
        tag = "DBG";
        break;
    case QtInfoMsg:
        tag = "INF";
        break;
    case QtWarningMsg:
        tag = "WRN";
        break;
    case QtCriticalMsg:
        tag = "CRT";
        break;
    case QtFatalMsg:
        tag = "FTL";
        break;
    }
    return QStringLiteral("[%1] %2").arg(QString::fromLatin1(tag), message);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }

    const QString line = formatLogLine(type, message);
    QMetaObject::invokeMethod(&AppLog::instance(), "pushLine", Qt::QueuedConnection,
                              Q_ARG(QString, line));

    if (type == QtFatalMsg) {
        std::abort();
    }
}

}  // namespace

AppLog::AppLog(QObject* parent) : QObject(parent) {}

AppLog& AppLog::instance() {
    static AppLog log;
    return log;
}

void AppLog::install() {
    (void)instance();
    QMutexLocker lock(&g_logMutex);
    if (!g_previousHandler) {
        g_previousHandler = qInstallMessageHandler(messageHandler);
    }
}

QStringList AppLog::lines() const {
    return m_lines;
}

void AppLog::clear() {
    m_lines.clear();
}

void AppLog::pushLine(const QString& line) {
    m_lines.push_back(line);
    if (m_lines.size() > kMaxLines) {
        m_lines.removeFirst();
    }
    emit lineAppended(line);
}
