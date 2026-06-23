// src/core/acquisition/SerialSource.h
//
// SerialSource — QtSerialPort-backed IDataSource for the ADS1299 firmware.
//
// Thread safety:
//   SerialSource is intended to live on a dedicated QThread (managed by
//   SessionController's workerThread_). All public methods are invoked via
//   QMetaObject::invokeMethod from the controller thread. openPort() and
//   closePort() may be called from any thread before start() is called.
//
// Test seam:
//   feedBytesForTest(bytes) runs the IDENTICAL parse path as onReadyRead()
//   without requiring a real serial port, enabling headless unit testing.

#pragma once
#include "core/acquisition/IDataSource.h"
#include "core/acquisition/FrameParser.h"

#include <QSerialPort>
#include <QString>
#include <QStringList>

namespace studio {

class SerialSource : public IDataSource
{
    Q_OBJECT

public:
    explicit SerialSource(QObject* parent = nullptr);
    ~SerialSource() override;

    // ---- Port management --------------------------------------------------
    // Opens the port at the given baud rate (8N1). Returns false on failure;
    // also emits errorOccurred and logs. Safe to call before start().
    bool openPort(const QString& portName, int baud = 921600);

    // Closes the serial port. Safe to call even if not open.
    void closePort();

    // ---- IDataSource overrides --------------------------------------------
    void start()    override;
    void stop()     override;
    bool isRunning() const override { return running_; }
    SourceCapabilities capabilities() const override;
    void setSampleRate(int sps) override;

    // ---- Test seam --------------------------------------------------------
    // Feeds raw bytes through the SAME parse → emit path as the real hardware
    // callback. No serial port required.
    void feedBytesForTest(const QByteArray& bytes);

    // ---- Static utility ---------------------------------------------------
    // Returns the system port names (e.g. "/dev/cu.usbmodem…", "COM3") from
    // QSerialPortInfo::availablePorts(). Never crashes; returns empty list if
    // the OS has no ports.
    static QStringList availablePorts();

    // ---- Diagnostics (delegate to FrameParser) ----------------------------
    uint64_t crcErrors() const { return parser_.crcErrors(); }
    uint64_t resyncs()   const { return parser_.resyncs();   }

private slots:
    void onReadyRead();

private:
    // Shared parse-and-emit path used by both onReadyRead and feedBytesForTest.
    void parseAndEmit(const QByteArray& bytes);

    QSerialPort port_;
    FrameParser parser_;
    bool        running_ = false;
    int         sampleRate_ = 250;
};

} // namespace studio
