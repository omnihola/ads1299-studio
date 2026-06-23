#include <QtTest>
#include "core/dsp/ScaleConverter.h"
using studio::ScaleConverter;

class TestScale : public QObject { Q_OBJECT
private slots:
    void lsb_for_gain24() {
        ScaleConverter c(24);
        // (4.5 / (24 * 8388607... use 2^23=8388608)) * 1e6 ≈ 0.02235 µV
        QVERIFY(qAbs(c.lsbMicrovolts() - 0.0223517) < 1e-5);
    }
    void counts_scale_linearly() {
        ScaleConverter c(1);
        QVERIFY(qAbs(c.countsToMicrovolts(1) - c.lsbMicrovolts()) < 1e-9);
        QVERIFY(qAbs(c.countsToMicrovolts(-1000) + 1000*c.lsbMicrovolts()) < 1e-6);
    }
};

QTEST_MAIN(TestScale)
#include "test_scaleconverter.moc"
