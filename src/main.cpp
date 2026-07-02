#include <QApplication>
#include <QStyleFactory>
#include <QDebug>
#include <QFile>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTimer>
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"
#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    // Set OpenGL Core profile BEFORE constructing QApplication so that
    // QOpenGLWidget (GlWaveformWidget) gets a 3.3 Core context on macOS.
    // On headless/offscreen (no GPU) this is a no-op — the widget degrades
    // gracefully to a QPainter fallback.
    {
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setVersion(3, 3);
        fmt.setSwapInterval(1);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    QApplication app(argc, argv);

    // Identity for QSettings (persisted window geometry, user preferences).
    QApplication::setOrganizationName("ADS1299Studio");
    QApplication::setApplicationName("ads1299-studio");

    // Use the Fusion style so the custom dark stylesheet is honored fully and
    // consistently on every platform. The native macOS style draws some widgets
    // (notably the QTabWidget tab bar) with system chrome that overrides QSS
    // backgrounds, producing a light tab strip; Fusion respects the QSS everywhere.
    app.setStyle(QStyleFactory::create("Fusion"));

    // Apply dark studio theme
    QFile qss(":/studio.qss");
    if (qss.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    // Self-test smoke: `ads1299-studio --selftest` constructs the window,
    // applies the theme, then quits after 800 ms (used by CI / no-display verification).
    // Screenshot: `ads1299-studio --shot PATH` starts streaming, waits 1600 ms,
    // grabs the window to a PNG at PATH, then quits.
    // Device bring-up (hardware verification):
    //   --connect-mmb0   connect the MMB0/ADS1299 before the event loop starts
    //   --test-signal    configure the on-chip ±1.875 mV square wave (250 SPS,
    //                    all channels MUX=101) so the waveform is a known shape
    const char* shotPath = nullptr;
    bool connectMmb0 = false;
    bool testSignal  = false;
    int  shotDelayMs = 1600;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--selftest") {
            QTimer::singleShot(800, &app, &QApplication::quit);
        } else if (QString(argv[i]) == "--shot" && i + 1 < argc) {
            shotPath = argv[i + 1];
        } else if (QString(argv[i]) == "--shot-delay" && i + 1 < argc) {
            shotDelayMs = QString(argv[i + 1]).toInt();
        } else if (QString(argv[i]) == "--connect-mmb0") {
            connectMmb0 = true;
        } else if (QString(argv[i]) == "--test-signal") {
            testSignal = true;
        }
    }

    auto* controller = new studio::SessionController(new studio::SimulatedSource());
    if (testSignal) {
        studio::DeviceConfig cfg = controller->config()
                                       .withSampleRate(250)
                                       .withInternalTestSignal(true);
        for (int ch = 0; ch < 8; ++ch)
            cfg = cfg.withMux(ch, 5);   // MUX=101: internal test signal
        controller->applyConfig(cfg);
    }

    studio::MainWindow w(controller);   // takes ownership
    w.resize(1280, 800);
    w.show();

    if (connectMmb0) {
        QString err;
        if (!controller->connectMmb0(&err))
            qWarning() << "[main] --connect-mmb0 failed:" << err;
    }

    if (shotPath) {
        w.startStreamingForDemo();
        const QString outPath = QString::fromLocal8Bit(shotPath);
        QTimer::singleShot(shotDelayMs, [&w, &app, outPath]() {
            QPixmap pm = w.grab();
            pm.save(outPath);
            app.quit();
        });
    }

    return app.exec();
}
