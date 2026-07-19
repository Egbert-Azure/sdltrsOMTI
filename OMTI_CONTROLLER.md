# OMTI 5010 controller emulation — how it works

This describes `src/trs_omti.c`/`trs_omti.h`, the OMTI 5010-class SASI/MFM hard-disk-controller emulation added by this fork. If you're debugging a boot/read/write problem, start here to understand the protocol, then see `SESSION_STATUS.md` for the current investigation and `omti_protocol_bugs.md`/`omti_disk_geometry.md` (Claude memory) for the specific bugs already found and fixed.

## What it emulates

A TCS Genie IIIs can have an OMTI 5010 SASI/MFM hard-disk controller card (typically driving a Seagate ST225 or similar), mapped at Z80 I/O ports **0x40-0x43**. This is a completely separate, independent interface from the Western Digital WD1000/1010 controller emulated in `trs_hard.c` (ports 0xC8-0xCF, or relocated equivalents) — the two share no state, no drive numbering, and a Genie IIIs can have both attached simultaneously. The protocol here was reverse-engineered from Thomas Holte's real CP/M 3.0 BIOS driver (`hd2.mac`) and boot-loader source (`ldrbiohd.mac`), not from official OMTI documentation.

## Port map

| Port | Direction | Meaning |
|---|---|---|
| 0x40 (`TRS_OMTI_PORT`) | R/W | Data/command byte — CDB bytes clocked in here, sector data flows through here, final status byte read here |
| 0x41 (`TRS_OMTI_STATUS`) | R | Status register (see below) |
| 0x41 (`TRS_OMTI_STATUS`) | W | Software reset — any write aborts to idle |
| 0x42 (`TRS_OMTI_SELECT`) | W | SELECT strobe — begins a new command if idle |
| 0x42 (`TRS_OMTI_SELECT`) | R | Card-presence signature; always reads `0xFA` (checked by the boot EPROM at reset, never used by `hd2.mac` itself) |
| 0x43 (`TRS_OMTI_MASK`) | W | DMA/interrupt mask — stored but not otherwise emulated (no DMA/interrupts in this emulation) |

## Protocol: a phase state machine, not a register file

Unlike WD1000 (`trs_hard.c`), which is register-based (the guest pokes distinct track/sector/count registers directly), OMTI is **phase-based** — it models the real SASI bus handshake:

```
OMTI_PH_IDLE
    │  SELECT strobe (write to 0x42)
    ▼
OMTI_PH_CDB          — 6 bytes clocked in one at a time at port 0x40,
    │                  each gated by status bit 0 (TRS_OMTI_REQ)
    │  6th byte received → omti_command() decodes and dispatches
    ▼
OMTI_PH_DATA_IN  or  OMTI_PH_DATA_OUT     (sector data, or absent for some commands)
    │                  status 0xCB (DATA_IN, byte ready to read) or
    │                  0xC9 (DATA_OUT, byte wanted) until state.datalen bytes moved
    ▼
OMTI_PH_STATUS       — status 0xCF; guest reads final status byte at port 0x40,
    │                  which returns the bus to OMTI_PH_IDLE
    ▼
OMTI_PH_IDLE
```

The status register (port 0x41) is how the guest driver polls where things are in this sequence — `TRS_OMTI_IDLE` (0xC0), `TRS_OMTI_DATA_OUT` (0xC9), `TRS_OMTI_DATA_IN` (0xCB), `TRS_OMTI_STATUS_RDY` (0xCF). Bit 0 of the status byte (`TRS_OMTI_REQ`) is what the real driver's wait loops actually poll for ("controller ready for next byte").

## The 6-byte command descriptor block (CDB)

Standard SASI CDB layout, decoded in `omti_command()`:

| Byte | Contents |
|---|---|
| 0 | Command opcode |
| 1 | bit5 = LUN (which of the 2 addressable drives); bit7 = cylinder bit 10; bits0-4 = head |
| 2 | bits6-7 = cylinder bits 9-8; bits0-5 = sector |
| 3 | cylinder bits 7-0 |
| 4 | block count (unused — always one sector at a time here) |
| 5 | control byte (unused) |

Note the **flat sector addressing quirk**: the real compiled `hd2.mac` driver always sends head=0 in the CDB and lets the sector field run past the drive's actual sectors-per-track (0..`heads*secs-1`), expecting the controller to do the head/sector split itself. `omti_seek()` replicates this: `head += sector / d->secs; sector %= d->secs;` before bounds-checking. Don't "fix" this to expect a pre-split head/sector from the guest — it's intentional and matches real hardware/driver behavior.

## Command set

| Opcode | Name | What it does |
|---|---|---|
| 0x00 | `TEST_UNIT_READY` | Reports drive-present/ready (opens the image if not already open) |
| 0x01 | `REZERO` | Seeks to cylinder 0/head 0/sector 0 |
| 0x03 | `REQUEST_SENSE` | Acknowledged, no real sense data emulated |
| 0x04 | `FORMAT` | Writes `state.fillbuf` (see below) to the addressed sector |
| 0x08 | `READ` | Seeks, reads one sector from the image file into `state.buf`, enters `DATA_IN` |
| 0x0a | `WRITE` | Seeks, enters `DATA_OUT`; on completion writes `state.buf` to the image file |
| 0x0b | `SEEK` | Positions only, no data phase |
| 0x0c | `SET_CHARACTERISTICS` | Acknowledged (8-byte payload consumed) but **does not** change addressing geometry — see "Geometry" below |
| 0x0f | `WRITE_SECTOR_BUFFER` | Stages an 8-byte-aligned... actually one-sector fill pattern into `state.fillbuf`, used by real format tooling (e.g. `HDNDF.Z80`) before issuing `FORMAT` |

Anything else logs `"trs_omti: unknown command 0x%02X"` and returns an error status to the guest.

## Geometry: comes from the disk image, not from the guest

This is a common point of confusion, so it's worth stating plainly: **`cyls`/`heads`/`secs` are read once from the attached `.hdv` file's Reed header at open time (`omti_open()`) and never changed afterward**, even though `SET DRIVE CHARACTERISTICS` looks like it should be able to reprogram them. An earlier version of this emulation let that command overwrite the live geometry, reasoning that real OMTI hardware is "programmed" for its drive this way — this was wrong and got reverted (twice, across two sessions) because the real boot EPROM sends a stale, wrong characteristics block automatically at every startup, and letting it override geometry corrupted every subsequent seek. A real OMTI 5010 drive wired with N heads responds to CDB head values 0..N-1 regardless of what a boot ROM claims here; this field is more likely for write-precompensation/step-rate configuration, not addressable-head count. **Do not resurrect this.**

Sector size is always `OMTI_DEFAULT_SECSIZE` (512 bytes) — OMTI 5010 + ST-506/MFM drives (Seagate ST225 etc.) are natively 512 bytes/sector, unlike WD1000's live sector-size register.

## Disk image format

`.hdv` files use Matthew Reed's format (`src/reed.h`, `ReedHardHeader`): a 256-byte header (magic bytes `0x56 0xCB`, geometry, write-protect flag) followed by raw sector data — the same format `trs_hard.c` uses, read/written completely independently (no shared "disk image" abstraction). Geometry-to-file-offset mapping is a flat linear byte shift:

```
file_offset = 256 + ((cyl * heads + head) * secs + sector) * secsize
```

No interleave/skew reordering — unlike floppy `.dmk` images, which do need side/track reordering. `trs_mkdisk.c` can create blank images of this format.

## `FORMAT` / `WRITE_SECTOR_BUFFER` and the fill pattern

Real OMTI format tooling (e.g. Holte's `HDNDF.Z80`) stages a fill byte via `WRITE SECTOR BUFFER` (a normal 6-byte CDB + one sector's worth of data-out) *before* issuing `FORMAT TRACK`, which is expected to write that staged pattern to the addressed sector. `state.fillbuf` holds this pattern, defaults to `0xE5` (CP/M's empty-directory-entry marker) on poweron so `FORMAT` behaves sanely even if a guest never explicitly loads a buffer first, and gets overwritten by `WRITE_SECTOR_BUFFER`'s data-out completion. `FORMAT` writes `state.fillbuf`, not a hardcoded pattern.

## Key functions in `trs_omti.c`

- `trs_omti_init(poweron)` — power-on/reset; on `poweron` also opens any attached drive images and resets `fillbuf` to `0xE5`.
- `trs_omti_attach(drive, filename)` / `trs_omti_remove(drive)` — GUI/CLI attach points (`-omti0`/`-omti1`, Alt-H GUI screen).
- `trs_omti_in(port)` / `trs_omti_out(port, value)` — the actual Z80 I/O trap entry points, dispatched from `src/trs_io.c`.
- `omti_command()` — decodes a completed 6-byte CDB and dispatches to the command table above.
- `omti_seek(lun, cyl, head, sector)` — the flat-sector-to-CHS math plus bounds check plus `fseek()`.
- `omti_data_in()` / `omti_data_out(value)` — per-byte handling during `DATA_IN`/`DATA_OUT` phases (also handles CDB byte-clocking while in `OMTI_PH_CDB`).
- `omti_open(drive)` — reads the Reed header, sets `writeprot`/`cyls`/`heads`/`secs`, opens the file (falls back read-only on permission failure).

## Debugging

Boot with `-io 0xc` (`OMTIDEBUG1|2`, bits also shared with `trs_hard.c`'s equivalent flags) to log every port access and every decoded command:

```
grep "trs_omti: command\|ERROR" logfile
```

`trs_omti: command 0xNN lun:L cyl:C head:H sec:S` is printed **before** `omti_seek()`'s head/sector wraparound math runs, so a raw sector value ≥ the drive's sectors-per-track in that log line is expected for reads/writes/seeks and does not by itself indicate a bug — check where the actual file access lands, not just the raw logged fields. The `zbx` debugger (`-z`, `omtidump` command) can also dump live controller/drive state.
