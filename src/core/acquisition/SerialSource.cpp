// src/core/acquisition/SerialSource.cpp
#include "core/acquisition/SerialSource.h"
#include "core/logging/Logger.h"

#include <QSerialPortInfo>
#include <QJsonObject>

namespace studio {

// ──────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ──────────────────────────────────────────────────────────────────────────────

SerialSource::SerialSource(QObject* parent)
    : IDataSource(parent)
{
    connect(&port_, &QSerialPort::readyRead,
            this,   &SerialSource::onReadyRead);
    connect(&port_, &QSerialPort::errorOccurred,
            this, [this](QSerialPort::SerialPortError err) {
                if (err != QSerialPort::NoError) {
                    const QString msg = port_.errorString();
                    Logger::instance().log("error", "SerialSource.portError",
                                          QJsonObject{{"error", msg}});
                    emit errorOccurred(msg);
                }
            });
}

SerialSource::~SerialSource()
{
    closePort();
}

// ──────────────────────────────────────────────────────────────────────────────
// Port management
// ──────────────────────────────────────────────────────────────────────────────

bool SerialSource::openPort(const QString& portName, int baud)
{
    if (port_.isOpen()) {
        port_.close();
    }

    port_.setPortName(portName);
    port_.setBaudRate(baud);
    port_.setDataBits(QSerialPort::Data8);
    port_.setParity(QSerialPort::NoParity);
    port_.setStopBits(QSerialPort::OneStop);
    port_.setFlowControl(QSerialPort::NoFlowControl);

    if (!port_.open(QIODevice::ReadWrite)) {
        const QString msg = QString("SerialSource: cannot open %1 — %2")
                            .arg(portName, port_.errorString());
        Logger::instance().log("error", "SerialSource.openPort.failed",
                               QJsonObject{{"port", portName},
                                           {"baud", baud},
                                           {"error", port_.errorString()}});
        emit errorOccurred(msg);
        return false;
    }

    Logger::instance().log("info", "SerialSource.openPort",
                           QJsonObject{{"port", portName}, {"baud", baud}});
    return true;
}

void SerialSource::closePort()
{
    if (port_.isOpen()) {
        port_.close();
        Logger::instance().log("info", "SerialSource.closePort",
                               QJsonObject{{"port", port_.portName()}});
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// IDataSource overrides
// ──────────────────────────────────────────────────────────────────────────────

void SerialSource::start()
{
    running_ = true;
    emit runningChanged(true);

    // Optionally send a start command to the MCU.
    if (port_.isOpen()) {
        port_.write("s", 1);
    }

    Logger::instance().log("info", "SerialSource.start", {});
}

void SerialSource::stop()
{
    // Optionally send a stop command to the MCU.
    if (port_.isOpen()) {
        port_.write("x", 1);
        port_.flush();
    }

    running_ = false;
    emit runningChanged(false);

    Logger::instance().log("info", "SerialSource.stop", {});
}

SourceCapabilities SerialSource::capabilities() const
{
    SourceCapabilities caps;
    caps.name        = "ADS1299 (serial)";
    caps.channels    = 8;
    caps.sampleRates = {250, 500, 1000, 2000, 4000, 8000, 16000};
    return caps;
}

void SerialSource::setSampleRate(int sps)
{
    sampleRate_ = sps;
    // A real implementation could send a config command to the MCU here.
    // For now we just store the value so it is available if needed.
    Logger::instance().log("info", "SerialSource.setSampleRate",
                           QJsonObject{{"sps", sps}});
}

// ──────────────────────────────────────────────────────────────────────────────
// Private slot — called on the worker thread by Qt's event loop
// ──────────────────────────────────────────────────────────────────────────────

void SerialSource::onReadyRead()
{
    const QByteArray bytes = port_.readAll();
    parseAndEmit(bytes);
}

// ──────────────────────────────────────────────────────────────────────────────
// Public test seam — same code path, no real port
// ──────────────────────────────────────────────────────────────────────────────

void SerialSource::feedBytesForTest(const QByteArray& bytes)
{
    parseAndEmit(bytes);
}

// ──────────────────────────────────────────────────────────────────────────────
// Shared parse-and-emit kernel
// ──────────────────────────────────────────────────────────────────────────────

void SerialSource::parseAndEmit(const QByteArray& bytes)
{
    parser_.feed(bytes);
    QVector<EegFrame> frames = parser_.takeFrames();
    if (!frames.isEmpty()) {
        // Convert QVector<EegFrame> → std::vector<EegFrame> (EegFrameBatch)
        EegFrameBatch batch(frames.cbegin(), frames.cend());
        emit framesReady(batch);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Static utility
// ──────────────────────────────────────────────────────────────────────────────

QStringList SerialSource::availablePorts()
{
    QStringList names;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        names << info.portName();
    }
    return names;
}

} // namespace studio
