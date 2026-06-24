#pragma once
// src/core/recording/SessionExporter.h
//
// SessionExporter — copies all sibling files produced by a recording session
// (.bdf, .csv, .meta.json, .impedance.csv) to a chosen destination directory.

#include <QString>
#include <QStringList>

namespace studio {

class SessionExporter
{
public:
    // Returns all existing sibling files for a recording base path.
    // Checks: <basePath>.bdf, <basePath>.csv, <basePath>.meta.json,
    //         <basePath>.impedance.csv — returns only those that exist.
    static QStringList relatedFiles(const QString& basePath);

    // Copies relatedFiles(basePath) into destDir.
    // Fills `copied` with the destination paths on success.
    // Returns false and sets `error` on any failure:
    //   - no source files found
    //   - destDir does not exist or is not writable
    //   - any individual file copy fails
    static bool exportTo(const QString& basePath,
                         const QString& destDir,
                         QStringList&  copied,
                         QString&      error);
};

} // namespace studio
