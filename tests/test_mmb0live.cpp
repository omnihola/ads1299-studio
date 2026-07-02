// tests/test_mmb0live.cpp
// LIVE hardware smoke test for the MMB0 Styx transport + 9P client.
// Skips entirely unless a 0451:5718 (Styx-mode) device is attached, so it is
// inert in CI. With hardware attached it exercises exactly the bring-up path
// Mmb0DataSource uses — open → attach(nobody) → read /version → read devid —
// and prints every wire-level detail on failure.

#include <QtTest>
#include <QDebug>
#include <QSignalSpy>

#include "core/acquisition/mmb0/Mmb0Bootloader.h"
#include "core/acquisition/mmb0/Mmb0DataSource.h"
#include "core/acquisition/mmb0/Mmb0UsbTransport.h"
#include "core/acquisition/mmb0/Styx9pClient.h"
#include "core/device/DeviceConfig.h"

using namespace studio::styx;
using namespace studio::mmb0;

class TestMmb0Live : public QObject {
    Q_OBJECT

private slots:

    // Cold-boot self-sufficiency: if the board sits in ROM-bootloader mode
    // (0451:9001), upload the firmware with OUR bootloader implementation —
    // this doubles as the live validation of Mmb0Bootloader::uploadFirmware.
    void initTestCase()
    {
        if (qEnvironmentVariable("ADS1299_RUN_LIVE_TESTS") != QStringLiteral("1"))
            QSKIP("Live MMB0 tests are opt-in; set ADS1299_RUN_LIVE_TESTS=1");

        if (Mmb0Bootloader::isStyxPresent() || !Mmb0Bootloader::isBootloaderPresent())
            return;

        const QString fw =
            Mmb0Bootloader::locateFirmware(Mmb0Bootloader::defaultCandidates());
        if (fw.isEmpty())
            QSKIP("board in bootloader mode and no firmware image found");

        qInfo() << "[live] 0451:9001 bootloader detected — uploading" << fw;
        Mmb0Bootloader boot;
        QVERIFY2(boot.uploadFirmware(fw), qPrintable(boot.lastError()));
        QVERIFY2(boot.waitForStyx(), qPrintable(boot.lastError()));
        qInfo() << "[live] firmware uploaded, board re-enumerated as 0451:5718";
    }

    void liveAttachAndReadVersion()
    {
        if (!Mmb0Bootloader::isStyxPresent())
            QSKIP("No 0451:5718 Styx-mode device attached");

        Mmb0UsbTransport t;
        QVERIFY2(t.open(), qPrintable(t.lastError()));

        Styx9pClient client(&t, 3000);

        const bool attached = client.attach(QStringLiteral("nobody"),
                                            QStringLiteral("nobody"));
        QVERIFY2(attached, qPrintable(
            QStringLiteral("attach failed: %1 / transport: %2")
                .arg(client.lastError(), t.lastError())));

        QByteArray version;
        QVERIFY2(client.readPath(QStringLiteral("/version"), version, 64),
                 qPrintable(client.lastError()));
        qInfo() << "[live] /version =" << version;
        QVERIFY(version.contains("0.0"));

        QByteArray devid;
        QVERIFY2(client.readPath(QStringLiteral("/ads1299evm/conf/devid"), devid, 64),
                 qPrintable(client.lastError()));
        qInfo() << "[live] devid =" << devid;
        QCOMPARE(QString::fromLatin1(devid.trimmed()), QStringLiteral("0x3E"));

        t.close();
    }

    // Full acquisition path against the real board: configure the on-chip
    // ±1.875 mV test signal (gain 24 → ≈ ±83886 codes; +82.4k measured) and
    // verify decoded frames carry that amplitude with clean status words.
    void liveStreamInternalTestSignal()
    {
        if (!Mmb0Bootloader::isStyxPresent())
            QSKIP("No 0451:5718 Styx-mode device attached");

        auto* transport = new Mmb0UsbTransport();
        QVERIFY2(transport->open(), qPrintable(transport->lastError()));

        Mmb0DataSource src(transport);   // takes ownership
        studio::DeviceConfig cfg = studio::DeviceConfig()
                                       .withSampleRate(250)
                                       .withInternalTestSignal(true);
        for (int ch = 0; ch < 8; ++ch)
            cfg = cfg.withMux(ch, 5);    // MUX=101: internal test signal
        src.setConfig(cfg);
        src.setBlocksizeSamples(64);   // 576 words — the fresh-flash-stable block size

        QSignalSpy frameSpy(&src, &studio::IDataSource::framesReady);
        QSignalSpy errorSpy(&src, &studio::IDataSource::errorOccurred);

        src.start();
        QTest::qWait(2500);
        src.stop();
        QTest::qWait(400);   // let the final one-shot block settle (SDATAC)

        QVERIFY2(errorSpy.isEmpty(),
                 qPrintable(errorSpy.isEmpty()
                                ? QString()
                                : errorSpy.at(0).at(0).toString()));

        int total = 0, inBand = 0;
        for (int i = 0; i < frameSpy.count(); ++i) {
            const auto batch = frameSpy.at(i).at(0).value<studio::EegFrameBatch>();
            for (const auto& f : batch) {
                ++total;
                // Square wave: samples sit near +82k or -82k; allow a small
                // number of mid-transition/settling samples outside the band.
                const int v = qAbs(f.ch[0]);
                if (v > 70000 && v < 95000)
                    ++inBand;
            }
        }
        qInfo() << "[live] frames:" << total << " in-band:" << inBand;

        // ≥ 2.5 s × 250 SPS × ~60% one-shot duty cycle.
        QVERIFY2(total > 300, qPrintable(QStringLiteral("only %1 frames").arg(total)));
        QVERIFY2(inBand > total * 95 / 100,
                 qPrintable(QStringLiteral("%1/%2 in amplitude band").arg(inBand).arg(total)));
    }

    // Channel-mapping proof, run through the GUI's real stop→reconfigure→
    // start flow in ONE session: first stream with all channels on the test
    // signal, then stop, re-route the test signal into chip channels 1, 2,
    // and 3 one at a time (target CHx MUX=101; others MUX=001 shorted).
    // frame.ch[0..2] must carry the ±82k square wave only when the matching
    // chip channel is selected. MonitorView/GlWaveformWidget map frame.ch[c]
    // directly to GUI lane "CH(c+1)", so this establishes GUI CH1/2/3 ↔
    // ADS1299 CH1/2/3 one-to-one.
    void liveChannelMappingFirstThreeChannelsToGuiLanes()
    {
        if (!Mmb0Bootloader::isStyxPresent())
            QSKIP("No 0451:5718 Styx-mode device attached");

        auto* transport = new Mmb0UsbTransport();
        if (!transport->open()) {
            const QString why = transport->lastError();
            delete transport;
            QSKIP(qPrintable(QStringLiteral(
                "device busy (close the running GUI app?): %1").arg(why)));
        }

        Mmb0DataSource src(transport);
        src.setBlocksizeSamples(64);   // 576 words — the fresh-flash-stable block size

        // Phase 1 — all channels on the test signal.
        studio::DeviceConfig allTest = studio::DeviceConfig()
                                           .withSampleRate(250)
                                           .withInternalTestSignal(true);
        for (int ch = 0; ch < 8; ++ch)
            allTest = allTest.withMux(ch, 5);
        src.setConfig(allTest);

        {
            QSignalSpy warmupSpy(&src, &studio::IDataSource::framesReady);
            src.start();
            QTest::qWait(800);
            src.stop();   // deliberately lands mid-block most of the time
            QVERIFY2(warmupSpy.count() > 0, "no frames in the warm-up phase");
        }

        for (int target = 0; target < 3; ++target) {
            studio::DeviceConfig cfg = allTest;
            for (int ch = 0; ch < 8; ++ch)
                cfg = cfg.withMux(ch, ch == target ? 5 : 1);
            src.setConfig(cfg);

            QSignalSpy frameSpy(&src, &studio::IDataSource::framesReady);
            src.start();
            QTest::qWait(2500);
            src.stop();
            QTest::qWait(400);   // let the final one-shot block settle (SDATAC)

            int total = 0, targetInBand = 0, othersQuiet = 0;
            qint64 sum[8] = {};
            int mn[8], mx[8];
            std::fill(std::begin(mn), std::end(mn), INT_MAX);
            std::fill(std::begin(mx), std::end(mx), INT_MIN);
            for (int i = 0; i < frameSpy.count(); ++i) {
                const auto batch = frameSpy.at(i).at(0).value<studio::EegFrameBatch>();
                for (const auto& f : batch) {
                    ++total;
                    for (int c = 0; c < 8; ++c) {
                        sum[c] += f.ch[c];
                        mn[c] = qMin(mn[c], f.ch[c]);
                        mx[c] = qMax(mx[c], f.ch[c]);
                    }
                    const int targetAbs = qAbs(f.ch[target]);
                    if (targetAbs > 70000 && targetAbs < 95000) ++targetInBand;
                    bool quiet = true;
                    for (int c = 0; c < 8; ++c)
                        if (c != target && qAbs(f.ch[c]) > 5000) quiet = false;
                    if (quiet) ++othersQuiet;
                }
            }
            qInfo() << "[live] mapping: target CH" << (target + 1)
                    << "total" << total << "target-in-band" << targetInBand
                    << "others-quiet" << othersQuiet;
            for (int c = 0; c < 8 && total > 0; ++c)
                qInfo() << "[live]   ch" << (c + 1) << "mean" << (sum[c] / total)
                        << "min" << mn[c] << "max" << mx[c];

            QVERIFY2(total > 300, qPrintable(QStringLiteral("only %1 frames for CH%2")
                                             .arg(total).arg(target + 1)));
            QVERIFY2(targetInBand > total * 95 / 100,
                     qPrintable(QStringLiteral("frame.ch[%1] carried the test signal "
                                               "in only %2/%3 samples")
                                    .arg(target).arg(targetInBand).arg(total)));
            QVERIFY2(othersQuiet > total * 95 / 100,
                     qPrintable(QStringLiteral("non-target channels were quiet in only "
                                               "%1/%2 samples for CH%3")
                                    .arg(othersQuiet).arg(total).arg(target + 1)));
        }
    }
};

QTEST_MAIN(TestMmb0Live)
#include "test_mmb0live.moc"
