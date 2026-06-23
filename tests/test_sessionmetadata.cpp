#include <QtTest>
#include "core/recording/SessionMetadata.h"

using studio::SessionMetadata;

class TestSessionMetadata : public QObject {
    Q_OBJECT
private slots:
    void roundtrip_json_equality() {
        // Build device config
        QJsonObject config;
        config["CONFIG1"] = 246;

        // Build metadata via with* chaining
        auto original = SessionMetadata()
            .withSubjectId("S01")
            .withMontage("10-20")
            .withSampleRate(500)
            .withNotes("rest EEG")
            .withStartTimeUtc("2026-01-01T00:00:00Z")
            .withDeviceConfigSnapshot(config);

        // Serialize to JSON
        auto json = original.toJson();

        // Deserialize back from JSON
        auto restored = SessionMetadata::fromJson(json);

        // Assert all fields equal
        QCOMPARE(restored.subjectId(), QString("S01"));
        QCOMPARE(restored.montage(), QString("10-20"));
        QCOMPARE(restored.sampleRate(), 500);
        QCOMPARE(restored.operatorNotes(), QString("rest EEG"));
        QCOMPARE(restored.startTimeUtc(), QString("2026-01-01T00:00:00Z"));
        QCOMPARE(restored.deviceConfigSnapshot(), config);

        // Verify gain array is present (should be all 24 by default)
        auto gains = restored.gainPerChannel();
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(gains[i], 24);
        }
    }

    void immutability_with_subject_id() {
        // Create a metadata object
        SessionMetadata a;
        QVERIFY(a.subjectId().isEmpty());

        // Call withSubjectId - should return a NEW object
        SessionMetadata b = a.withSubjectId("X");

        // Original should remain unchanged
        QVERIFY(a.subjectId().isEmpty());

        // New object should have the updated value
        QCOMPARE(b.subjectId(), QString("X"));
    }

    void immutability_with_sample_rate() {
        // Create a metadata object with default sample rate
        SessionMetadata a;
        QCOMPARE(a.sampleRate(), 250);

        // Call withSampleRate - should return a NEW object
        SessionMetadata b = a.withSampleRate(500);

        // Original should remain unchanged
        QCOMPARE(a.sampleRate(), 250);

        // New object should have the updated value
        QCOMPARE(b.sampleRate(), 500);
    }

    void immutability_with_gain_per_channel() {
        // Create a metadata object with default gains
        SessionMetadata a;
        auto default_gains = a.gainPerChannel();
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(default_gains[i], 24);
        }

        // Create new gains array
        std::array<int, 8> new_gains{};
        for (int i = 0; i < 8; ++i) {
            new_gains[i] = 12 + i;
        }

        // Call withGainPerChannel - should return a NEW object
        SessionMetadata b = a.withGainPerChannel(new_gains);

        // Original should remain unchanged
        auto original_gains = a.gainPerChannel();
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(original_gains[i], 24);
        }

        // New object should have the updated values
        auto updated_gains = b.gainPerChannel();
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(updated_gains[i], 12 + i);
        }
    }

    void json_defaults_missing_keys() {
        // Create a minimal JSON with only a few fields
        QJsonObject minimal;
        minimal["subjectId"] = "S02";
        minimal["sampleRate"] = 1000;

        // Deserialize from minimal JSON
        auto metadata = SessionMetadata::fromJson(minimal);

        // Check that defaults are applied for missing keys
        QCOMPARE(metadata.subjectId(), QString("S02"));
        QCOMPARE(metadata.sampleRate(), 1000);
        QCOMPARE(metadata.montage(), QString(""));
        QCOMPARE(metadata.operatorNotes(), QString(""));
        QCOMPARE(metadata.startTimeUtc(), QString(""));

        // Gain array should default to all 24
        auto gains = metadata.gainPerChannel();
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(gains[i], 24);
        }

        // Device config snapshot should be empty object
        QJsonObject emptyObj;
        QCOMPARE(metadata.deviceConfigSnapshot(), emptyObj);
    }

    void json_roundtrip_with_full_gain_array() {
        // Create metadata with custom gains
        std::array<int, 8> gains{1, 2, 3, 4, 5, 6, 7, 8};
        auto original = SessionMetadata()
            .withSubjectId("S03")
            .withGainPerChannel(gains);

        // Roundtrip through JSON
        auto json = original.toJson();
        auto restored = SessionMetadata::fromJson(json);

        // Verify all 8 gain values are preserved
        auto restored_gains = restored.gainPerChannel();
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(restored_gains[i], i + 1);
        }
    }

    void json_roundtrip_with_device_config() {
        // Create a device config snapshot
        QJsonObject config;
        config["device"] = "ADS1299";
        config["firmware"] = "v1.0.0";
        config["calibration"] = 123;

        auto original = SessionMetadata()
            .withSubjectId("S04")
            .withDeviceConfigSnapshot(config);

        // Roundtrip through JSON
        auto json = original.toJson();
        auto restored = SessionMetadata::fromJson(json);

        // Verify the nested object is preserved
        QCOMPARE(restored.deviceConfigSnapshot(), config);
    }

    void chaining_multiple_with_calls() {
        auto metadata = SessionMetadata()
            .withSubjectId("S05")
            .withMontage("10-20")
            .withSampleRate(256)
            .withNotes("test session")
            .withStartTimeUtc("2026-06-23T12:34:56Z");

        QCOMPARE(metadata.subjectId(), QString("S05"));
        QCOMPARE(metadata.montage(), QString("10-20"));
        QCOMPARE(metadata.sampleRate(), 256);
        QCOMPARE(metadata.operatorNotes(), QString("test session"));
        QCOMPARE(metadata.startTimeUtc(), QString("2026-06-23T12:34:56Z"));
    }
};

QTEST_MAIN(TestSessionMetadata)
#include "test_sessionmetadata.moc"
