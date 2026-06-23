# ADS1299 Studio — Design Spec

**Date:** 2026-06-23
**Status:** Approved (brainstorming complete)
**Hardware:** Texas Instruments ADS1299EEGFE-PDK (ADS1299EEG-FE front-end + MMB0 motherboard)
**Reference docs:** SLAU443B (EEG Front-End PDK user's guide), SBAS499 (ADS1299 datasheet)

## 1. Purpose

A research-grade desktop GUI for acquiring, monitoring, and recording 8-channel
EEG from the ADS1299 analog front-end. The primary use is **live monitoring +
reliable recording to disk**, with full device-configuration provenance, event
markers, and electrode impedance / lead-off logging.

The application is NOT for clinical or diagnostic use (per SLAU443B §1.1).

## 2. Critical hardware constraint

The ADS1299EEG-FE board does not talk to a PC on its own. In the stock kit it
plugs into the **MMB0 motherboard**, which speaks a **proprietary, undocumented
USB protocol** (the "USBStyx" driver). The stock TI software is a Windows-XP-only
LabVIEW / National Instruments application. SLAU443B §2.1 explicitly states it
"does not provide technical details about the MMB0 itself."

Consequence: a custom cross-platform GUI cannot reliably talk to the MMB0. The
practical, documented path is to wire the front-end's **SPI header** (SLAU443B
Table 8, "Serial Interface Pin Out") to a user-supplied microcontroller
(Teensy 4.x / STM32 / Raspberry Pi) that reads the ADS1299 over SPI on the `DRDY`
interrupt and streams fixed binary frames over USB-serial. This is the same
approach used by essentially every open ADS1299 project (e.g. OpenBCI).

## 3. Strategy: simulator-first behind a swappable boundary

All data enters through one interface, `IDataSource`. Two implementations:

- **`SimulatedSource`** — synthetic but realistic 8-channel EEG. Built first.
  Makes the entire pipeline buildable and testable headless, with no hardware.
- **`SerialSource`** — real hardware via USB-serial. Dropped in later behind the
  same interface, with zero changes to the UI or recording layers.

The binary frame protocol is specified now (§8) so future MCU firmware has a
fixed target.

## 4. Tech stack

- **Language / framework:** C++17, Qt6 (Widgets, SerialPort).
- **Build:** CMake.
- **Plotting:** QCustomPlot (vendored) — fast real-time line rendering with
  display-side decimation.
- **EEG file formats:** EDFlib (vendored) — standard C library for EDF+/BDF.
- **Spectrum:** KISS FFT (vendored).
- **Tests:** QtTest.
- **Theme:** Dark "scientific instrument" QSS with design tokens in `Theme.h`.

Rationale: C++ Qt was chosen by the user for a single native binary. EDF/DSP are
handled with small, battle-tested vendored libraries rather than hand-rolled code.

## 5. Module architecture

```
ads1299-studio/
├─ core/
│  ├─ acquisition/   IDataSource, SimulatedSource, SerialSource(P2), EegFrame, FrameParser
│  ├─ device/        Ads1299Registers, DeviceConfig (immutable), DeviceController
│  ├─ dsp/           Biquad/IirFilter, FftProcessor, RingBuffer<T>, ScaleConverter
│  ├─ recording/     Recorder (EDF/BDF+CSV), SessionMetadata, AnnotationStore, ImpedanceLog
│  └─ logging/       Logger (JSONL structured diagnostics)
├─ app/             SessionController, AppState
├─ ui/
│  ├─ MainWindow, MonitorView, RegistersView, ImpedanceView, SpectrumView, RecordingPanel
│  └─ theme/        Theme.h, studio.qss, widgets (LedIndicator, ChannelStrip)
├─ tests/
├─ third_party/     QCustomPlot, EDFlib, KISS FFT
└─ CMakeLists.txt
```

Each module has one clear purpose, a narrow public interface, and is testable in
isolation. Target 200–400 lines/file, 800 max.

### 5.1 Acquisition layer
- `IDataSource` — abstract: `start()`, `stop()`, `applyConfig(DeviceConfig)`,
  `capabilities()`, signal `framesReady(EegFrameBatch)`, signal `statusChanged()`.
- `EegFrame` — `{ uint32 seq; int32 ch[8] (24-bit sign-extended); uint8 statP; uint8 statN; uint8 gpio }`.
- `SimulatedSource` — generates alpha/beta rhythms + 1/f noise + configurable
  line noise + occasional artifacts; deterministic seed for tests. Own QThread.
- `SerialSource` (Phase 2) — QtSerialPort + `FrameParser` decoding the §8 protocol.

### 5.2 Device model
`Ads1299Registers` encodes the full register map (SBAS499 + SLAU443B §5): per-channel
gain (1–24), sample rate (250 SPS–16 kSPS), input mux (normal/test/shorted/temp),
reference (SRB1 / dedicated), BIAS drive, lead-off. `DeviceConfig` is an **immutable**
value object that round-trips to/from register bytes. Each config is snapshotted into
session metadata for provenance.

### 5.3 DSP
`Biquad`/`IirFilter` (bandpass, high-pass, 50/60 Hz notch), `FftProcessor` (windowed,
KISS FFT), `RingBuffer<T>` (fixed circular), `ScaleConverter` (raw counts ↔ µV using
gain and Vref = 4.5 V → LSB = Vref / (gain · 2^23)).

### 5.4 Recording
- `Recorder` — writes BDF+ and raw CSV concurrently on the writer thread. The
  raw signal is stored as **24-bit BDF+** (not 16-bit EDF) to preserve the full
  ADS1299 resolution; EDFlib handles both, BDF+ is the primary research format.
- `SessionMetadata` — JSON sidecar: subject ID, montage, date, notes, full register snapshot.
- `AnnotationStore` — time-locked event markers → EDF+ annotations + JSON.
- `ImpedanceLog` — periodic per-channel impedance + lead-off → CSV/JSON time series.
- `Logger` — JSONL diagnostics for every connect / config change / record start-stop / drop / error.

## 6. Threading & data integrity

Three threads decoupled by ring buffers:
- **Acquisition thread** → batched frames via queued signals.
- **GUI thread** → renders decimated traces (no per-sample draw at high SPS).
- **Writer thread** → recording I/O; never blocks acquisition.

The `seq` field is checked for continuity every batch. Dropped samples are
detected, counted, logged, and annotated into the EDF. Buffer overflow, disk-full,
and write errors are surfaced in the status bar and the diagnostic log — never
silently swallowed. This integrity accounting is the core of "research-grade."

## 7. UI (dark scientific instrument)

Shell: top toolbar (Connect ▸ Start ▸ Record), left config dock, central tabbed
views, bottom status bar (live SPS, dropped-frame counter, buffer health, free disk).

Tabs:
- **Monitor** — scrolling multi-channel traces (QCustomPlot); per-channel µV scale,
  offset, montage; pause/zoom.
- **Registers** — full device-config editor mapped to `DeviceConfig`.
- **Impedance** — per-channel LED indicators + impedance history.
- **Spectrum** — live FFT / PSD per channel.
- **Recording** — session metadata form, marker controls, file paths, record buttons.

Theme: QSS dark charcoal, design tokens (color / typography / spacing) in `Theme.h`,
custom `LedIndicator` and `ChannelStrip` widgets.

## 8. Binary frame protocol (target for future MCU firmware)

Little-endian unless noted. One frame per ADS1299 DRDY:

```
offset  size  field
0       2     sync     = 0xA5 0x5A
2       4     seq      uint32, increments per frame (drop detection)
6       24    data     8 × int24 big-endian (native ADS1299 DATA order), sign-extended on host
30      1     statP    LOFF_STATP (positive lead-off comparators)
31      1     statN    LOFF_STATN (negative lead-off comparators)
32      1     gpio     GPIO state
33      1     crc8     CRC-8 over bytes [2..32]
                       total = 34 bytes/frame
```

Host side: `FrameParser` resynchronizes on the sync word, verifies CRC, checks
`seq` continuity. The ADS1299 24-bit STATUS word maps to statP/statN/gpio.

## 9. Error handling

- Sample-count discontinuity → flag + count + log + EDF annotation.
- Disk full / write failure → stop recording gracefully, alert, preserve written data.
- Buffer overflow → count and surface; never drop silently.
- Serial disconnect (Phase 2) → clean stop, status update, log entry.
- All user input (metadata, register fields, file paths) validated at the UI boundary.

## 10. Testing

QtTest unit tests: register encode/decode round-trip, raw↔µV conversion, ring
buffer wrap, filter frequency response, EDF write-then-read-back equality,
simulator determinism (seeded), frame parser (incl. corrupted/desynced input).
The simulator enables full end-to-end pipeline tests with no hardware. Target 80%+
coverage on `core/`.

## 11. Build sequencing (detailed in the implementation plan)

Primary use = live monitoring + recording, so the first vertical slice is:
CMake/Qt scaffold → `IDataSource` + `SimulatedSource` → `RingBuffer` + threading →
`MonitorView` + dark theme → `Recorder` (EDF/CSV) + metadata + markers + integrity
counters. Then layer Registers, Impedance, Spectrum, and finally the Phase-2
`SerialSource`.

## 12. Out of scope (YAGNI)

- Reverse-engineering the MMB0 USB protocol.
- Clinical/diagnostic features.
- Real-time classification / BCI decoding.
- Cloud sync / multi-station networking.
