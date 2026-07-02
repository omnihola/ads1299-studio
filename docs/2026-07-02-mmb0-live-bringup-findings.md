# MMB0/ADS1299 live bring-up — verified findings (2026-07-02)

Live-hardware session that got `ads1299-studio` streaming real ADS1299 samples
end-to-end. **Corrects the 2026-07-01 handoff's "hardware DRDY problem"
conclusion: the hardware was never broken.** Every finding below was verified
on the board (ADS1299EEGFE-PDK, firmware `ads1299evm-pdk.bin` 0.0.27).

## Root causes of "acquire never completes" (all software/parameters)

1. **Power**: the MMB0 needs the external supply (5.5–15 VDC ≥500 mA barrel
   jack, or a 6 V battery pack — SLAU443B §3.3/§9.4). On USB power alone the
   digital side runs (bootload/SPI work) but the board idle-drops and browns
   out. With 6 V DC attached the board stays enumerated indefinitely.
2. **`blocksize_samples` MUST be a multiple of 9.** The firmware's DRDY ISR
   (`t1299_ob.c drdyIsr`) consumes 9 words per DRDY frame and disables INT0
   once the remaining count is < 9, while the receive-DMA completion
   (`rblock_finished`) waits for *exactly* `blocksize` words. With the
   reference tool's old default of 100 (100 % 9 = 1), only 99 words are ever
   clocked → the block mathematically never completes → `acquire` stays 1
   forever. This deadlock was previously misdiagnosed as a dead DRDY line.
   (Our app always writes `samples × 9`, so it was already safe.)

## Verified /data wire format

- **9 × 32-bit BIG-endian words per EEG sample**: STATUS + CH1..CH8.
- Each word carries the 24-bit value **sign-extended to 32 bits**
  (status `0xFFC00000`; positive channel `0x000141C1`).
- STATUS low 24 bits = `[1100][LOFF_STATP:8][LOFF_STATN:8][GPIO:4]`.
- Internal test signal (`config2=0xD0`, `chNset=0x65`, gain 24) measured
  **+82.37k codes** vs the theoretical ±83886 (±1.875 mV) — within 2%.
- Implemented in `Ads1299WordParser` (`WordOrder::BigEndian`).

## estyx / USBStyx transport rules (learned the hard way)

1. **Reply padding**: the firmware pads every reply to whole 64-byte USB
   packets (the official `cstyx/USBConnection.cc` pads both directions).
   After extracting one complete R-message, the residue in the packet is
   padding, NOT the next message — a deframer that retains it swallows every
   subsequent reply as the continuation of a phantom frame. Styx is strict
   request-reply → discard residue after each message
   (`Mmb0UsbTransport::recv`).
2. **Read 64 bytes per bulk-IN transfer.** Larger IN requests (e.g. 4 KiB)
   against this firmware time out on macOS/libusb.
3. **~2 KB server message buffer**: a single `/data` Tread requesting more
   than ~2037 payload bytes returns garbage past word ~509 while still
   declaring the full count (576-word block read as one 2304-byte Tread →
   junk after word 509; ≤1152-byte reads clean). We cap `/data` Treads at
   2016 bytes (56 samples, sample-aligned) — `kMaxDataChunkBytes`.
4. No Tversion; attach `uname/aname = "nobody"`; root fid 1 (estyx stores
   fids in a `short`); conf registers hex text `"0xNN"`; acquire text
   `"1"/"0"`; never write `/ads1299evm/conf/reset`.

## End-to-end status

- `test_mmb0live` (hardware-gated, skips in CI): attach → `/version=0.0.27`
  → `devid=0x3E` → streams the test signal: **480 frames / 2.5 s, 480/480
  within the ±82k amplitude band, zero errors**.
- GUI verified live: `ads1299-studio --test-signal --connect-mmb0` shows
  `Connected: ADS1299 (MMB0)`, SPS ≈ 192, Dropped 0.
- Effective rate is ~77% of nominal (192/250 SPS): the one-shot firmware
  model cannot capture between block completion and re-arm, and parser seq
  numbers are contiguous across the gap (invisible to seq-gap detection).
  Raising the per-block sample count toward 56 (one max-size Tread) improves
  the duty cycle; gap-free capture would need firmware changes.

## Session-lifecycle landmines (second live session, same day)

All four were reproduced on hardware and are now guarded in `Mmb0DataSource`:

1. **RDATAC register lockout on restart.** `acquire=0` only clears a firmware
   flag; the chip stays in RDATAC until the in-flight block's ISR issues
   STOP+SDATAC. Register WRITES during that window are silently dropped (a
   fast stop→start left every channel on its previous MUX) and reads return
   conversion garbage. Guards: poll `/conf/devid` until it reads 0x3E before
   configuring (`waitForSpiRegisterAccess`), then **read every written
   register back** and refuse to arm on any mismatch
   (`writeAndVerifyRegisters`).
2. **XFERPROG wedge.** `ADS1299_readblock()` disables global interrupts on
   entry and, when the previous block's `iXferInProgress` was never cleared,
   early-returns WITHOUT re-enabling them — the DSP's USB stack dies until a
   power cycle (even bulk OUT times out). Guards: after an acquire timeout,
   leave `acquire` at "1" as evidence (`acquireWedged_`); the next bringUp's
   drain step sees the stuck "1" and refuses to re-arm with a power-cycle
   error (`drainInFlightBlock`).
3. **Stale-word queue shift.** A session aborted mid-block can leave one
   word in the McBSP/DMA pipeline that lands at the head of the next block,
   shifting the 9-word grid (ch[0] read the STATUS value, ch[1] the real
   CH1). Guard: locate the true alignment via the STATUS signature
   (big-endian `FF Cx xx xx` at every 9-word stride) and drop the stale
   prefix (`findSampleAlignment`), sacrificing at most one sample per block.
4. **Unpadded hex read-back.** The firmware prints u32 files as `0x%X`
   ("0x0", not "0x00") — all read-back comparisons must be numeric, never
   string equality.

Channel mapping verified live by rotating the test signal through chip
CH1→CH2→CH3 (others shorted): the ±82k square wave follows `frame.ch[0..2]`
exactly, and MonitorView lane "CHn" draws `frame.ch[n-1]` — GUI channels map
one-to-one to ADS1299 channels. Shorted-channel offsets measured −0.4k…−1.2k
codes (−9…−27 µV) with ~40-code noise spans (≈0.9 µVpp, at the datasheet's
1 µVpp channel-noise spec).

## Fresh-flash first-session stall (unresolved firmware defect)

The first streaming session after a firmware flash stalls randomly within
2–6 block re-arms (observed with 270-, 288- and 576-word blocks; quieting
the USB bus during collection did not help). The stall is a lost DMA
completion in the firmware's fragile cross-block bookkeeping (the global
priming flag `a` in t1299_ob.c is never reset per block; the XFERPROG
early-return leaks a global interrupt disable). Once ANY session survives
and later sessions stop mid-block, the McBSP pipeline holds a residue word
that makes every subsequent block self-priming — sessions in that state ran
480-frame and 3-channel-rotation suites repeatedly with zero stalls (the
one-word shift is compensated losslessly by the host's alignment lock).

**Operational recipe:** cold-boot (unplug BOTH the 6 V barrel AND USB; power
first, USB second on reconnect) → connect → Start. If "block never
completed" appears, cold-boot and retry — usually 1–3 attempts. Once
streaming, the session and all later ones are stable. Replugging USB alone
never recovers a wedge: the DSP keeps running on the external supply.

**Root fix (future work):** rebuild the firmware — full source is in
`reference/ti-mmb0/` and `~/Documents/ads1292-studio/mmb0/fw_src/`. Known
bugs to fix: (1) `acquire_next_block()` ignores `dc_readblock`'s return;
(2) `ADS1299_readblock()` XFERPROG early-return leaves `IRQ_globalDisable`
in effect; (3) per-block DMA priming relies on the never-reset global `a`.
Needs the TI C55x code-generation tools.

## Debug workflow notes

- Reference C tools live in `~/Documents/ads1292-studio/mmb0/` (probe,
  bootload, acquire). `mmb0_acquire --blocksize` must be a multiple of 9.
- `--shot-delay N` extends the `--shot` capture window; `--connect-mmb0`
  and `--test-signal` drive the live bring-up headlessly.
