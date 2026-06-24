# MMB0 ↔ PC USB Protocol (reverse‑engineered from TI software)

**Date:** 2026-06-23
**Goal:** let the macOS Qt app do live ADS1299 acquisition by talking to the PDK's
**MMB0 motherboard** directly over USB with **libusb** — exactly as TI's own software does.
**Source of truth:** TI's `cstyx`/`estyx` C source + drivers, extracted from
`ADS1299EEGFESoftware-1.0.2-installer.exe` (preserved under `reference/ti-mmb0/`).

## TL;DR — the link is libusb + 9P (Plan 9 "Styx")

The PC does **not** use a proprietary protocol. It opens the MMB0 as a raw USB
device via **libusb** and speaks **9P2000** (the Plan 9 / "Styx" remote‑file
protocol). The device exposes a small synthetic **filesystem**; the host configures
the ADS1299 and streams data by **reading/writing named files**. All of this is
portable to macOS with `libusb‑1.0` (TI even bundles `libusb‑1.0.8/os/darwin_usb.c`).

## 1. USB layer

| Item | Value | Source |
|---|---|---|
| Vendor ID | **0x0451** (Texas Instruments) | firmware device descriptor; `usbstyx.inf` |
| Product ID (USBStyx/libusb mode) | **0x5718** | firmware device descriptor; `usbstyx.inf` |
| Product ID (alt "MMB0 NI‑VISA" mode) | 0x9001 | `mmb0_visa.inf` (not used for our path) |
| Interface | **0** (alt 0), claimed | `USBConnection.cc` |
| Bulk OUT endpoint | **0x01** | `USBConnection.cc` (`_outep=1`); fw config desc |
| Bulk IN endpoint | **0x81** | `USBConnection.cc` (`_inep=0x81`); fw config desc |
| wMaxPacketSize | **64** | firmware config descriptor |
| Timeout | 3000 ms | `USBConnection.cc` |

Open sequence (`USBConnection.cc`): `usb_set_configuration` → `usb_claim_interface(0)`
→ `usb_set_altinterface(0)` → `usb_clear_halt(0x81)` + `usb_clear_halt(0x01)`.
Every bulk transfer length is **rounded up to a multiple of 64** (the unused tail is
ignored). On macOS this maps 1:1 to `libusb_set_configuration` / `libusb_claim_interface`
/ `libusb_clear_halt` / `libusb_bulk_transfer`.

The driver the TI installer binds on Windows is **libusb‑win32** (`usbstyx.inf`,
class `libusb-win32 devices`). On macOS no driver install is needed — `libusb` claims
the interface directly (the MMB0 exposes a vendor‑specific interface, not HID/CDC).

## 2. Transport: 9P2000 over the bulk endpoints

Each 9P message is self‑framed: it begins with `size[4]` (little‑endian total byte
count) so the reader knows the message length; the host accumulates bulk‑IN bytes
until a full message is present (`USBConnection.cc` keeps an `_rxq`). T‑messages go
out on EP 0x01, R‑messages come back on EP 0x81.

Message set & marshalling are in `cstyx/styxmsg.cc` (classic Plan 9 `GBIT32`/`PBIT32`,
`IOHDRSZ=24`). The **version string is `"9P2000.USB.estyx"`** (`estyx/src/estyx.h`),
NOT plain `"9P2000"` — the Tversion must offer this exact string.

Session bring‑up (standard 9P):
1. `Tversion(tag=NOTAG, msize, "9P2000.USB.estyx")` → `Rversion(msize', version')` — negotiate max message size.
2. `Tattach(tag, fid=ROOT, afid=NOFID, uname, aname="")` → `Rattach(qid)` — get the root fid.

Per‑file access (what `cstyx_write_path` / `cstyx_read_all_path` do internally):
3. `Twalk(tag, fid=ROOT, newfid, nwname, wname[...])` → `Rwalk(qid[...])` — resolve a path like `ads1299evm/conf/config1`.
4. `Topen(tag, newfid, mode)` → `Ropen(qid, iounit)`.
5. `Tread(tag, fid, offset, count)` → `Rread(count, data[])`  **or**  `Twrite(tag, fid, offset, data[])` → `Rwrite(count)`.
6. `Tclunk(tag, fid)` → `Rclunk` — release the fid.

The public C API reduces this to two calls:
`cstyx_write_path(client, "/path", data, len)` and
`cstyx_read_all_path(client, "/path", buf, lim)` (`cstyx/cstyx.h`).

## 3. Device namespace (the synthetic files)

From `reference/ti-mmb0/docs/ADS1299EVM Styx Index.txt`:

```
/version                         # firmware/protocol version
/id                              # device id string
/ads1299evm/blocksize_samples    # write: samples per /data read block
/ads1299evm/acquire              # write: start/stop continuous acquisition
/ads1299evm/data                 # read: streamed ADS1299 sample records
/ads1299evm/conf/config1..4      # ADS1299 CONFIGn registers (read/write)
/ads1299evm/conf/ch1set..ch8set  # CHnSET registers
/ads1299evm/conf/loff, loffflip, loffsensn/p, loffstatn/p
/ads1299evm/conf/bias sensn/p, misc1, misc2, gpio, devid, reset, sleep
/mmb0/led/{dp,seg,ch}, /mmb0/pll, /mmb0/clkout   # MMB0 housekeeping
```

Each `conf/*` file is a register: **write** a byte to set it, **read** to get it
back. The register encodings are the standard ADS1299 ones already modelled in our
`DeviceConfig` (Task 16) and documented in `reference/ti-mmb0/docs/ADS1299 Register Map.txt`.

## 4. Acquisition flow (host side)

1. libusb open (0x0451/0x5718), claim iface 0, clear halts.
2. 9P `Tversion`("9P2000.USB.estyx") + `Tattach` → root.
3. Write the desired register set: `write_path("/ads1299evm/conf/config1", <byte>)`,
   `…/config2`, `…/config3`, `…/ch1set..ch8set`, `…/misc1`, lead‑off, bias, etc.
   (Optionally `…/reset` first.)
4. `write_path("/ads1299evm/blocksize_samples", <N>)` — choose block size.
5. `write_path("/ads1299evm/acquire", <start>)` — begin continuous conversion
   (firmware puts the ADS1299 in RDATAC and reads it over SPI on DRDY).
6. Loop: `read_path("/ads1299evm/data", buf)` → each read returns N sample **records**.

### Data record format (standard ADS1299 RDATAC output)
Per sample: **27 bytes** = `status[3]` + `ch1[3] … ch8[3]`, each value **24‑bit,
big‑endian, two's‑complement** (MSB first). The 24‑bit status word is
`0b1100 + LOFF_STATP[8] + LOFF_STATN[8] + GPIO[4]`. This is exactly what our existing
`int24ToInt32` + `EegFrame` (statP/statN/gpio) already model — the parser just needs
to consume 27‑byte records instead of the §8 serial frame.

7. Stop: `write_path("/ads1299evm/acquire", <stop>)`; `Tclunk` fids; release/close.

## 5. macOS implementation plan (next phase)

Add a `core/acquisition/mmb0/` backend behind the existing `IDataSource`:
- `Styx9pClient` — minimal 9P2000 client (Tversion/Tattach/Twalk/Topen/Tread/Twrite/Tclunk
  + size‑prefixed marshalling). ~300 lines; portable from `estyx` convS2M/M2S or written fresh. Unit‑testable with a loopback.
- `Mmb0UsbTransport` — `libusb_bulk_transfer` over EP 0x01/0x81, 64‑byte rounding, rx queue.
- `Mmb0DataSource : IDataSource` — open → attach → write `DeviceConfig` registers →
  blocksize → acquire → read `/ads1299evm/data` → parse 27‑byte records → `framesReady`.
  Everything downstream (scaling, MonitorView, Recorder, Spectrum) is reused unchanged.
- Build dep: `libusb-1.0` (`brew install libusb`; CMake `find_package`/`pkg_config`).
- Wire MainWindow "Connect" to enumerate 0x0451/0x5718 and start this source.

Testable headlessly now (9P marshalling + record parsing); real‑hardware test needs the
MMB0 plugged into the Mac (it should enumerate as 0x0451/0x5718 with no driver install).

## 6. Open items to verify against the real device
- Exact `acquire` start/stop byte values and `blocksize_samples` width (read the LabVIEW
  `*.vi` constants or sniff once; or just try `1`/`0`).
- Whether registers must be written via individual `conf/*` files (per the index) or a
  bulk block — the index implies per‑register files.
- The `/version` and `/id` strings (handshake sanity).
- 9P `msize` the firmware accepts (start with 8192, honor Rversion).

## References (in `reference/ti-mmb0/`)
- `cstyx/USBConnection.cc` — USB transport (endpoints, framing).
- `cstyx/styxmsg.cc`, `cstyx/Client.cc`, `cstyx/Fid.cc`, `cstyx/File.cc` — 9P client.
- `cstyx/estyx/src/` — 9P message encode/decode (convS2M/M2S/D2M/M2D), device server model.
- `cstyx/cstyx.h` — high‑level `read_all_path`/`write_path` API.
- `docs/ADS1299EVM Styx Index.txt`, `docs/ADS1299 Register Map.txt`, `docs/usbstyx.inf`,
  `docs/mmb0_visa.inf`, `docs/ads1299evm-pdk.bin` (firmware), `docs/ads1299evm.ini`.
- `styxsh/styxsh.py`, `styxsh/dsploader.py` — a readable Python Styx shell (usage reference).
