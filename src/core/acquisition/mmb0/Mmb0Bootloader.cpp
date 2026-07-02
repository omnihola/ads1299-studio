#include "Mmb0Bootloader.h"
#include "Mmb0UsbTransport.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QThread>

#include <libusb.h>

namespace studio::mmb0 {

const char* Mmb0Bootloader::kKnownFirmwareSha256 =
    "b5c1bc9415c187d707a5a0e72429a11cc14c9b2ed0dc9a1afd8e8f306d1caf12";

// ── Firmware location / validation ───────────────────────────────────────────

QString Mmb0Bootloader::locateFirmware(const QStringList& candidates)
{
    for (const QString& c : candidates) {
        if (c.isEmpty()) continue;
        const QFileInfo fi(c);
        if (fi.isFile() && fi.isReadable())
            return c;
    }
    return QString();
}

QStringList Mmb0Bootloader::defaultCandidates()
{
    QStringList candidates;

    const QString configured =
        QSettings().value(QStringLiteral("mmb0/firmwarePath")).toString();
    if (!configured.isEmpty())
        candidates << configured;

    const QString env = qEnvironmentVariable("ADS1299_FIRMWARE");
    if (!env.isEmpty())
        candidates << env;

    const QString name = QStringLiteral("ads1299evm-pdk.bin");
    candidates << QCoreApplication::applicationDirPath()
                      + QStringLiteral("/firmware/") + name;
    candidates << QDir::current().filePath(QStringLiteral("firmware/") + name);
    candidates << QDir::current().filePath(name);

    return candidates;
}

Mmb0Bootloader::FirmwareStatus
Mmb0Bootloader::validateFirmware(const QString& path, QString* detail)
{
    auto explain = [&](const QString& d) {
        if (detail) *detail = d;
    };

    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists()) {
        explain(QStringLiteral("firmware file not found: %1").arg(path));
        return FirmwareStatus::Missing;
    }

    QFile f(path);
    if (!fi.isFile() || !f.open(QIODevice::ReadOnly)) {
        explain(QStringLiteral("firmware file not readable: %1").arg(path));
        return FirmwareStatus::Unreadable;
    }

    const QByteArray content = f.readAll();
    if (content.isEmpty()) {
        explain(QStringLiteral("firmware file is empty: %1").arg(path));
        return FirmwareStatus::Empty;
    }

    if (content.size() != kKnownFirmwareSize) {
        explain(QStringLiteral("unexpected size %1 (known release image is %2 bytes)")
                    .arg(content.size())
                    .arg(kKnownFirmwareSize));
        return FirmwareStatus::UnknownImage;
    }

    const QByteArray sha256 =
        QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
    if (sha256 != QByteArray(kKnownFirmwareSha256)) {
        explain(QStringLiteral("sha256 %1 does not match the known release image")
                    .arg(QString::fromLatin1(sha256)));
        return FirmwareStatus::UnknownImage;
    }

    explain(QStringLiteral("known release image"));
    return FirmwareStatus::Ok;
}

// ── USB presence probes ──────────────────────────────────────────────────────

bool Mmb0Bootloader::isBootloaderPresent()
{
    return Mmb0UsbTransport::isDevicePresent(kVid, kBootloaderPid);
}

bool Mmb0Bootloader::isStyxPresent()
{
    return Mmb0UsbTransport::isDevicePresent(kVid, kStyxPid);
}

// ── Upload ───────────────────────────────────────────────────────────────────

bool Mmb0Bootloader::uploadFirmware(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        lastError_ = QStringLiteral("cannot open firmware file: %1").arg(path);
        return false;
    }
    QByteArray image = f.readAll();
    if (image.isEmpty()) {
        lastError_ = QStringLiteral("firmware file is empty: %1").arg(path);
        return false;
    }

    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        lastError_ = QStringLiteral("libusb_init failed");
        return false;
    }

    libusb_device_handle* dev =
        libusb_open_device_with_vid_pid(ctx, kVid, kBootloaderPid);
    if (!dev) {
        lastError_ = QStringLiteral("bootloader device 0451:9001 not found");
        libusb_exit(ctx);
        return false;
    }

    bool ok = false;
    do {
        // On Linux the kernel may hold the interface; ignored on macOS/Windows.
        libusb_set_auto_detach_kernel_driver(dev, 1);

        // The ROM bootloader enumerates unconfigured — select config 1 first.
        int rc = libusb_set_configuration(dev, 1);
        if (rc != 0 && rc != LIBUSB_ERROR_BUSY) {
            lastError_ = QStringLiteral("set_configuration failed: %1")
                             .arg(libusb_error_name(rc));
            break;
        }
        rc = libusb_claim_interface(dev, 0);
        if (rc != 0) {
            lastError_ = QStringLiteral("claim_interface failed: %1")
                             .arg(libusb_error_name(rc));
            break;
        }
        libusb_set_interface_alt_setting(dev, 0, 0);

        // The WHOLE image in one bulk transfer — libusb packetizes internally.
        // The known image (79248 % 64 = 16) ends on a natural short packet;
        // an image that is an exact packet multiple needs an explicit ZLP so
        // the ROM loader sees end-of-transfer.
        int transferred = 0;
        rc = libusb_bulk_transfer(dev, kUploadEp,
                                  reinterpret_cast<uint8_t*>(image.data()),
                                  image.size(), &transferred, 5000 /* ms */);
        if (rc == 0 && transferred == image.size()
            && Mmb0UsbTransport::needsZlp(image.size())) {
            int zero = 0;
            rc = libusb_bulk_transfer(dev, kUploadEp,
                                      reinterpret_cast<uint8_t*>(image.data()),
                                      0, &zero, 5000 /* ms */);
        }
        libusb_release_interface(dev, 0);

        if (rc != 0 || transferred != image.size()) {
            lastError_ = QStringLiteral("firmware bulk write failed: %1 (%2/%3 bytes)")
                             .arg(rc != 0 ? libusb_error_name(rc) : "short write")
                             .arg(transferred)
                             .arg(image.size());
            break;
        }
        ok = true;
    } while (false);

    libusb_close(dev);
    libusb_exit(ctx);
    return ok;
}

bool Mmb0Bootloader::waitForStyx(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (isStyxPresent())
            return true;
        QThread::msleep(200);
    }
    lastError_ = QStringLiteral("device did not re-enumerate as 0451:5718 within %1 ms")
                     .arg(timeoutMs);
    return false;
}

QString Mmb0Bootloader::lastError() const
{
    return lastError_;
}

} // namespace studio::mmb0
