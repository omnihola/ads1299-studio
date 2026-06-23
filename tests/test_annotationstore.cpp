#include <QtTest>
#include "core/recording/AnnotationStore.h"

using studio::AnnotationStore;
using studio::Annotation;

class TestAnnotationStore : public QObject {
    Q_OBJECT
private slots:
    void ordering_and_count() {
        AnnotationStore store;

        // Add three out-of-order annotations
        store.add(5.0, "c");
        store.add(1.0, "a");
        store.add(3.0, "b");

        // Verify count
        QCOMPARE(store.count(), 3);

        // Verify all() returns sorted by onset ascending
        auto annotations = store.all();
        QCOMPARE(annotations.size(), 3);

        QCOMPARE(annotations[0].onsetSec, 1.0);
        QCOMPARE(annotations[0].label, QString("a"));

        QCOMPARE(annotations[1].onsetSec, 3.0);
        QCOMPARE(annotations[1].label, QString("b"));

        QCOMPARE(annotations[2].onsetSec, 5.0);
        QCOMPARE(annotations[2].label, QString("c"));
    }

    void to_json() {
        AnnotationStore store;

        // Add annotations
        store.add(1.0, "a");
        store.add(3.0, "b");
        store.add(5.0, "c");

        // Get JSON
        auto json = store.toJson();

        // Verify it's an array with 3 elements
        QCOMPARE(json.size(), 3);

        // Verify first element
        auto first = json[0].toObject();
        QCOMPARE(first["onset"].toDouble(), 1.0);
        QCOMPARE(first["label"].toString(), QString("a"));
    }

    void validation_negative_onset() {
        AnnotationStore store;

        // Add with negative onset (should be clamped to 0.0)
        store.add(-2.0, "neg");

        // Verify it was added with onset clamped to 0.0
        QCOMPARE(store.count(), 1);
        auto annotations = store.all();
        QCOMPARE(annotations[0].onsetSec, 0.0);
        QCOMPARE(annotations[0].label, QString("neg"));
    }

    void validation_empty_label() {
        AnnotationStore store;

        // Add with empty label (should be no-op)
        store.add(2.0, "");
        QCOMPARE(store.count(), 0);

        // Add with whitespace-only label (should be no-op)
        store.add(2.5, "   ");
        QCOMPARE(store.count(), 0);
    }

    void clear() {
        AnnotationStore store;

        // Add some annotations
        store.add(1.0, "a");
        store.add(2.0, "b");
        QCOMPARE(store.count(), 2);

        // Clear
        store.clear();
        QCOMPARE(store.count(), 0);
        QCOMPARE(store.all().size(), 0);
    }
};

QTEST_MAIN(TestAnnotationStore)
#include "test_annotationstore.moc"
