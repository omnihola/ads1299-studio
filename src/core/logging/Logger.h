#pragma once

#include <QFile>
#include <QJsonObject>
#include <QMutex>
#include <QString>

namespace studio {

class Logger
{
public:
    static Logger& instance();

    void open(const QString& path);
    void log(const QString& level,
             const QString& event,
             const QJsonObject& fields = {});
    void close();

private:
    Logger() = default;

    QFile   file_;
    QMutex  mutex_;
};

} // namespace studio
