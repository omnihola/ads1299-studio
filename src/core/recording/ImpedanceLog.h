#pragma once
// src/core/recording/ImpedanceLog.h
//
// ImpedanceLog — per-channel electrode impedance history.
//
// Records timestamped snapshots of per-channel kΩ values together with the
// ADS1299 lead-off comparator bytes (LOFF_STATP / LOFF_STATN). Provides
// JSON and CSV export. Has no Qt Widgets dependency — lives in studio_core.
//
// ImpedanceStatus threshold rules (exact):
//   if (leadOff || kOhm >= 50.0) → Error
//   else if (kOhm >= 10.0)       → Warn
//   else                         → Ok

#include <array>
#include <cstdint>
#include <QVector>
#include <QJsonArray>
#include <QString>

namespace studio {

enum class ImpedanceStatus { Ok, Warn, Error };

struct ImpedanceSample {
    double               tSec  = 0.0;
    std::array<double,8> kOhm{};
    uint8_t              statP = 0;
    uint8_t              statN = 0;
};

class ImpedanceLog {
public:
    // Record one impedance snapshot.
    void record(double tSec,
                const std::array<double,8>& kOhm,
                uint8_t statP,
                uint8_t statN);

    // Return all recorded samples (insertion order).
    QVector<ImpedanceSample> all() const;

    // Number of recorded samples.
    int count() const;

    // Discard all samples.
    void clear();

    // Serialize to JSON array: [{"t":<sec>,"kohm":[…8],"statP":<int>,"statN":<int>},…]
    QJsonArray toJson() const;

    // Write to CSV file. Header: t_sec,ch0_kohm,…,ch7_kohm,statP,statN
    // Returns false on open failure (also logs error); true on success.
    bool writeCsv(const QString& path) const;

    // Pure classifier — does not depend on instance state.
    // leadOff=true OR kOhm>=50 → Error; kOhm>=10 → Warn; else Ok.
    static ImpedanceStatus statusFor(double kOhm, bool leadOff);

private:
    QVector<ImpedanceSample> samples_;
};

} // namespace studio
