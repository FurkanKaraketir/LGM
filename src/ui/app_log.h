#pragma once

#include <QObject>
#include <QStringList>

class AppLog : public QObject {
    Q_OBJECT

public:
    static AppLog& instance();
    static void install();

    QStringList lines() const;
    void clear();

public slots:
    void pushLine(const QString& line);

signals:
    void lineAppended(const QString& line);

private:
    explicit AppLog(QObject* parent = nullptr);

    static constexpr int kMaxLines = 5000;

    QStringList m_lines;
};
