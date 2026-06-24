# ADS1299 Studio

A research‑grade, native **macOS / Qt6** desktop application for acquiring, monitoring, and recording 8‑channel EEG from the Texas Instruments **ADS1299** analog front‑end (ADS1299EEGFE‑PDK).

It provides a modern dark "scientific instrument" UI with GPU‑accelerated real‑time waveforms, full‑fidelity raw‑data recording (BDF+ / CSV / JSON), live signal‑quality tooling, and a clean hardware‑abstraction boundary so the same app runs against a built‑in simulator today and the real MMB0 hardware over USB.

> ⚠️ **Not for clinical or diagnostic use.** Like the TI EVM it targets (see SLAU443B), this software is for laboratory evaluation and research only.

---

## Highlights

- **GPU real‑time scope** — custom `QOpenGLWidget` line renderer (VAO/VBO + GLSL 330 Core, `GL_LINE_STRIP` per channel) with min/max‑envelope decimation and right‑aligned scrolling, built for high sample rates and channel counts. Per‑channel **auto‑scale** (fast‑attack / slow‑release envelope) or manual µV/div.
- **Research‑grade recording** — 24‑bit **BDF+** (lossless, via EDFlib) **plus** a complete per‑sample **CSV** (`timestamp_utc, seq, t_seconds, statP, statN, gpio, ch0..7_raw counts, ch0..7_uV, flag`) **plus** a self‑describing **JSON** sidecar. Dropped samples are detected, padded, flagged (`drop_pad`), and annotated into the BDF so the time base stays wall‑clock‑correct. Recording always stores **raw** data — display filters never touch it.
- **Signal quality** — live notch (50/60 Hz) + band‑pass display filters, persistent spectrum (FFT/PSD), per‑channel impedance / lead‑off view, a prominent alert bar (dropped samples / lead‑off / errors), and a pre‑recording electrode‑quality check.
- **Usable workflow** — first‑class **Acquisition** sidebar (sample rate + gain + live resolution/bandwidth readouts) with an always‑visible **Recording** module, event markers, one‑click session **export**, a **Sessions** browser for past recordings, and keyboard shortcuts (F5 / Ctrl+R / Ctrl+M / Ctrl+P / Ctrl+1‑5 / F1).
- **Swappable acquisition** — everything sits behind one `IDataSource` interface: a deterministic **simulator** (default), a generic **serial** backend, and a **libusb MMB0** backend that speaks the device's real protocol.

---

## Hardware path: how the app talks to the MMB0

The ADS1299EEGFE‑PDK ships with the **MMB0 motherboard** as its USB bridge. The full USB protocol was reverse‑engineered from TI's own client source and documented in:

**[`docs/hardware/2026-06-23-mmb0-usb-styx-protocol.md`](docs/hardware/2026-06-23-mmb0-usb-styx-protocol.md)**

In short, the PC talks to the MMB0 as a **raw libusb device** speaking **9P2000** (the Plan 9 / "Styx" remote‑filesystem protocol):

| | |
|---|---|
| USB IDs | VID `0x0451`, PID `0x5718` (USBStyx/libusb mode) |
| Interface / endpoints | iface 0, bulk **OUT `0x01`**, bulk **IN `0x81`**, 64‑byte packets |
| Transport | 9P2000, version string `"9P2000.USB.estyx"` |
| Model | a synthetic filesystem — write registers to `/ads1299evm/conf/*`, set `/ads1299evm/blocksize_samples`, write `/ads1299evm/acquire`, then read `/ads1299evm/data` (27‑byte ADS1299 records) |

On macOS this needs **no driver install** — `libusb‑1.0` claims the interface directly. The backend (`Mmb0DataSource` + `Styx9pClient` + `Mmb0UsbTransport`) is implemented and headless‑tested; a short list of firmware constants to confirm on first real‑hardware run is in the protocol doc.

---

## Architecture

A layered C++17 design with a strict **acquisition boundary** (`IDataSource`) so data sources are interchangeable without touching the UI or recording.

> **Extending the project?** See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the `IDataSource` extension point, threading model, and the invariants you must not break.

```
┌─ UI (src/ui) ────────────────────────────────────────────────┐
│ MainWindow · MonitorView→GlWaveformWidget(OpenGL) · Spectrum  │
│ AcquisitionPanel · RecordingPanel · RegistersView ·           │
│ ImpedanceView · SessionsView · AlertBar · dialogs · theme     │
└──────────────────────────────┬───────────────────────────────┘
                               │ signals/slots
┌─ App (src/app) ──────────────▼───────────────────────────────┐
│ SessionController — worker thread, seq‑gap integrity,         │
│ metrics, config, recording lifecycle, source swap             │
└──────┬───────────────────────────────────┬───────────────────┘
       │ IDataSource                        │ Recorder (writer thread)
┌──────▼─────────────────┐      ┌───────────▼───────────────────┐
│ SimulatedSource        │      │ BDF+ (EDFlib) · CSV · JSON     │
│ SerialSource           │      │ AnnotationStore · SessionExp.  │
│ Mmb0DataSource ─ 9P/   │      └────────────────────────────────┘
│   Styx9pClient/USB     │
└────────────────────────┘   core/dsp: ScaleConverter · IirFilter ·
                             FftProcessor · DisplayFilterChain · Decimate
                             core/device: DeviceConfig · Ads1299Registers
```

Source tree:

```
src/
  app/          SessionController, AppState
  core/
    acquisition/  IDataSource, EegFrame, SimulatedSource, SerialSource, FrameParser
      mmb0/       Styx9p (9P2000 marshalling), Styx9pClient, ITransport,
                  Mmb0UsbTransport, MessageDeframer, Ads1299RecordParser, Mmb0DataSource
    device/       DeviceConfig (immutable), Ads1299Registers
    dsp/          ScaleConverter, RingBuffer, IirFilter/Biquad, FftProcessor,
                  Decimate, DisplayFilterChain
    recording/    Recorder, SessionMetadata, AnnotationStore, ImpedanceLog,
                  SessionExporter, SessionScanner
    logging/      Logger (JSONL diagnostics)
  ui/
    gl/           GlWaveformWidget, WaveformTransform, AutoScale
    theme/        Theme tokens, studio.qss, LedIndicator
    *.cpp         MainWindow + all views/panels/dialogs
tests/            40 test files (unit + integration), run via ctest
docs/             specs, implementation plans, MMB0 protocol
third_party/      QCustomPlot 2.1.1, EDFlib, KISS FFT (vendored)
```

---

## Tech stack

- **C++17**, **Qt 6** (Widgets, OpenGLWidgets, OpenGL, SerialPort, PrintSupport, Test)
- **CMake** (≥ 3.21), out‑of‑source build
- **libusb‑1.0** (MMB0 backend)
- Vendored: **QCustomPlot 2.1.1** (spectrum plot), **EDFlib** (BDF/EDF), **KISS FFT** (spectrum)
- On macOS the app forces the **Fusion** style so the custom dark stylesheet themes every widget consistently.

---

## Build & run (macOS, Apple Silicon)

Prerequisites (Homebrew):

```bash
brew install qt cmake libusb
```

Configure, build, run:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build -j
./build/ads1299-studio
```

Run the tests:

```bash
ctest --test-dir build --output-on-failure
```

Headless smoke / CI (no display or GPU required):

```bash
QT_QPA_PLATFORM=offscreen ./build/ads1299-studio --selftest    # launches, applies theme, exits 0
QT_QPA_PLATFORM=offscreen ./build/ads1299-studio --shot out.png # renders a screenshot
```

> Note: the GPU waveform renderer requires a real GL context; under `offscreen` it degrades gracefully to a "GPU unavailable" placeholder (no crash), so visual rendering must be checked on real hardware.

---

## Using it

1. Launch the app — it starts streaming **simulated** 8‑channel EEG so you can explore everything without hardware.
2. **Acquisition** sidebar (left): pick the **Sample rate** (250 Hz … 16 kSPS) and **Gain** (1×…24×); the live readouts show resolution (µV/LSB), input range, and Nyquist bandwidth.
3. **Monitor** tab: press **Start** (F5). Toggle **Auto** for per‑channel auto‑scaling, or set µV/div manually. Enable the **Notch / Band‑pass** display filters (these affect the *display only* — recordings stay raw).
4. **Recording** module (left sidebar): fill subject/montage/notes, choose an output folder, **Record** (Ctrl+R). A pre‑recording signal‑quality check runs first; add event markers with **Ctrl+M**. Recording is frozen against config changes to protect the time base.
5. **Sessions** tab: browse past recordings (subject / date / duration / rate), reveal in Finder, or export.
6. **Connect** (toolbar): auto‑detects a real MMB0 (`0x0451:0x5718`) and swaps it in as the live source.

### Recorded files (per session base path)

| File | Contents |
|---|---|
| `<base>.bdf` | 24‑bit **BDF+**, lossless EEG + event annotations |
| `<base>.csv` | complete per‑sample record: absolute timestamp, seq, raw counts **and** µV, lead‑off/GPIO status, drop flags |
| `<base>.meta.json` | self‑describing sidebar: subject, sample rate, gain, full device‑register snapshot, CSV column list, annotations |

---

## Status

| Area | State |
|---|---|
| Simulator + full UI + recording + DSP | ✅ complete, tested |
| GPU waveform renderer + auto‑scale | ✅ complete (visuals verified on hardware) |
| MMB0 libusb / 9P‑Styx backend | ✅ implemented & headless‑tested — **not yet verified against the physical MMB0** |
| Real‑hardware bring‑up | ⏳ next step (a few firmware constants to confirm; see protocol doc) |

All ~40 test suites pass (`ctest`). Development history is organized into phases (foundation → orchestration → UI → recording → device config → signal quality → serial → MMB0 backend → fidelity/UX → data‑quality tools → GPU rendering); specs and plans live under `docs/`.

---

## License

- Application source: see repository.
- Vendored third‑party libraries retain their own licenses: **QCustomPlot** (GPL), **EDFlib** (BSD), **KISS FFT** (BSD).
- The TI MMB0 reverse‑engineering reference source is **not** included in this repository (kept local only); only an original protocol description is published here.
