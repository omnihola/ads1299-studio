// tests/test_waveformtransform.cpp
//
// Unit tests for studio::gl::WaveformTransform pure helpers.
// Headless – no OpenGL context required.

#include <QtTest>
#include "ui/gl/WaveformTransform.h"

using namespace studio::gl;

class TestWaveformTransform : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // X mapping
    // -----------------------------------------------------------------------
    void sampleIndexX()
    {
        // p=0 (oldest) must map to -1
        QCOMPARE(sampleIndexToNdcX(0, 100), -1.0f);

        // p=W-1 (newest) must map to +1
        QCOMPARE(sampleIndexToNdcX(99, 100), 1.0f);

        // midpoint of 101 samples → index 50 → exactly 0
        const float mid = sampleIndexToNdcX(50, 101);
        QVERIFY2(qAbs(mid) < 1e-6f, "midpoint should be ~0");

        // W <= 1 must return -1 without crashing (no divide-by-zero)
        QCOMPARE(sampleIndexToNdcX(0, 1), -1.0f);
        QCOMPARE(sampleIndexToNdcX(0, 0), -1.0f);
        QCOMPARE(sampleIndexToNdcX(5, -3), -1.0f);
    }

    // -----------------------------------------------------------------------
    // Channel centres
    // -----------------------------------------------------------------------
    void channelCenters()
    {
        constexpr int N = 8;

        // channel 0 centre: 1 - 1/8 = 0.875
        const float c0 = channelCenterNdcY(0, N);
        QVERIFY2(qAbs(c0 - 0.875f) < 1e-6f, "ch0 centre should be 0.875");

        // channel 7 centre: 1 - 15/8 = -0.875
        const float c7 = channelCenterNdcY(7, N);
        QVERIFY2(qAbs(c7 - (-0.875f)) < 1e-6f, "ch7 centre should be -0.875");

        // All centres strictly descending
        float prev = channelCenterNdcY(0, N);
        for (int c = 1; c < N; ++c) {
            const float cur = channelCenterNdcY(c, N);
            QVERIFY2(cur < prev, "centres must be strictly descending");
            prev = cur;
        }

        // Formula: centre_c = 1 - (2c+1)/N
        for (int c = 0; c < N; ++c) {
            const float expected = 1.0f - static_cast<float>(2 * c + 1) / static_cast<float>(N);
            const float actual   = channelCenterNdcY(c, N);
            QVERIFY2(qAbs(actual - expected) < 1e-6f, "formula mismatch");
        }
    }

    // -----------------------------------------------------------------------
    // µV scaling
    // -----------------------------------------------------------------------
    void microvoltsScaling()
    {
        constexpr int    N             = 8;
        constexpr double UV_PER_DIV    = 200.0;
        constexpr double DIVS_HALF     = 1.5;
        constexpr double FULL_SCALE_UV = UV_PER_DIV * DIVS_HALF; // 300 µV

        // uv == 0  →  y == channelCenterNdcY(c, N)  for every channel
        for (int c = 0; c < N; ++c) {
            const float y      = microvoltsToNdcY(0.0, c, N, UV_PER_DIV, DIVS_HALF);
            const float centre = channelCenterNdcY(c, N);
            QVERIFY2(qAbs(y - centre) < 1e-6f, "zero µV must map to lane centre");
        }

        // positive µV increases y (moves upward in NDC)
        for (int c = 0; c < N; ++c) {
            const float yPos = microvoltsToNdcY( 50.0, c, N, UV_PER_DIV, DIVS_HALF);
            const float yNeg = microvoltsToNdcY(-50.0, c, N, UV_PER_DIV, DIVS_HALF);
            QVERIFY2(yPos > yNeg, "positive µV must produce higher NDC y");
        }

        // uv == +fullScaleUv → y == centre + laneHeight/2  (top edge)
        for (int c = 0; c < N; ++c) {
            const float centre    = channelCenterNdcY(c, N);
            const float laneHalf  = 1.0f / static_cast<float>(N); // (2/N)/2
            const float topEdge   = centre + laneHalf;

            const float y = microvoltsToNdcY(FULL_SCALE_UV, c, N, UV_PER_DIV, DIVS_HALF);
            QVERIFY2(qAbs(y - topEdge) < 1e-5f, "full-scale µV must reach top lane edge");
        }

        // uv == -fullScaleUv → y == centre - laneHeight/2  (bottom edge)
        for (int c = 0; c < N; ++c) {
            const float centre   = channelCenterNdcY(c, N);
            const float laneHalf = 1.0f / static_cast<float>(N);
            const float botEdge  = centre - laneHalf;

            const float y = microvoltsToNdcY(-FULL_SCALE_UV, c, N, UV_PER_DIV, DIVS_HALF);
            QVERIFY2(qAbs(y - botEdge) < 1e-5f, "negative full-scale µV must reach bottom lane edge");
        }
    }

    // -----------------------------------------------------------------------
    // Lanes do not overlap at full scale
    // -----------------------------------------------------------------------
    void lanesDoNotOverlapAtFullScale()
    {
        constexpr int    N             = 8;
        constexpr double UV_PER_DIV    = 200.0;
        constexpr double DIVS_HALF     = 1.5;
        constexpr double FULL_SCALE_UV = UV_PER_DIV * DIVS_HALF;

        for (int c = 0; c < N; ++c) {
            const float centre   = channelCenterNdcY(c, N);
            const float laneHalf = 1.0f / static_cast<float>(N);

            const float yTop = microvoltsToNdcY( FULL_SCALE_UV, c, N, UV_PER_DIV, DIVS_HALF);
            const float yBot = microvoltsToNdcY(-FULL_SCALE_UV, c, N, UV_PER_DIV, DIVS_HALF);

            // Both extremes must stay within the lane bounds
            QVERIFY2(yTop <= centre + laneHalf + 1e-5f, "top edge must not exceed lane boundary");
            QVERIFY2(yBot >= centre - laneHalf - 1e-5f, "bottom edge must not go below lane boundary");

            // A full-scale point in channel c must not cross into the adjacent
            // channel's lane centre.
            if (c + 1 < N) {
                const float nextCentre = channelCenterNdcY(c + 1, N);
                // Channel c's bottom edge must be >= next channel's centre
                QVERIFY2(yBot >= nextCentre - 1e-5f,
                         "full-scale bottom of ch c must not go past centre of ch c+1");
            }
            if (c > 0) {
                const float prevCentre = channelCenterNdcY(c - 1, N);
                // Channel c's top edge must be <= prev channel's centre
                QVERIFY2(yTop <= prevCentre + 1e-5f,
                         "full-scale top of ch c must not go past centre of ch c-1");
            }
        }
    }
};

QTEST_MAIN(TestWaveformTransform)
#include "test_waveformtransform.moc"
