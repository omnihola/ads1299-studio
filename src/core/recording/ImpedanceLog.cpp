// src/core/recording/ImpedanceLog.cpp
#include "core/recording/ImpedanceLog.h"
#include "core/logging/Logger.h"

#include <QFile>
#include <QTextStream>
#include <QJsonObject>

namespace studio {

// ---------------------------------------------------------------------------
// ImpedanceLog::statusFor — pure threshold classifier
// ---------------------------------------------------------------------------

ImpedanceStatus ImpedanceLog::statusFor(double kOhm, bool leadOff)
{
    if (leadOff || kOhm >= 50.0) return ImpedanceStatus::Error;
    if (kOhm >= 10.0)            return ImpedanceStatus::Warn;
    return ImpedanceStatus::Ok;
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

void ImpedanceLog::record(double tSec,
                           const std::array<double,8>& kOhm,
                           uint8_t statP,
                           uint8_t statN)
{
    ImpedanceSample s;
    s.tSec  = tSec;
    s.kOhm  = kOhm;
    s.statP = statP;
    s.statN = statN;
    samples_.append(s);
}

QVector<ImpedanceSample> ImpedanceLog::all() const
{
    return samples_;
}

int ImpedanceLog::count() const
{
    return samples_.size();
}

void ImpedanceLog::clear()
{
    samples_.clear();
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

QJsonArray ImpedanceLog::toJson() const
{
    QJsonArray arr;
    for (const ImpedanceSample& s : samples_) {
        QJsonArray kohmArr;
        for (double v : s.kOhm) {
            kohmArr.append(v);
        }
        QJsonObject obj;
        obj["t"]     = s.tSec;
        obj["kohm"]  = kohmArr;
        obj["statP"] = static_cast<int>(s.statP);
        obj["statN"] = static_cast<int>(s.statN);
        arr.append(obj);
    }
    return arr;
}

bool ImpedanceLog::writeCsv(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::instance().log("error", "ImpedanceLog.writeCsv.openFailed",
                               QJsonObject{{"path", path}});
        return false;
    }

    QTextStream ts(&f);
    ts << "t_sec,"
       << "ch0_kohm,ch1_kohm,ch2_kohm,ch3_kohm,"
       << "ch4_kohm,ch5_kohm,ch6_kohm,ch7_kohm,"
       << "statP,statN\n";

    for (const ImpedanceSample& s : samples_) {
        ts << s.tSec;
        for (double v : s.kOhm) {
            ts << ',' << v;
        }
        ts << ',' << static_cast<int>(s.statP)
           << ',' << static_cast<int>(s.statN)
           << '\n';
    }
    return true;
}

} // namespace studio
