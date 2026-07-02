// tests/test_mainwindowgating.cpp
// TDD tests for the connection-status + Start-gating feature (handoff §GUI):
//
//   - Persistent connection state: a status label + the link LED reflect the
//     last connect result until the next attempt (not a transient toast).
//   - Start gating: startAction_ is enabled only when a usable source is
//     active. The simulator always counts as usable (default path must not be
//     locked out); MMB0/serial sources require a successful connect and are
//     re-disabled when the device errors/drops.
//   - The serial-port prompt is virtualized (promptSerialPort) so tests can
//     drive the Connect flow headlessly, and it offers a "Simulator" entry so
//     the user can always return to the built-in source.

#include <QtTest>
#include <QAction>
#include <QLabel>

#include "ui/MainWindow.h"
#include "ui/ControlGating.h"
#include "app/SessionController.h"
#include "app/AppState.h"
#include "core/acquisition/SimulatedSource.h"
#include "core/acquisition/mmb0/Mmb0Bootloader.h"
#include "core/acquisition/mmb0/Mmb0DataSource.h"
#include "core/acquisition/mmb0/ITransport.h"

namespace {
// Tests below drive the Connect flow expecting the MMB0 attempt to fail;
// with a real board attached the attempt would succeed (or flash firmware).
bool mmb0HardwareAttached()
{
    return studio::mmb0::Mmb0Bootloader::isStyxPresent()
        || studio::mmb0::Mmb0Bootloader::isBootloaderPresent();
}
} // namespace

namespace {

// Transport whose recv always fails — a "dead" MMB0 device.
class FailingTransport : public studio::styx::ITransport {
public:
    bool send(const QByteArray&) override { return true; }
    bool recv(QByteArray& out, int) override { out.clear(); return false; }
};

// MainWindow with the modal serial-port prompt stubbed out.
class TestableMainWindow : public studio::MainWindow {
public:
    using studio::MainWindow::MainWindow;
    QString promptResult;   // what the "user" picks; empty = cancel

protected:
    QString promptSerialPort(const QStringList& options) override {
        lastOptions = options;
        return promptResult;
    }

public:
    QStringList lastOptions;
};

} // namespace

class TestMainWindowGating : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // Keep QSettings reads/writes away from the real app's settings.
        QCoreApplication::setOrganizationName(QStringLiteral("ads1299-studio-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("gating-tests"));
    }

    // ── Pure gating logic ────────────────────────────────────────────────────
    void pureGatingRules() {
        using studio::SourceType;
        using studio::gating::startEnabled;

        // Simulator: always startable unless a connect attempt is in flight.
        QVERIFY( startEnabled(SourceType::Simulated, /*linkUp=*/false, /*connecting=*/false));
        QVERIFY(!startEnabled(SourceType::Simulated, false, /*connecting=*/true));

        // Hardware sources: need a live link.
        QVERIFY( startEnabled(SourceType::Mmb0,   /*linkUp=*/true,  false));
        QVERIFY(!startEnabled(SourceType::Mmb0,   /*linkUp=*/false, false));
        QVERIFY( startEnabled(SourceType::Serial, true,  false));
        QVERIFY(!startEnabled(SourceType::Serial, false, false));
        QVERIFY(!startEnabled(SourceType::Mmb0,   true,  /*connecting=*/true));
    }

    // ── Boot state: simulator active → Start enabled, status says so ────────
    void simulatorActiveAtBoot() {
        TestableMainWindow w;   // default controller = SimulatedSource

        auto* start = w.findChild<QAction*>(QStringLiteral("startAction"));
        auto* connectAct = w.findChild<QAction*>(QStringLiteral("connectAction"));
        auto* label = w.findChild<QLabel*>(QStringLiteral("connStatusLabel"));
        QVERIFY(start);
        QVERIFY(connectAct);
        QVERIFY(label);

        QVERIFY(start->isEnabled());
        QVERIFY(connectAct->isEnabled());
        QVERIFY2(label->text().contains(QStringLiteral("Simulator"), Qt::CaseInsensitive),
                 qPrintable(label->text()));
    }

    // ── Failed connect: persistent failure status, simulator keeps Start ────
    void failedConnectShowsPersistentStatusAndKeepsSimulator() {
        if (mmb0HardwareAttached())
            QSKIP("MMB0 hardware attached — failed-connect flow not testable");
        TestableMainWindow w;
        w.promptResult = QString();   // user cancels the fallback prompt

        auto* start = w.findChild<QAction*>(QStringLiteral("startAction"));
        auto* connectAct = w.findChild<QAction*>(QStringLiteral("connectAction"));
        auto* label = w.findChild<QLabel*>(QStringLiteral("connStatusLabel"));
        QVERIFY(start && connectAct && label);

        connectAct->trigger();        // no MMB0 hardware on the test machine
        QTest::qWait(50);

        // Status is persistent and reports the failure / not-connected state.
        QVERIFY2(label->text().contains(QStringLiteral("no device"), Qt::CaseInsensitive)
                     || label->text().contains(QStringLiteral("fail"), Qt::CaseInsensitive)
                     || label->text().contains(QStringLiteral("not connected"), Qt::CaseInsensitive),
                 qPrintable(label->text()));

        // Simulator is still the active source → Start must stay available.
        QVERIFY(start->isEnabled());
        QVERIFY(connectAct->isEnabled());

        // The fallback prompt must offer the simulator escape hatch.
        QVERIFY(w.lastOptions.contains(studio::MainWindow::simulatorOptionLabel()));
    }

    // ── Device drop: source error disables Start until reconnected ──────────
    void deviceErrorDisablesStartAndSimulatorRestoresIt() {
        if (mmb0HardwareAttached())
            QSKIP("MMB0 hardware attached — reconnect flow would touch the device");
        auto* ctrl = new studio::SessionController(new studio::SimulatedSource());
        TestableMainWindow w(ctrl);

        auto* start = w.findChild<QAction*>(QStringLiteral("startAction"));
        auto* label = w.findChild<QLabel*>(QStringLiteral("connStatusLabel"));
        QVERIFY(start && label);

        // Inject a "connected" MMB0 source whose transport is dead.
        ctrl->setSource(new studio::mmb0::Mmb0DataSource(new FailingTransport()),
                        studio::SourceType::Mmb0);
        QTest::qWait(20);

        // A device source just became active → treated as linked, Start allowed.
        QVERIFY(start->isEnabled());

        // Streaming attempt fails in bringUp → errorOccurred → link marked down.
        ctrl->startStreaming();
        QTRY_VERIFY_WITH_TIMEOUT(!start->isEnabled(), 2000);
        ctrl->stopStreaming();

        // Reconnect via the Connect flow, picking the Simulator entry.
        w.promptResult = studio::MainWindow::simulatorOptionLabel();
        auto* connectAct = w.findChild<QAction*>(QStringLiteral("connectAction"));
        QVERIFY(connectAct);
        connectAct->trigger();
        QTest::qWait(50);

        QVERIFY(start->isEnabled());
        QVERIFY2(label->text().contains(QStringLiteral("Simulator"), Qt::CaseInsensitive),
                 qPrintable(label->text()));
    }
};

QTEST_MAIN(TestMainWindowGating)
#include "test_mainwindowgating.moc"
