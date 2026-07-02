# Handoff: MMB0/ADS1299 reverse-engineering findings + connection-UI feature

**Date:** 2026-07-01
**For:** the agent working in `~/Documents/ads1299-studio` (the real ADS1299 / EEG app)
**Author:** a session that ran in the WRONG folder (`~/Documents/ads1292-studio`, the ADS1292 / ECG app) and must now hand its results across.

> ⚠️ **Folder mix-up — read this first.** All the work described below was done and
> committed inside **`~/Documents/ads1292-studio`** (branch `c++`), which is a *different*
> project (ADS1292, 2-channel ECG, serial). The MMB0/ADS1299 reverse-engineering belongs
> in **THIS** project (`ads1299-studio`, 8-channel EEG). Nothing here should be copied
> verbatim — it must be **ported/adapted** into `ads1299-studio`, and cross-checked against
> the MMB0 backend you already have. Reference C sources live at
> `~/Documents/ads1292-studio/mmb0/*.c` and findings at `~/Documents/ads1292-studio/mmb0/FINDINGS.md`.

---

## Goal

Two deliverables, both to be applied in `ads1299-studio`:

1. **Make the MMB0/ADS1299 backend actually stream EEG samples** — verify/fix your existing
   `src/core/acquisition/mmb0/` code against the protocol facts I confirmed on real hardware.
2. **Add clear connection-status feedback + gate "Start" on a successful connection** in the GUI
   (`src/ui/MainWindow.cpp`), so the user can tell whether Connect worked and cannot Start
   without a live device.

---

## Current progress (what was proven on real hardware this session)

The board (ADS1299EEGFE-PDK: MMB0 motherboard = TMS320C5000 DSP + ADS1299 analog front-end)
was driven end-to-end over **pure libusb on macOS** (no TI Windows driver). Gates reached:

- **G0** libusb enumeration — OK
- **G1** firmware image sourced + verified — OK
- **G2** firmware upload → device re-enumerates to Styx mode — OK
- **G3** read `/version` = `0.0.27` over Styx/9P — OK
- **G4** read `/ads1299evm/conf/devid` = `0x3E` (ADS1299, 8-channel) — OK
- **G5** full ADS1299 register config with **verified read-back** (SPI to the chip works) — OK
- **G5 streaming** — **BLOCKED by a board-level hardware condition** (see "What didn't work").

---

## Reference sources (authoritative — have your agent read these)

- **GitHub mirror of the extracted PDK** (firmware image + full TI source, incl. the estyx 9P
  server and the ADS1299 driver): `github.com/binodthapachhetry/ADS1299EEGFE-PDK-related`
  - Firmware image: `ADS1299EEGFESoftware/ADS1299SW/ads1299/fw_1299/ads1299evm-pdk.bin`
    — **79248 bytes, sha256 `b5c1bc9415c187d707a5a0e72429a11cc14c9b2ed0dc9a1afd8e8f306d1caf12`** — a
    **C55xx DSP boot table** (magic starts `0001 69c5 0000 0015 6c...`).
  - Host upload logic: `.../lib_1299/Styx/styxsh/dsploader.py`
  - 9P client the protocol was derived from: `.../lib_1299/Styx/cstyx/{Client.cc,styxmsg.cc,...}`
  - **Firmware-side data path (authoritative for acquire/data semantics):**
    `ADS1299EEGFEFirmware/ADS1299FW/dev/build/adcpro/ADCProLib/src/acquire.cpp` and
    `.../ads1299evm/ads1299evm_files.c` and `.../ads1299evm/t1299_ob.c` (ADS1299 driver + DRDY ISR).
- My reference C tools (in ads1292-studio, port don't copy): `mmb0/mmb0_probe.c`,
  `mmb0/mmb0_bootload.c`, `mmb0/mmb0_styx_read.c`, `mmb0/mmb0_acquire.c`.

---

## What worked (VERIFIED protocol facts — trust these over guesses)

### USB identity + firmware upload (bootloader → Styx mode)
- Board cold-boots as **`0451:9001`** (C5000 ROM USB bootloader). One vendor-specific interface,
  one endpoint: **`ep 0x06` OUT bulk, wMaxPacketSize=64**.
- **Upload = the whole firmware image bulk-written to ep 0x06 in ONE transfer** (libusb-1.0
  packetizes internally), matching `dsploader.py::loadDSP`:
  `open → set_configuration(1) → claim_interface(0) → set_interface_alt_setting(0,0) →
  libusb_bulk_transfer(ep 0x06, whole 79248-byte file, timeout≈5000ms) → release`.
  **No ZLP** (79248 % 64 = 16 → ends on a natural short packet). The DSP then self-resets.
- After upload it **re-enumerates as `0451:5718`** (USBStyx firmware mode), interface 0 with
  **`ep 0x01` OUT bulk 64B** and **`ep 0x81` IN bulk 64B** — these are the Styx transport pipes.
- **The board reverts to `0451:9001` on every power cycle.** So the app MUST be able to detect
  9001 and upload the firmware, not just assume 5718 is present. (See bug #4 below.)

### Styx / 9P wire protocol (from cstyx `styxmsg.cc` / `Client.cc`)
- Standard **9P2000 framing**, little-endian: `size[4] type[1] tag[2] <body>`.
- Message type numbers: `Tversion=100, Rversion=101, Tattach=104, Rattach=105, Rerror=107,
  Twalk=110, Rwalk=111, Topen=112, Ropen=113, Tread=116, Rread=117, Twrite=118, Rwrite=119,
  Tclunk=120, Rclunk=121` …
- **NO Tversion handshake in the reference client.** `Client::connect()` goes straight to
  `Tattach` then `Twalk`. Tag is fixed **`1`**. Root fid = **`1`**; walk to fresh fids.
- `Tattach`: `afid = 0xffffffff`, `uname = "nobody"`, `aname = "nobody"`.
- Qid = 13 bytes (`type[1] vers[4] path[8]`, only low 32 of path used). Strings = `len[2]+bytes`
  (no NUL on the wire). `Tread`/`Twrite` offset is **8 bytes** on the wire (low 32 used).
- Per-file sequence: `Tattach(1) → Twalk(1→N, path segments) → Topen(N, mode) → Tread/Twrite → Tclunk(N)`.
  My minimal client (`mmb0_styx_read.c`) read one message per round-trip by accumulating 64B IN
  packets until it had `size[4]` bytes.

### Device tree (enumerated live)
```
/version                         -> "0.0.27"
/id
/ads1299evm/blocksize_samples    (u32, decimal text; default 576)
/ads1299evm/acquire              (bool; "0"/"1"; ONE-SHOT — see below)
/ads1299evm/data                 (the sample block; 32-bit words)
/ads1299evm/conf/{config1,config2,config3,config4,ch1set..ch8set,devid,gpio,loff,
                  loffflip,loffsensp,loffsensn,loffstatp,loffstatn,misc1,misc2,
                  biassensp,biassensn,reset,sleep}   (u32 files, hex text "0xNN")
/mmb0/{led/*,pll,clkout,clkfreq}
```

### ADS1299 register config (VERIFIED read-back over SPI)
Writing hex text ("0xE0") to `/ads1299evm/conf/<reg>` performs a WREG; reading returns "0xNN".
Confirmed on hardware after a fresh flash:
- `devid` = **`0x3E`** (ADS1299, 8-channel; 0x3D=6ch, 0x3C=4ch). Use this as a health check.
- To get valid conversions you MUST set **`config3 = 0xE0`** (PD_REFBUF=1, internal reference ON;
  the reset default `0x60` has the reference **off**).
- `config1 = 0x96` (250 SPS). For a *known* verification waveform use the internal test signal:
  `config2 = 0xD0` (INT_CAL=1) and `chNset = 0x65` (gain 24, MUX=101 test signal). Normal input = `0x60`.
- All 12 register writes read back correctly after a fresh flash → **SPI to the ADS1299 works.**

### Acquisition model — **ONE-SHOT per block** (from `acquire.cpp`, authoritative)
`acquire_set(1)` clears the queue, collects **exactly one block** of `blocksize_samples` via
`dc_readblock(...)`, and its completion callback `acquire_adc_done()` calls **`acquire_set(0)`** —
so `acquire` **auto-resets to 0** after one block. The correct host loop is:
```
write blocksize_samples
loop:
    write /ads1299evm/acquire = "1"        (0x31 or 0x01 — see note)
    poll-read /ads1299evm/acquire until it reads "0"   (block collected)
    read /ads1299evm/data                  (returns the block)
```
`acquire_data_read` returns `count = requested_len>>2` **32-bit words** (`len<<2` bytes);
`acquire_data_len` returns `queue.len()<<2` bytes. **The queue stores 32-bit LONGS, not raw
27-byte ADS1299 frames** — the exact on-wire byte layout of `/data` MUST be confirmed by a raw
dump (use the internal test signal so you know what the decoded values should look like).

---

## What didn't work / dead ends (don't repeat these)

1. **Sample streaming is blocked by a BOARD-LEVEL hardware condition, not software.**
   With config verified landing (devid 0x3E, config3 0xE0), `acquire=1` **never flips back to 0**
   → the block never completes → **the ADS1299 DRDY (data-ready) interrupt never fires**. Per
   `t1299_ob.c`: `ADS1299_readblock()` issues `START`+`RDATAC` and enables DRDY on **INT0**, and
   the driver header states a hard requirement: **"the CS\ signal of the converter [must be] tied
   to ground permanently"**, with DRDY\ wired to the DSP's INT0. So the likely causes are
   **front-end board seating / a CS-to-ground jumper / the CLKSEL clock-source jumper** — check
   the ADS1299EEGFE hardware, not the code. **Tell the user this is hardware; don't chase a
   software ghost.**
2. **Writing `/ads1299evm/conf/reset` HANGS the Styx server** — every subsequent request times
   out and the board must be power-cycled. **Do not write `reset`.** Rely on the fresh-flash state
   (which is a clean SDATAC state; devid reads 0x3E) instead.
3. **`acquire="start"` / polling `/data` immediately** returns 0 bytes forever — because acquire
   is one-shot (see above) and `/data` is non-blocking. Must write `"1"` and wait for the auto-reset.
4. **Firmware-image extraction from the NI installers** (`ADS1299EEGFE*-installer.exe`) is a rabbit
   hole — they are Tclkit+Metakit VFS, not extractable with zlib carving. Get the `.bin` from the
   GitHub mirror above (verified sha256) or by running the installer in Windows.
5. **Operational gotchas:** the Styx-mode (5718) device **idle-drops** if not read promptly; under
   marginal power the whole board browns out (all rails, incl. 1.8V/3V LEDs) and **latches** —
   recovery needs a full **cold power-cycle** (unplug ~15s). If plugging the board collapses a USB
   hub, that's over-current. A stable supply / direct port matters. On Parallels, a running Windows
   VM grabs the USB device — suspend it (`prlctl suspend`) before libusb work.

---

## BUGS to check in the EXISTING `ads1299-studio` MMB0 backend

I read your `src/core/acquisition/mmb0/*` and found several divergences from the verified protocol
that likely explain why it doesn't stream. Verify each:

1. **`Mmb0DataSource::start()` writes `acquire=1` ONCE; `pollOnce()` never re-arms it.**
   (`Mmb0DataSource.cpp` ~line 190 writes acquire=1; the 5ms `pollTimer_`→`pollOnce` just reads
   `/data`.) Given the **one-shot** firmware model, you get at most ONE block, then `/data` stays
   empty. **Fix:** re-arm `acquire=1` per block and wait for it to read `0` before reading `/data`
   (or confirm empirically whether a continuous mode exists — the source says one-shot).
2. **`Styx9pClient::connectSession(8192, "9P2000.USB.estyx")` sends Tversion and waits for Rversion.**
   The reference client (and my working client) **skip Tversion entirely** and go straight to Tattach.
   If your connect ever hangs/fails at version negotiation, **try removing the Tversion step**.
   Also your `attach(QString(), QString())` uses empty uname/aname — the reference uses
   **`"nobody"`/`"nobody"`**; switch if attach is rejected.
3. **`pollOnce()` assumes `blocksize_samples * 27` bytes and parses 27-byte records.** The firmware
   queue is in **32-bit words** (`/data` = `len<<2` bytes), and `blocksize_samples` is a word count,
   not a 27-byte-record count. **Verify the real `/data` byte layout with a raw dump + the internal
   test signal** before trusting the 27-byte framing in `Ads1299RecordParser`.
4. **No firmware-upload path.** Nothing in `src/` handles `0451:9001` → upload → `0451:5718`. If the
   board is freshly powered (bootloader), the app can't connect. **Add a bootload step** (port
   `mmb0_bootload.c`): detect 9001, bulk-write `ads1299evm-pdk.bin` to ep 0x06, wait for 5718.
   Ship/locate the `.bin` (79248 B, sha256 above) — do NOT commit it if licensing is a concern.
5. **Never write `/ads1299evm/conf/reset`** anywhere (it hangs the server — see dead-end #2).

---

## GUI feature: clear connection status + gate Start on a successful connection

**Why:** the user clicks Connect and can't tell if it succeeded. In `ads1299-studio`,
`MainWindow.cpp` already has a `connectAction_`, `controller_->connectMmb0()/connectSerial()`,
a `linkLed_` (`LedIndicator` Ok/Warn), and transient `statusBar()->showMessage(...)`. That's a
start, but (a) the statusBar message is transient/easy to miss and (b) **Start is not gated on a
successful connection.**

**What I implemented in ads1292-studio (adapt the intent, not the code):**
1. **Persistent, colored connection state** — green ● connected / yellow ● connecting / red ●
   failed / grey ○ not connected, with a short text (port + fw). In ads1299-studio, keep `linkLed_`
   but also add a **persistent status label** (not just a 4s toast), or make the LED + a text field
   reflect the last result until the next attempt.
2. **"Connecting…" transient state** — while `connectMmb0/connectSerial` runs, show a connecting
   indicator and disable the connect action so it can't be double-fired.
3. **Gate Start on a live connection** — the key ask: **`startAction_` must be disabled until a
   connection has succeeded** (and re-disabled if it drops/fails). In ads1292-studio this was one
   line in the control-gating function: `startEnabled = !streaming && !connecting && isConnected`.
   Wire an `isConnected()` (or a `connectedSourceType_`) flag from `connectMmb0/connectSerial`
   results into the enable/disable of `startAction_`.
   - Note: the **simulator** source counts as "connected" for Start purposes — don't lock the user
     out of the default sim path. Gate on "a source is active", where MMB0/serial require a
     *successful* connect and the simulator is always available.

**Tests:** ads1292-studio's `test_gui_smoke.cpp` gained assertions like "Start disabled until
connected; disabled again after a failed connect; enabled after a successful connect." Add the
equivalent for `ads1299-studio`'s MainWindow control-gating.

---

## Next steps (ordered)

1. **Hardware first (unblocks everything):** with the user, verify the ADS1299EEGFE front-end is
   fully seated on the MMB0 and check the **CS-to-ground**, **CLKSEL/clock**, and **START** jumpers
   per the EVM user guide; confirm the 1.8V/3V rail LEDs stay solid under load. Symptom to clear:
   `acquire=1` must flip back to `0` within ~1s (DRDY firing).
2. **Backend fixes** (do these in `ads1299-studio/src/core/acquisition/mmb0/`): re-arm acquire per
   block (#1), add the 9001→5718 firmware-upload path (#4), confirm/curtail Tversion (#2), and
   **empirically verify the `/data` byte layout** with a raw dump using the internal test signal (#3).
   Use `devid==0x3E` and `config3` read-back as your "chip is alive + configured" gate.
3. **Once a block streams:** decode against the known internal-test-signal square wave to lock the
   frame format, then wire it into `Ads1299RecordParser` / `EegFrame`.
4. **GUI:** implement the connection-status + Start-gating feature above; add the control-gating tests.
5. Optionally port the standalone C tools (`mmb0_probe/bootload/styx_read/acquire`) as dev utilities.

**Build/run for reference (ads1292-studio, the tools):**
`clang -std=c11 -O2 mmb0/<tool>.c $(pkg-config --cflags --libs libusb-1.0) -o mmb0/<tool>`
