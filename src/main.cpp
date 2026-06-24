#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTimer>
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

    studio::MainWindow w;
    w.resize(1280, 800);
    w.show();

    // Self-test smoke: `ads1299-studio --selftest` constructs the window,
    // applies the theme, then quits after 800 ms (used by CI / no-display verification).
    // Screenshot: `ads1299-studio --shot PATH` starts streaming, waits 1600 ms,
    // grabs the window to a PNG at PATH, then quits.
    const char* shotPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--selftest") {
            QTimer::singleShot(800, &app, &QApplication::quit);
        } else if (QString(argv[i]) == "--shot" && i + 1 < argc) {
            shotPath = argv[i + 1];
        }
    }

    if (shotPath) {
        w.startStreamingForDemo();
        const QString outPath = QString::fromLocal8Bit(shotPath);
        QTimer::singleShot(1600, [&w, &app, outPath]() {
            QPixmap pm = w.grab();
            pm.save(outPath);
            app.quit();
        });
    }

    return app.exec();
}
