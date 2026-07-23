<!-- /CHANGELOG.md -->

# Changelog

## 2026-07-22 — Hard-disk backend unification & single active controller

### Added

- `trs_hard_image.c/.h`: shared Reed-header `.hdv` open, geometry decode and
  sector-offset math, previously copy-pasted in `trs_hard.c` (WD1000), `trs_omti.c`
  (OMTI 5527) and `trs_xebec.c` (Xebec S1410). The three backends now keep only
  their own controller state machines.
- `trs_hdctl.c/.h`: a `(controller-type, unit)` dispatch over the three backends,
  plus a single **active controller** setting (`hdctl_get_active` / `_set_active`).
  A real Genie IIIs is fitted with exactly one controller; the active one is chosen
  explicitly (GUI / CLI / config) or auto-resolved from attached images
  (Xebec → OMTI → WD1000) when unset.
- Hard Disk Management menu: a **Controller** selector (GENIE3S only) toggling
  WD1000 / OMTI / Xebec, with generic drive slots for whichever is active.
- Config key `hardcontroller = wd1000|omti|xebec`; CLI flags `-hard2`/`-hard3`,
  `-omti1`, `-xebec1` for the newly addressable drives.

### Changed

- Only the active controller answers on the GENIE3S bus. `trs_io.c` now gates its
  hard-disk port dispatch on `hdctl_get_active()` instead of the old
  image-presence `xebec_active()` heuristic; the other controllers are inert.
- Drive caps raised to each controller's real maximum: WD1000 = 4 (2-bit SDH drive
  field), OMTI / Xebec = 2 (1-bit SASI LUN).
- State-save version bumped to 17 (saves the active controller; wider drive arrays).

### Fixed

- "Create Hard Disk Image → Insert into Drive" only ever attached to WD1000 slots,
  whichever slot was picked; it now targets the chosen controller.
- The Space-key write-protect toggle silently ignored OMTI/Xebec rows (the handler
  guarded on `type < ENTRY`, excluding them, and passed the wrong drive index) and
  `trs_write_protect` had no OMTI/Xebec cases at all. Both fixed via the dispatch.

## 2026-07-22 — GDOS 2.4 hard-disk support (XEBEC/TCS host adapter)

### Fixed

- GDOS 2.4 hard-disk access, now working end-to-end (format, directory, read, write). GDOS talks to its hard disk on I/O ports 0x00–0x02, not the 0x40–0x42 ports used by Holte's CP/M-era adapter. The emulator now answers on 0x00–0x02, so GDOS's boot-time probe succeeds and the drive is usable.

### Added

- A second host-adapter interface in `trs_xebec.c` (`trs_xebec_tcs_in` / `trs_xebec_tcs_out`) at ports 0x00–0x02, driving the same controller core as the existing Holte-era path. Sectors are 256 bytes on this path; 512 bytes remains on Holte's 0x40–0x42 path.
- CDB block-count support and the `FORMAT TRACK` / sector-buffer opcodes GDOS uses.
- Dispatch for the new ports in `trs_io.c`'s GENIE3S block.

### Changed

- State-save version bumped to 16.

### Background

GDOS 2.4's resident hard-disk driver lives in high RAM (F000–F4FF). At boot it runs a SASI selection against the Genie IIIs onboard host adapter: write the controller ID to port 0x00, verify, pulse SEL on port 0x02, then poll port 0x01 for BUSY. With those ports unimplemented it read 0xFF (nothing present) and set error code 0x0F ("Bauteil nicht erreichbar").

The visible symptom was an `IN (0x01)` at PC=F1BB repeating 1024 times in the log — the driver's timeout loop. The two previously unexplained flags (5996h / 440Ch) turned out to be downstream consequences of the probe result, not independent configuration.

The driver was recovered by scripting `zbx` over stdin (`-zbx < script.txt`, with `stop f1bb` / `go` / `pe` / `dis`); the breakpoint fires during the automatic boot-time probe, no interaction needed. Disassembly showed 6-byte SASI Class-0 CDBs (opcodes 00/01/03/04/06/08/0A/0F), 256-byte sectors transferred as single auto-handshaked INIR/OTIR bursts, and status bits identical to Holte's board (both expose raw SASI signals).

A successful selection logs: `in 01=00 → out 00,01 → in 00=01 → out 02 → in 01=0B` (BUSY | REQ | C/D — controller answered).

### Verified

- `HDFORMAT` completes both passes ("Durchgang 1/2") — the FORMAT commands and sector-buffer setup return clean completion statuses.
- `dir 5` and `dir 6` return real directory listings (multi-sector 256-byte reads, correctly addressed). GDOS partitions the single physical Xebec unit into two logical drives with its own geometry table: drive 5 = 40 tracks, drive 6 = 163 tracks.
- Write path: a file copied onto drive 5 is written and reads back via `dir 5` in the same session.

The `*` in `PD 0` is not the hard-disk detection marker. Drives 5 and 6 mount, format, and list directories without it, so the `*` marks something else (likely the boot/system drive or a floppy-specific state).

Confirmed the written bytes persist in the `.hdv` file, not just the sector buffer: drive 5, quit the emulator, reboot, and check the file survives via `dir 5`. Same with drive 6. Then copied from 5 to 6 and vice versa. KILL files, write again.

GENDIR to delete and create DIR again for 5 and 6. Copy  files again.
Success