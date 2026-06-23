#pragma once
#include "core/acquisition/IDataSource.h"
#include <QTimer>
#include <random>

namespace studio {

class SimulatedSource : public IDataSource {
    Q_OBJECT

public:
    explicit SimulatedSource(uint32_t seed = 42, QObject* parent = nullptr);

    void start() override;
    void stop() override;
    bool isRunning() const override { return running_; }

    SourceCapabilities capabilities() const override;
    void setSampleRate(int sps) override;

    EegFrameBatch generateBatch(int nSamples);  // pure, testable

private slots:
    void tick();

private:
    QTimer timer_;
    std::mt19937 rng_;
    bool running_ = false;
    int sps_ = 250;
    uint32_t seq_ = 0;
    double t_ = 0.0;
};

}  // namespace studio
