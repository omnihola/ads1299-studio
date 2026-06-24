#pragma once
// src/core/recording/SessionScanner.h
//
// SessionScanner — scans a directory for *.meta.json files produced by the
// Recorder and parses each into a SessionInfo summary struct.

#include <QString>
#include <QVector>

namespace studio {

// ── SessionInfo ───────────────────────────────────────────────────────────────

struct SessionInfo {
    QString basePath;       // directory/<base>  (no extension)
    QString metaPath;       // the .meta.json path
    QString subjectId;
    QString startTimeUtc;   // ISO 8601 string as stored in meta.json
    int     sampleRate   = 0;
    qint64  totalSamples = 0;
    double  durationSec  = 0.0;  // totalSamples / sampleRate (0 when sampleRate == 0)
    bool    hasBdf       = false; // <basePath>.bdf exists
    bool    hasCsv       = false; // <basePath>.csv exists
};

// ── SessionScanner ────────────────────────────────────────────────────────────

class SessionScanner
{
public:
    // Scan a directory for *.meta.json files, parse each into a SessionInfo.
    // Entries with unparseable or missing JSON are silently skipped.
    // Result is sorted by startTimeUtc DESCENDING (newest first).
    static QVector<SessionInfo> scan(const QString& dir);

    // Parse a single .meta.json file.
    // basePath is derived from metaJsonPath by removing the trailing ".meta.json".
    // Returns a default-constructed SessionInfo (subjectId empty, sampleRate 0, etc.)
    // if the file cannot be read or the JSON is invalid.
    static SessionInfo parseMeta(const QString& metaJsonPath);
};

} // namespace studio
