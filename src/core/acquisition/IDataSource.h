// src/core/acquisition/IDataSource.h
#pragma once
#include <QObject>
#include "core/acquisition/EegFrame.h"
#include "core/acquisition/SourceCapabilities.h"

namespace studio {
class IDataSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDataSource() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual SourceCapabilities capabilities() const = 0;
    virtual void setSampleRate(int sps) = 0;

signals:
    void framesReady(const studio::EegFrameBatch& batch);
    void errorOccurred(const QString& message);
    void runningChanged(bool running);
};
}

Q_DECLARE_METATYPE(studio::EegFrameBatch)
