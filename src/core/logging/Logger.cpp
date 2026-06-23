#include "Logger.h"

#include <QDateTime>
#include <QIODevice>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QTextStream>

namespace studio {

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

void Logger::open(const QString& path)
{
    QMutexLocker lock(&mutex_);
    if (file_.isOpen())
        file_.close();
    file_.setFileName(path);
    (void)file_.open(QIODevice::Append | QIODevice::Text);
}

void Logger::log(const QString& level,
                 const QString& event,
                 const QJsonObject& fields)
{
    QMutexLocker lock(&mutex_);
    if (!file_.isOpen())
        return;

    QJsonObject obj;
    obj["ts"]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    obj["level"] = level;
    obj["event"] = event;

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        obj[it.key()] = it.value();

    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    file_.write(line);
    file_.flush();
}

void Logger::close()
{
    QMutexLocker lock(&mutex_);
    if (file_.isOpen())
        file_.close();
}

} // namespace studio
