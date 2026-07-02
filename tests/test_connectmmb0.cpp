// tests/test_connectmmb0.cpp
// QtTest — headless test for SessionController::connectMmb0()
// No real MMB0 device is expected on this machine; the test validates:
//   1. connectMmb0() returns false gracefully when no device is present.
//   2. The original source remains intact and functional after the failed call.

#include <QtTest>
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"
#include "core/acquisition/mmb0/Mmb0Bootloader.h"

class TestConnectMmb0 : public QObject
{
    Q_OBJECT

private slots:
    // connectMmb0() must return false when no MMB0 hardware is present,
    // and the controller's existing source must remain fully operational.
    void connectMmb0NoDeviceReturnsFalseGracefully()
    {
        // This test asserts the NO-DEVICE behavior; with a real board attached
        // connectMmb0 would succeed (and even flash firmware) — skip instead
        // of driving hardware from a unit test.
        if (studio::mmb0::Mmb0Bootloader::isStyxPresent()
            || studio::mmb0::Mmb0Bootloader::isBootloaderPresent()) {
            QSKIP("MMB0 hardware attached — no-device behavior not testable");
        }

        // Arrange: controller with a live SimulatedSource (1-channel, 250 SPS).
        studio::SessionController ctrl(new studio::SimulatedSource(1));

        // Act: attempt MMB0 connect — no device present on this machine.
        const bool result = ctrl.connectMmb0();

        // Assert: returns false without crashing.
        QCOMPARE(result, false);

        // Assert: the original SimulatedSource is still intact — start streaming,
        // wait briefly, verify data arrived, then stop cleanly.
        ctrl.startStreaming();
        QTest::qWait(150);
        QVERIFY(ctrl.displayBuffer().size() > 0);
        ctrl.stopStreaming();
    }
};

QTEST_MAIN(TestConnectMmb0)
#include "test_connectmmb0.moc"
