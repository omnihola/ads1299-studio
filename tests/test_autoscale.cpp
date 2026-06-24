// tests/test_autoscale.cpp
//
// Unit tests for studio::gl::autoScaleStep — pure smoothing helper.
// Headless – no OpenGL context, no GL state required.

#include <QtTest>
#include "ui/gl/AutoScale.h"

using namespace studio::gl;

class TestAutoScale : public QObject
{
    Q_OBJECT

private slots:

    // -------------------------------------------------------------------------
    // attackGrowsFast
    // When the signal peak is larger than the current scale, the scale should
    // jump toward the target quickly (using the attack coefficient).
    // -------------------------------------------------------------------------
    void attackGrowsFast()
    {
        // current=100, peak=850 → target = 850/0.85 = 1000.0
        // one step (attack=0.5): 100 + (1000-100)*0.5 = 550
        constexpr double current  = 100.0;
        constexpr double peak     = 850.0;
        constexpr double floor_uv = 2.0;
        constexpr double fill     = 0.85;
        constexpr double attack   = 0.5;
        constexpr double release  = 0.05;

        const double next = autoScaleStep(current, peak, floor_uv, fill, attack, release);

        // Should be very close to 550
        QVERIFY2(qAbs(next - 550.0) < 1e-9, "attack step from 100 with peak=850 must be ~550");

        // Must be strictly larger than current (growing)
        QVERIFY2(next > current, "scale must grow toward target when peak > current");

        // And it must have moved much more than a release step would have
        // (release from a 900-wide gap would be 900*0.05=45; attack gives 450)
        const double releaseStep = current + (1000.0 - current) * release;
        QVERIFY2(next > releaseStep, "attack step must be much larger than release step");
    }

    // -------------------------------------------------------------------------
    // releaseShrinksSlow
    // When the signal peak has shrunk, the scale should decay slowly (release).
    // -------------------------------------------------------------------------
    void releaseShrinksSlow()
    {
        // current=1000, peak=85 → target = 85/0.85 = 100.0
        // one step (release=0.05): 1000 + (100-1000)*0.05 = 1000 - 45 = 955
        constexpr double current  = 1000.0;
        constexpr double peak     = 85.0;
        constexpr double floor_uv = 2.0;
        constexpr double fill     = 0.85;
        constexpr double attack   = 0.5;
        constexpr double release  = 0.05;

        const double next = autoScaleStep(current, peak, floor_uv, fill, attack, release);

        // Should be ~955
        QVERIFY2(qAbs(next - 955.0) < 1e-9, "release step from 1000 with peak=85 must be ~955");

        // Must be strictly smaller than current (shrinking)
        QVERIFY2(next < current, "scale must shrink when target < current");

        // Must remain well above the target after only one step
        QVERIFY2(next > 500.0, "scale must remain far above target after one release step");
    }

    // -------------------------------------------------------------------------
    // respectsFloor
    // With a flat / zero signal, the scale must not go below floor-derived target
    // (floor/fill ≈ 2.35).  After many iterations from a high start it should
    // settle near ~2.35 and never go below floorUv.
    // -------------------------------------------------------------------------
    void respectsFloor()
    {
        constexpr double floor_uv = 2.0;
        constexpr double fill     = 0.85;
        constexpr double attack   = 0.5;
        constexpr double release  = 0.05;

        // Expected floor target
        const double floorTarget = floor_uv / fill; // ≈ 2.352...

        double scale = 100.0;
        for (int i = 0; i < 500; ++i) {
            scale = autoScaleStep(scale, 0.0, floor_uv, fill, attack, release);
            // Must never drop below floorUv
            QVERIFY2(scale >= floor_uv,
                     qPrintable(QString("scale dropped below floorUv at iteration %1").arg(i)));
        }

        // After 500 release steps from 100 toward ~2.35, should be very close
        QVERIFY2(scale >= floorTarget - 1e-6,
                 "scale must never go below floor-derived target");
        QVERIFY2(scale < floorTarget + 0.1,
                 "scale must converge close to floor-derived target after 500 steps");
    }

    // -------------------------------------------------------------------------
    // convergesToFillPeak
    // With a constant peak, iterating many times should converge to
    // peak/fill (≈ 588.2 for peak=500, fill=0.85).
    // -------------------------------------------------------------------------
    void convergesToFillPeak()
    {
        constexpr double peak     = 500.0;
        constexpr double floor_uv = 2.0;
        constexpr double fill     = 0.85;
        constexpr double attack   = 0.5;
        constexpr double release  = 0.05;

        const double expectedTarget = peak / fill; // ≈ 588.235...

        double scale = 50.0; // start below target — will attack up

        // Run enough iterations for convergence from below
        for (int i = 0; i < 200; ++i) {
            scale = autoScaleStep(scale, peak, floor_uv, fill, attack, release);
        }

        // Should be very close to expectedTarget
        QVERIFY2(qAbs(scale - expectedTarget) < 0.01,
                 qPrintable(QString("scale %1 must converge to ~%2").arg(scale).arg(expectedTarget)));

        // Now test convergence from above (release path)
        double scaleHigh = 5000.0;
        for (int i = 0; i < 500; ++i) {
            scaleHigh = autoScaleStep(scaleHigh, peak, floor_uv, fill, attack, release);
        }

        QVERIFY2(qAbs(scaleHigh - expectedTarget) < 0.1,
                 qPrintable(QString("scale from high %1 must converge to ~%2").arg(scaleHigh).arg(expectedTarget)));
    }
};

QTEST_APPLESS_MAIN(TestAutoScale)
#include "test_autoscale.moc"
