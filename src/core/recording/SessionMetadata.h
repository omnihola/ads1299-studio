#pragma once

#include <array>
#include <QString>
#include <QJsonObject>

namespace studio {

class SessionMetadata
{
public:
    SessionMetadata() = default;

    // Const accessors
    QString subjectId() const;
    QString montage() const;
    int sampleRate() const;
    std::array<int, 8> gainPerChannel() const;
    QString operatorNotes() const;
    QString startTimeUtc() const;
    QJsonObject deviceConfigSnapshot() const;

    // Immutable updates: return a NEW copy, never mutate this
    SessionMetadata withSubjectId(const QString& value) const;
    SessionMetadata withMontage(const QString& value) const;
    SessionMetadata withSampleRate(int value) const;
    SessionMetadata withGainPerChannel(const std::array<int, 8>& value) const;
    SessionMetadata withNotes(const QString& value) const;
    SessionMetadata withStartTimeUtc(const QString& value) const;
    SessionMetadata withDeviceConfigSnapshot(const QJsonObject& value) const;

    // JSON serialization
    QJsonObject toJson() const;
    static SessionMetadata fromJson(const QJsonObject& json);

private:
    QString subject_id_;
    QString montage_;
    int sample_rate_{250};
    std::array<int, 8> gain_per_channel_{24, 24, 24, 24, 24, 24, 24, 24};
    QString operator_notes_;
    QString start_time_utc_;
    QJsonObject device_config_snapshot_;
};

} // namespace studio
