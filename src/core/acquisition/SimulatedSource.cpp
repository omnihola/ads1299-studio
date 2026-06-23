#include "core/acquisition/SimulatedSource.h"
#include "core/Constants.h"
#include <cmath>

namespace studio {

SimulatedSource::SimulatedSource(uint32_t seed, QObject* parent)
    : IDataSource(parent), rng_(seed) {
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &SimulatedSource::tick);
}

SourceCapabilities SimulatedSource::capabilities() const {
    SourceCapabilities c;
    c.name = "Simulated EEG";
    return c;
}

void SimulatedSource::setSampleRate(int sps) {
    sps_ = sps;
}

EegFrameBatch SimulatedSource::generateBatch(int n) {
    EegFrameBatch batch;
    batch.reserve(n);

    std::normal_distribution<double> noise(0.0, 4.0);  // µV-scale noise
    const double dt = 1.0 / sps_;
    const double lsbUv = (kVref / (24.0 * kAdcFullScale)) * 1e6;

    for (int i = 0; i < n; ++i) {
        EegFrame f;
        f.seq = seq_++;

        for (int c = 0; c < kNumChannels; ++c) {
            double uv = 20.0 * std::sin(2 * M_PI * 10 * t_ + c)        // alpha
                      + 8.0 * std::sin(2 * M_PI * 20 * t_ + c * 0.5)   // beta
                      + noise(rng_);

            // µV -> counts at gain 24
            f.ch[c] = int32_t(uv / lsbUv);
        }

        batch.push_back(f);
        t_ += dt;
    }

    return batch;
}

void SimulatedSource::tick() {
    const int batchSamples = std::max(1, sps_ / 50);  // ~20ms batches
    emit framesReady(generateBatch(batchSamples));
}

void SimulatedSource::start() {
    if (running_) return;
    running_ = true;
    emit runningChanged(true);
    timer_->start(20);
}

void SimulatedSource::stop() {
    if (!running_) return;
    running_ = false;
    timer_->stop();
    emit runningChanged(false);
}

}  // namespace studio
