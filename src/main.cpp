#include <QApplication>
#include <QFile>
#include <QPixmap>
#include <QTimer>
#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

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
