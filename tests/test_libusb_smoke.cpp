#include <QtTest>
#include <libusb.h>

class TestLibusbSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initAndExit()
    {
        libusb_context* ctx = nullptr;
        int rc = libusb_init(&ctx);
        QCOMPARE(rc, 0);
        QVERIFY(ctx != nullptr);
        libusb_exit(ctx);
    }
};

QTEST_MAIN(TestLibusbSmoke)
#include "test_libusb_smoke.moc"
