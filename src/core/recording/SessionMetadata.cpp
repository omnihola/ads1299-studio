#include "SessionMetadata.h"
#include <QJsonArray>

namespace studio {

QString SessionMetadata::subjectId() const {
    return subject_id_;
}

QString SessionMetadata::montage() const {
    return montage_;
}

int SessionMetadata::sampleRate() const {
    return sample_rate_;
}

std::array<int, 8> SessionMetadata::gainPerChannel() const {
    return gain_per_channel_;
}

QString SessionMetadata::operatorNotes() const {
    return operator_notes_;
}

QString SessionMetadata::startTimeUtc() const {
    return start_time_utc_;
}

QJsonObject SessionMetadata::deviceConfigSnapshot() const {
    return device_config_snapshot_;
}

// Immutable with* methods - copy, mutate copy, return new object
SessionMetadata SessionMetadata::withSubjectId(const QString& value) const {
    SessionMetadata copy = *this;
    copy.subject_id_ = value;
    return copy;
}

SessionMetadata SessionMetadata::withMontage(const QString& value) const {
    SessionMetadata copy = *this;
    copy.montage_ = value;
    return copy;
}

SessionMetadata SessionMetadata::withSampleRate(int value) const {
    SessionMetadata copy = *this;
    copy.sample_rate_ = value;
    return copy;
}

SessionMetadata SessionMetadata::withGainPerChannel(const std::array<int, 8>& value) const {
    SessionMetadata copy = *this;
    copy.gain_per_channel_ = value;
    return copy;
}

SessionMetadata SessionMetadata::withNotes(const QString& value) const {
    SessionMetadata copy = *this;
    copy.operator_notes_ = value;
    return copy;
}

SessionMetadata SessionMetadata::withStartTimeUtc(const QString& value) const {
    SessionMetadata copy = *this;
    copy.start_time_utc_ = value;
    return copy;
}

SessionMetadata SessionMetadata::withDeviceConfigSnapshot(const QJsonObject& value) const {
    SessionMetadata copy = *this;
    copy.device_config_snapshot_ = value;
    return copy;
}

QJsonObject SessionMetadata::toJson() const {
    QJsonObject json;

    json["subjectId"] = subject_id_;
    json["montage"] = montage_;
    json["sampleRate"] = sample_rate_;
    json["operatorNotes"] = operator_notes_;
    json["startTimeUtc"] = start_time_utc_;

    // Serialize gain array as JSON array
    QJsonArray gainArray;
    for (int i = 0; i < 8; ++i) {
        gainArray.append(gain_per_channel_[i]);
    }
    json["gainPerChannel"] = gainArray;

    // Serialize device config snapshot as-is
    json["deviceConfigSnapshot"] = device_config_snapshot_;

    return json;
}

SessionMetadata SessionMetadata::fromJson(const QJsonObject& json) {
    SessionMetadata metadata;

    metadata.subject_id_ = json["subjectId"].toString("");
    metadata.montage_ = json["montage"].toString("");
    metadata.sample_rate_ = json["sampleRate"].toInt(250);
    metadata.operator_notes_ = json["operatorNotes"].toString("");
    metadata.start_time_utc_ = json["startTimeUtc"].toString("");

    // Deserialize gain array
    QJsonArray gainArray = json["gainPerChannel"].toArray();
    if (gainArray.size() == 8) {
        for (int i = 0; i < 8; ++i) {
            metadata.gain_per_channel_[i] = gainArray[i].toInt(24);
        }
    }
    // If gainArray is not present or has wrong size, keep defaults (all 24)

    // Deserialize device config snapshot
    metadata.device_config_snapshot_ = json["deviceConfigSnapshot"].toObject();

    return metadata;
}

} // namespace studio
