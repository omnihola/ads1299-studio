// src/core/recording/SessionExporter.cpp
//
// See SessionExporter.h for the public contract.

#include "core/recording/SessionExporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace studio {

namespace {
// Candidate extensions in the order they are typically created.
const QStringList kExtensions{
    ".bdf",
    ".csv",
    ".meta.json",
    ".impedance.csv",
};
} // namespace

QStringList SessionExporter::relatedFiles(const QString& basePath)
{
    QStringList found;
    for (const QString& ext : kExtensions) {
        const QString candidate = basePath + ext;
        if (QFile::exists(candidate)) {
            found.append(candidate);
        }
    }
    return found;
}

bool SessionExporter::exportTo(const QString& basePath,
                                const QString& destDir,
                                QStringList&   copied,
                                QString&       error)
{
    copied.clear();

    // Validate destination directory
    const QFileInfo destInfo(destDir);
    if (!destInfo.isDir() || !destInfo.isWritable()) {
        error = QString("Destination folder does not exist or is not writable: %1").arg(destDir);
        return false;
    }

    const QStringList sources = relatedFiles(basePath);
    if (sources.isEmpty()) {
        const QFileInfo baseInfo(basePath);
        error = QString("No session files found for %1").arg(baseInfo.fileName());
        return false;
    }

    for (const QString& srcPath : sources) {
        const QString fileName = QFileInfo(srcPath).fileName();
        const QString destPath = QDir(destDir).filePath(fileName);

        // QFile::copy fails if the destination exists — remove first
        if (QFile::exists(destPath)) {
            if (!QFile::remove(destPath)) {
                error = QString("Could not overwrite existing file: %1").arg(destPath);
                return false;
            }
        }

        if (!QFile::copy(srcPath, destPath)) {
            error = QString("Failed to copy %1 to %2").arg(srcPath, destPath);
            return false;
        }

        copied.append(destPath);
    }

    return true;
}

} // namespace studio
