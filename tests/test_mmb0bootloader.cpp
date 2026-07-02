// tests/test_mmb0bootloader.cpp
// TDD tests for Mmb0Bootloader — the 0451:9001 → 0451:5718 firmware-upload path.
//
// The MMB0 cold-boots as the C5000 ROM USB bootloader (0451:9001) on every
// power cycle; the app must be able to locate the ads1299evm-pdk.bin image
// (79248 bytes, known sha256), bulk-write it to ep 0x06 in one transfer, and
// wait for the board to re-enumerate as 0451:5718 (USBStyx firmware mode).
//
// The USB upload itself needs real hardware and is intentionally NOT run here
// (unit tests must never flash a board). These tests cover the pure logic:
// firmware location and validation.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "core/acquisition/mmb0/Mmb0Bootloader.h"

using studio::mmb0::Mmb0Bootloader;

namespace {

QString writeFile(const QString& path, const QByteArray& content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(content);
    f.close();
    return path;
}

} // namespace

class TestMmb0Bootloader : public QObject {
    Q_OBJECT

private slots:

    void knownImageConstants()
    {
        QCOMPARE(Mmb0Bootloader::kKnownFirmwareSize, qint64(79248));
        QCOMPARE(QString::fromLatin1(Mmb0Bootloader::kKnownFirmwareSha256),
                 QStringLiteral("b5c1bc9415c187d707a5a0e72429a11cc14c9b2ed0dc9a1afd8e8f306d1caf12"));
    }

    // locateFirmware returns the first candidate that exists as a readable file.
    void locateFirmwarePicksFirstExisting()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString missing = dir.filePath("does-not-exist.bin");
        const QString second  = writeFile(dir.filePath("fw-b.bin"), QByteArrayLiteral("x"));
        const QString third   = writeFile(dir.filePath("fw-c.bin"), QByteArrayLiteral("y"));
        QVERIFY(!second.isEmpty());
        QVERIFY(!third.isEmpty());

        const QString found = Mmb0Bootloader::locateFirmware({missing, second, third});
        QCOMPARE(found, second);
    }

    void locateFirmwareEmptyWhenNoneExist()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString found = Mmb0Bootloader::locateFirmware(
            {dir.filePath("a.bin"), dir.filePath("b.bin"), QString()});
        QVERIFY(found.isEmpty());
    }

    void validateMissingFile()
    {
        QTemporaryDir dir;
        QString detail;
        const auto st = Mmb0Bootloader::validateFirmware(dir.filePath("nope.bin"), &detail);
        QCOMPARE(st, Mmb0Bootloader::FirmwareStatus::Missing);
        QVERIFY(!detail.isEmpty());
    }

    void validateEmptyFile()
    {
        QTemporaryDir dir;
        const QString p = writeFile(dir.filePath("empty.bin"), QByteArray());
        QString detail;
        const auto st = Mmb0Bootloader::validateFirmware(p, &detail);
        QCOMPARE(st, Mmb0Bootloader::FirmwareStatus::Empty);
    }

    // A readable, non-empty file that is not the known release image is
    // classified UnknownImage (callers may proceed with a warning — a wrong
    // image simply fails to boot the DSP; the board stays in 9001).
    void validateUnknownImage()
    {
        QTemporaryDir dir;

        // Wrong size entirely.
        const QString small = writeFile(dir.filePath("small.bin"),
                                        QByteArray(1024, '\xAB'));
        QString detail;
        QCOMPARE(Mmb0Bootloader::validateFirmware(small, &detail),
                 Mmb0Bootloader::FirmwareStatus::UnknownImage);
        QVERIFY2(detail.contains(QStringLiteral("size")) || detail.contains(QStringLiteral("sha256")),
                 qPrintable(detail));

        // Right size, wrong content (sha256 mismatch).
        const QString sized = writeFile(dir.filePath("sized.bin"),
                                        QByteArray(int(Mmb0Bootloader::kKnownFirmwareSize), '\x00'));
        QCOMPARE(Mmb0Bootloader::validateFirmware(sized, &detail),
                 Mmb0Bootloader::FirmwareStatus::UnknownImage);
    }

    // USB presence probes must be safe to call with no hardware attached.
    void presenceProbesDoNotCrash()
    {
        const bool boot = Mmb0Bootloader::isBootloaderPresent();
        const bool styx = Mmb0Bootloader::isStyxPresent();
        QVERIFY(boot == true || boot == false);
        QVERIFY(styx == true || styx == false);
    }
};

QTEST_MAIN(TestMmb0Bootloader)
#include "test_mmb0bootloader.moc"
