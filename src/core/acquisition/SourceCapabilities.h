// src/core/acquisition/SourceCapabilities.h
#pragma once
#include <QString>
#include <QVector>

namespace studio {
struct SourceCapabilities {
    int channels = 8;
    QVector<int> sampleRates = {250, 500, 1000, 2000, 4000, 8000, 16000};
    QString name = "Unknown";
};
}
