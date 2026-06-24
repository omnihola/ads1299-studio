# Architecture & Extension Guide

This document is for developers extending or maintaining ADS1299 Studio. For a
user-facing overview, build, and run instructions, see the top-level
[`README.md`](../README.md).

## Layers

```
ui/   — Qt widgets/views. Knows nothing about how data is produced.
app/  — SessionController: orchestration, threading, recording lifecycle, config.
core/ — pure logic: acquisition sources, DSP, device config, recording. No UI.
```

The hard boundary is **`core/acquisition/IDataSource.h`**. The UI and recording
layers depend only on the `EegFrame` stream coming through it — never on which
source produced it. This is what makes the simulator, the serial backend, and
the libusb MMB0 backend interchangeable.

## The data flow

```
IDataSource ──frames──▶ SessionController ──┬─▶ displayBuffer ──▶ MonitorView/Spectrum/...
 (worker thread)         (seq-gap check,     │   (lock-free ring)
                          metrics, config)   └─▶ Recorder (writer thread) ─▶ BDF/CSV/JSON
```

- A source emits `EegFrame`s on a **worker thread**.
- `SessionController` checks sequence continuity (counts/annotates dropped
  samples), updates metrics, pushes raw frames to a lock-free display ring, and
  — when recording — hands the **raw** frame to the `Recorder` on its own
  **writer thread**.
- Views pull from the display ring on a render timer; they never block the
  acquisition path.

## Extension point: adding a new data source

This is the primary way to extend the app (e.g. a new transport, a file-replay
source, a different ADC). Steps:

1. **Implement `IDataSource`** (`core/acquisition/IDataSource.h`): subclass
   `QObject`, emit `framesReady(QVector<EegFrame>)` (or the existing frame
   signal), implement `start()`/`stop()`. Produce `EegFrame`s with raw 24-bit
   channel counts and the status word — **do not pre-scale to µV** (scaling and
   filtering happen downstream; recording stores raw).
2. **Mind thread affinity.** The source is moved to a worker thread by
   `SessionController`. Any `QTimer` it owns must be **parented to the source**
   (`new QTimer(this)`), not a value member — value-member timers don't migrate
   with `moveToThread` and fail with "Timers cannot be started from another
   thread." (See `SimulatedSource` for the canonical pattern.)
3. **Wire it in** `SessionController` (a `connectX()` method that swaps the
   source) and surface it in `MainWindow` (e.g. the Connect action). Source
   swapping tears down the old source on its stopped worker thread — follow the
   existing `setSource` path.
4. **Test headlessly.** Device-dependent tests `QSKIP` when no hardware is
   present (see the MMB0 tests); pure logic gets normal unit tests.

## Threading model

- **Acquisition** runs on a worker thread owned by `SessionController`.
- **Recording** runs on a separate writer thread; `Recorder::writeBatch` is a
  slot invoked via the event loop (it must be a slot, or queued invocation
  silently no-ops).
- **Teardown:** quit+join the thread *before* deleting a thread-affined object;
  then a direct `delete` is safe and silent (calling `moveToThread` on an object
  whose thread has stopped emits a misleading "two sets of Qt binaries" warning).

## Invariants (do not break these)

- **Recording stays raw.** Display filters (notch/band-pass) and µV scaling are
  applied only on the way to the screen. The bytes written to BDF/CSV are the
  unmodified device counts. This is enforced structurally — the recorder is fed
  from the raw frame, not the filtered display block.
- **Config is frozen while recording.** `applyConfig` is a no-op during
  recording and the Acquisition controls are disabled, so the BDF time base and
  scaling can't change mid-file.
- **Per-session integrity counters reset on `startStreaming`** (dropped samples,
  expected sequence, first-frame flag) — they are per-session, not cumulative.
- **Dropped samples are padded and annotated**, keeping the recording's time
  base wall-clock-correct (CSV `drop_pad` rows + BDF annotations).

## Persistence

User preferences (window geometry, output folder, sample rate, gain) are stored
via `QSettings`. All keys live in **`src/app/SettingsKeys.h`** (`studio::settings::k*`)
so a save site and a load site can't drift apart — add new keys there.

## Testing

- `ctest --test-dir build` runs the full suite (pure logic + headless UI).
- Headless smoke: `QT_QPA_PLATFORM=offscreen ./build/ads1299-studio --selftest`
  (exit 0) and `--shot <png>` for a screenshot. The GPU waveform renderer has no
  GL context under `offscreen` and degrades to a placeholder, so **visual GL
  output must be verified on real hardware**, not in CI.

## Where things live

See the source tree in [`README.md`](../README.md#architecture). In short:
`core/acquisition` (+ `mmb0/`), `core/dsp`, `core/device`, `core/recording`,
`app/`, `ui/` (+ `ui/gl/`, `ui/theme/`).
