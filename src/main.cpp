#include <QApplication>
#include <QLabel>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QLabel w("ADS1299 Studio — scaffold");
    w.resize(480, 240);
    w.show();
    return app.exec();
}
