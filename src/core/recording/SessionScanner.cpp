// src/core/recording/SessionScanner.cpp
//
// See SessionScanner.h for the public contract.

#include "core/recording/SessionScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace studio {

// ── parseMeta ────────────────────────────────────────────────────────────────

SessionInfo SessionScanner::parseMeta(const QString& metaJsonPath)
{
    SessionInfo info;

    // Derive basePath by stripping the ".meta.json" suffix.
    constexpr auto kSuffix = ".meta.json";
    if (!metaJsonPath.endsWith(kSuffix)) {
        return info;  // unexpected — return empty
    }
    info.metaPath = metaJsonPath;
    info.basePath = metaJsonPath.chopped(static_cast<int>(qstrlen(kSuffix)));

    // Read and parse the JSON file.
    QFile f(metaJsonPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return info;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Signal an unparseable file by leaving sampleRate == 0 and clearing basePath.
        info.basePath.clear();
        return info;
    }

    const QJsonObject obj = doc.object();

    // Pull the fields written by Recorder::writeMetaJson / SessionMetadata::toJson.
    // Top-level keys (Recorder enriches meta_ with these directly on obj):
    //   "subjectId"              — from SessionMetadata::toJson
    //   "sampleRate"             — int, written by Recorder (mirrors meta.sampleRate())
    //   "totalSamplesPerChannel" — qint64, written by Recorder
    //   "recordingStartUtc"      — ISO string, written by Recorder
    info.subjectId    = obj["subjectId"].toString();
    info.sampleRate   = obj["sampleRate"].toInt(0);
    info.totalSamples = static_cast<qint64>(obj["totalSamplesPerChannel"].toDouble(0));
    info.startTimeUtc = obj["recordingStartUtc"].toString();

    // Fallback: if recordingStartUtc is absent, try the SessionMetadata field
    // "startTimeUtc" (written by SessionMetadata::toJson as well).
    if (info.startTimeUtc.isEmpty()) {
        info.startTimeUtc = obj["startTimeUtc"].toString();
    }

    // Compute duration (guard against divide-by-zero).
    info.durationSec = (info.sampleRate > 0)
                           ? static_cast<double>(info.totalSamples) / static_cast<double>(info.sampleRate)
                           : 0.0;

    // Check for sidecar files.
    info.hasBdf = QFile::exists(info.basePath + ".bdf");
    info.hasCsv = QFile::exists(info.basePath + ".csv");

    return info;
}

// ── scan ─────────────────────────────────────────────────────────────────────

QVector<SessionInfo> SessionScanner::scan(const QString& dir)
{
    QDir d(dir);
    const QStringList metaFiles = d.entryList({"*.meta.json"}, QDir::Files);

    QVector<SessionInfo> results;
    results.reserve(metaFiles.size());

    for (const QString& name : metaFiles) {
        const QString fullPath = d.absoluteFilePath(name);
        SessionInfo info = parseMeta(fullPath);

        // Skip entries where basePath was cleared (parse failure).
        if (info.basePath.isEmpty()) {
            continue;
        }

        results.append(info);
    }

    // Sort descending by startTimeUtc (ISO 8601 strings sort lexicographically).
    std::sort(results.begin(), results.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.startTimeUtc > b.startTimeUtc;
              });

    return results;
}

} // namespace studio
