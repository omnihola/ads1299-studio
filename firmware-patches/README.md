# MMB0 / ADS1299 firmware patches

Source-level fixes for the PDK firmware (`ads1299evm-pdk.bin`, v0.0.27) that
cause the **fresh-flash first-session stall** and the **restart wedge**
documented in `docs/2026-07-02-mmb0-live-bringup-findings.md`. These are the
root fixes for the defects the host currently only *compensates for* (quiet
polling, alignment lock, wedge detection, RDATAC gates).

> **Status: source patch, NOT yet compiled or hardware-verified.** Building
> the firmware needs the TI C5500 code-generation tools (`cl55`, `hex55`)
> plus DSP/BIOS 5.41.x — a toolchain we deliberately did not keep installed.
> The changes are derived from static analysis of the firmware sources; the
> reasoning for each is in the patch comments and below. Apply, build, flash,
> and re-run `tests/test_mmb0live` (with `ADS1299_RUN_LIVE_TESTS=1`) to
> confirm the stall is gone.

## Applying

The firmware source tree is the extracted PDK mirror (git-ignored under
`reference/pdk-mirror`, cloned from
`github.com/binodthapachhetry/ADS1299EEGFE-PDK-related`). From its root:

```sh
cd reference/pdk-mirror
git apply ../../firmware-patches/0001-mmb0-fix-dma-interrupt-wedge.patch
```

## Building

Per `ads1299evm/Debug/makefile` (paths are the Windows originals; adapt to
your CGT/BIOS install):

```sh
# compile + link → ads1299evm-mmb0.out  (large memory model, C5509A target)
cl55 -v5509A -g --define=CCS_C55XX --define=C55XX --define=USBHW_DMALOG \
     --large_memory_model --memory_model=large --ptrdiff_size=32 \
     --use_long_branch --rom_model  ... -o ads1299evm-mmb0.out $(OBJS)

# convert to the C5000 boot table the host uploads to ep 0x06
hex55 mmb0.hexcmd        # → ads1299evm-pdk.bin
```

Drop the resulting `ads1299evm-pdk.bin` into `firmware/` (git-ignored) and
`connectMmb0` will upload it on the next cold boot.

## The four fixes (all in one patch)

Two files: `ADCProLib/src/acquire.cpp` and `ads1299evm/t1299_ob.c`.

1. **Stuck-acquire — `acquire_next_block()` ignored `dc_readblock`'s status.**
   The return was cast to `(void)`. When the driver can't start the transfer
   (e.g. `TIDC_ERR_XFERPROG`), the completion callback never fires and
   `acquiring` stays 1 forever — the host sees `/ads1299evm/acquire` never
   return to 0 and times out. Fix: on non-zero status, cancel the reserved
   (never-filled) queue write via `startWrite(0)` and clear `acquiring`.
   *Confidence: high (obvious dropped-error-return).*

2. **Interrupt-wedge — `ADS1299_readblock()` XFERPROG early return.**
   The function does `IRQ_globalDisable()` on entry, then `return
   TIDC_ERR_XFERPROG` when a prior transfer is still flagged in progress —
   WITHOUT re-enabling interrupts. That permanently disables all DSP
   interrupt handling (USB stack included) until a power cycle. This is the
   XFERPROG wedge the host now detects and refuses to re-arm into. Fix:
   `IRQ_globalEnable()` before the early return.
   *Confidence: high (plain resource leak; symmetric restore).*

3. **First-frame priming — the global `a` was never reset per block.**
   `a` latches to 1 after the first frame of the FIRST block and is never
   cleared. From the second block on, `SubmitBlock`/`drdyIsr` take the
   "not first frame" path and program the transmit-DMA element count one
   short, so the block never accumulates its full sample count and acquire
   stalls. Each block is an independent DMA setup whose first frame must
   re-prime the McBSP, so `a` must be 0 at the start of every block. Fix:
   set `a = 0` in `ADS1299_readblock()` right after claiming
   `iXferInProgress`. *Confidence: medium — logic is sound but `a`'s full
   timing behavior can only be confirmed on hardware; this is the fix most
   in need of a live re-test.*

4. **Interrupt-wedge (second path) — `SubmitBlock` failure return.**
   The `iStatus != TIDC_NO_ERR` bail-out returns with `HWI_disable()`,
   `TSK_disable()` and `IRQ_globalDisable()` all still in effect (set on the
   way in), wedging the DSP exactly like fix #2. Fix: `TSK_enable()`,
   `HWI_enable()`, `IRQ_globalEnable()` before the error return.
   *Confidence: high (same class as #2).*

Fixes #1–#3 are the direct causes of the two observed failure modes; #4 is a
latent instance of the same interrupt-restore bug found while auditing the
function. Together they should let a freshly flashed board stream from the
first session without the host-side stall workarounds having to paper over
it — though those workarounds stay in place as defense in depth.
