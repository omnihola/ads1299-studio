#include "AnnotationStore.h"
#include "core/logging/Logger.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>
#include <cmath>
extern "C" {
#include "edflib.h"
}

namespace studio {

void AnnotationStore::add(double onsetSec, const QString& label)
{
    // Validate label: reject empty or whitespace-only
    if (label.trimmed().isEmpty()) {
        return;
    }

    // Clamp negative onsetSec to 0.0
    double validOnset = (onsetSec < 0.0) ? 0.0 : onsetSec;

    annotations_.append(Annotation{validOnset, label});
}

QVector<Annotation> AnnotationStore::all() const
{
    // Return a sorted copy (stable sort preserves insertion order for equal onsets)
    auto result = annotations_;
    std::stable_sort(result.begin(), result.end(),
                     [](const Annotation& a, const Annotation& b) {
                         return a.onsetSec < b.onsetSec;
                     });
    return result;
}

int AnnotationStore::count() const
{
    return annotations_.size();
}

void AnnotationStore::clear()
{
    annotations_.clear();
}

QJsonArray AnnotationStore::toJson() const
{
    QJsonArray result;
    auto sorted = all();
    for (const auto& annotation : sorted) {
        QJsonObject obj;
        obj["onset"] = annotation.onsetSec;
        obj["label"] = annotation.label;
        result.append(obj);
    }
    return result;
}

int AnnotationStore::writeToBdf(int edfHandle) const
{
    int written = 0;
    auto sorted = all();

    for (const auto& annotation : sorted) {
        // Convert onset from seconds to 100-nanosecond units
        long long onset100ns = static_cast<long long>(std::llround(annotation.onsetSec * 1e7));

        // Use duration = -1 (no duration)
        long long duration = -1;

        // Call edfwrite_annotation_utf8_hr
        int result = edfwrite_annotation_utf8_hr(edfHandle, onset100ns, duration,
                                                 annotation.label.toUtf8().constData());

        if (result < 0) {
            Logger::instance().log("ERROR", "annotation_write_failed",
                                   {{"annotation", annotation.label},
                                    {"onset", annotation.onsetSec}});
            return -1;
        }

        ++written;
    }

    return written;
}

} // namespace studio
