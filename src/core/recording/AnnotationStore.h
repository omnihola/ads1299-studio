#pragma once

#include <QString>
#include <QVector>
#include <QJsonArray>

namespace studio {

struct Annotation {
    double onsetSec;
    QString label;
};

class AnnotationStore {
public:
    // Add an annotation. Empty/blank labels are ignored. Negative onsetSec is clamped to 0.0.
    void add(double onsetSec, const QString& label);

    // Returns all annotations sorted by onsetSec ascending (stable).
    QVector<Annotation> all() const;

    // Returns the number of annotations.
    int count() const;

    // Clears all annotations.
    void clear();

    // Serializes annotations to JSON: [{"onset":<sec>,"label":<str>}, ...]
    QJsonArray toJson() const;

    // Writes all annotations to an open BDF+ handle.
    // Returns number written; returns -1 on EDFlib error (also logs error).
    int writeToBdf(int edfHandle) const;

private:
    QVector<Annotation> annotations_;
};

} // namespace studio
