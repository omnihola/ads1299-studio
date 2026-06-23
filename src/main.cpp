#include <QApplication>
#include <QFile>
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
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--selftest") {
            QTimer::singleShot(800, &app, &QApplication::quit);
        }
    }

    return app.exec();
}
