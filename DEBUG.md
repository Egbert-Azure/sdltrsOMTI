<!-- /DEBUG.md — OMTI hard-disk boot: status and debugging notes -->

# OMTI hard-disk boot — status and debugging notes

*Updated 2026-07-19. Read this before changing code or disk images.*

## Goal

Boot a TCS Genie IIIs directly from an OMTI-emulated hard disk: a genuine boot with no floppy inserted, using the real boot EPROM (`g3s_hd-omti_bootrom_2764.bin`). The OMTI 5527 SASI/MFM controller emulation lives in `src/trs_omti.c` / `.h` at ports 0x40–0x43. (The controller is a 5527, not the "5010" it was first labelled; the `DISKIO.MAC` header comment reads "operated with OMTI 5527".) Floppy boot (`egcpm01.dmk` / `egcpm02a.dmk` via the standard ROM) already works fully and is the supported path; the harder target was the zero-floppy EPROM path.

## Status: done — zero-floppy direct EPROM boot works

As of 2026-07-19, direct EPROM boot with no floppy attached reaches a working `C>` prompt. On screen: the full `GENIE IIIs SYSTEM / Version b of BIOS (851120) / Thomas Holte @ 1985` banner, the `CP/M V3.0 Loader` banner, all four system components (`RESBIOS3`, `BNKBIOS3`, `RESBDOS3`, `BNKBDOS3`), `60K TPA`, then `C>`. No OMTI errors throughout (`HDV/g3s-omti-WORKING.hdv`; the log shows 0 `ERROR` lines across ~190K lines).

### Root cause

`COPYSYS.COM`'s write of `CPM3.SYS` onto the hard disk (via `SYSTEM.SUB`/`GENCPM`) places the file's real bytes at a location inconsistent with the directory entry it creates to describe them (full characterization below). This is a `COPYSYS.COM`/`GENCPM` bug — or at least an inconsistency that neither this fork nor the original hardware ever exercised over this exact zero-floppy path — not a `trs_omti.c` bug.

The fix is a disk-image workaround, not a code change: take a hard disk built normally via `SYSTEM.SUB` (its boot/loader image at cyl 0–1 is written correctly; only `CPM3.SYS`'s placement is wrong), then overwrite `CPM3.SYS`/`CCP.COM`'s data and directory entries with cleanly extracted copies of those exact files from the source floppy (`egcpm02a.dmk`), placed at simple, block-aligned locations with a standard single-entry (`ex=0`) directory encoding.

### Recipe

`dmk-working/build_working_hdv.py` (see its docstring for exact steps). In short:

1. Build a target `.hdv` normally: `egcpm34.dmk` (A:) + `egcpm02a.dmk` (B:) + `SYSTEM.SUB` against the standard ROM.
2. Extract `CPM3.SYS`/`CCP.COM` from `egcpm02a.dmk` directly via `cpmextract.py` (not from the built hard disk).
3. Run `build_working_hdv.py BASE_HDV CPM3_SYS CCP_COM OUT_HDV`.
4. Boot `OUT_HDV` with no floppy via `g3s_hd-omti_bootrom_2764.bin`.

### Not done

`COPYSYS.COM`/`GENCPM.COM` are proprietary DR binaries with no source. The bug inside them — why the directory entry and the real write location disagree — was never root-caused at the Z80-instruction level, only worked around. Making `SYSTEM.SUB` produce a directly bootable disk with no manual patch step would need either a `COPYSYS`/`GENCPM` invocation that avoids the bug, or live Z80 debugging of those binaries. Not planned; the workaround is sufficient and repeatable.

## Follow-up fixes

The first working boot had two more issues, both fixed in `build_working_hdv.py`:

1. `dir c:` showed garbage entries after the two real files. This was real leftover data in the directory area (blocks 0–15) from the buggy `COPYSYS` write — specifically the misplaced `CPM3.SYS` fragment that used to sit at file offset `0x1ed00`, inside the 16-block directory-reserved region. The build script only cleaned blocks 16–22 (where it places the real files) and never wiped the rest. Fixed: the script now wipes the entire 16-block directory area to `0xE5` before writing the two real entries. `dir c:` now shows exactly `CPM3.SYS`/`CCP.COM` and nothing else.

2. D: read as garbage, and the boot banner shows `Seagate ST 225 - 21.4 MB` twice. The banner is genuine: `HD2.MAC:330` has `iniok: db 'Seagate ST 225 - 21.4 MB'`, one hardcoded string printed once per successfully initialized logical drive (once for C:, once for D:), never parameterized per partition in Holte's original source. Real hardware showed the same. D: was simply out of scope of the original `.hdv` — a 1 MB-truncated test image, fine for C:, but D:'s region (`DPBHD2` in `DISKIO1.MAC`, `OFF=307` cylinders, right where C: ends) didn't exist in the file, so reads past EOF returned zero bytes, which CP/M misreads as directory garbage rather than empty. Fixed: the script now extends the output to the full 615-cylinder (~21.4 MB) size, padded with `0xE5`. D: needs no files (blank data partition); `dir d:` now shows `No File`.

Both verified in one clean boot: `dir c:` shows only the two real files, `dir d:` shows `No File`, zero OMTI errors. `HDV/g3s-omti-WORKING.hdv` was rebuilt with both fixes and is the canonical working image (21,412,096 bytes, full size).

## End-to-end confirmation: interactive file write

After the fixes, I booted `HDV/g3s-omti-WORKING.hdv` with `dmk-working/egcpm02a.dmk` also attached as A: (still the zero-floppy-capable raw EPROM boot; the floppy was added only for convenience, the hard-disk boot itself used no floppy), then used `COPY.COM` (not `PIP.COM`, which isn't on this floppy) to copy a real file from A: to C:. It wrote successfully — the first file written to C:. That is hands-on proof the whole chain works: boot, BDOS, directory allocation, and file write all functioning on a live OMTI hard disk, not just a scripted test. `HDV/g3s-omti-WORKING.hdv` now contains that file; it is a live disk, not a pristine template, so back it up before further experimentation if you want a clean baseline.

Usage guide: `HOWTO.md` (repo root) covers build instructions, which `.hdv` to use, boot commands (direct hard-disk and floppy+hard-disk), `COPY.COM` usage, GUI hard-disk management, and the known-quirks list. That file is for day-to-day use; this one is the debugging history.

## Two emulator bugs found and fixed (commit `283601dc`, "OMTI routine bug")

1. `SET DRIVE CHARACTERISTICS` must not override addressing geometry. (This reverted an earlier, overly broad fix.) The raw boot EPROM sends a stale 612-cyl/2-head characteristics block at startup, before touching the disk; letting it overwrite `d->cyls`/`d->heads` corrupted every subsequent seek. Real OMTI hardware doesn't use this field to restrict addressable heads (confirmed from real hardware). Current behavior: acknowledged (`omti_finish(0)`), geometry stays keyed to the attached image's Reed-header values. Do not re-revert this — it was tried twice and confirmed wrong both times.

2. `FORMAT TRACK` (0x04) always wrote zeros instead of the guest's fill pattern, and `WRITE SECTOR BUFFER` (0x0F) — the command real formatting tools use to stage that pattern — wasn't implemented (it fell through to `default:`, logged "unknown command 0x0F", returned an error). Found while trying to run the real `HDNDF.Z80`/`HDNDF.COM` formatter (the disk had never been properly formatted); it would have failed immediately at its `WRITE SECTOR BUFFER` step. Fixed: added `TRS_OMTI_WRITE_SECTOR_BUFFER` (0x0f), a persistent `state.fillbuf` (defaults to `0xE5` on power-on), and `FORMAT` now writes `state.fillbuf` instead of a hardcoded zero buffer.

## Geometry and DPB fields (settled)

- 615 cyl / 4 heads / 17 sectors per track / 512 bytes per sector. Confirmed by three independent period sources (`hddtbl.asm`, `hd2.mac`'s `fdhead`, `HDNDF.Z80`'s header comment) and by the compiled driver on the disk in use.
- `OFF` (reserved system tracks) = 2, for both the boot-time DPB (`DPB0` in `LDRBIOHD.MAC`) and the runtime DPB (`DPBHD0` in `DISKIO1.MAC`). This reverses an earlier conclusion that set boot-time `OFF` to 0 (wrong — it produced a different, seemingly more specific CPMLDR error that looked like progress but wasn't). Confirmed by direct raw-byte inspection: `CPM3.SYS`/`CCP.COM`'s valid directory entries live at file offset `0x11100`, matching cylinder 2 (`256 + 2×34816`), i.e. `OFF=2`. With boot-time `OFF=0`, CPMLDR scanned cylinders 0–1 (pure boot/loader code, no directory) and never found the entry ("failed to open"). With `OFF=2`, CPMLDR finds it immediately (reads one directory sector, then jumps to the computed data-block address).
- `DSM`/`DRM` in boot-time `DPB0` also needed correcting from stale values (2591/1024) to match the runtime DPB (5226/2047); that part of the earlier fix was correct and stands.
- Methodology note: boot-time `DPB0`'s compiled-on-disk values differ from the `.MAC` text source until the text source itself is patched. `SYSTEM.SUB` reassembles `LDRBIOHD.MAC` fresh via M80 on every rebuild, so a binary-only patch to an already-compiled `.REL`/boot-image copy is silently discarded on the next rebuild. Apply all `DPB0` fixes as text edits within the `.MAC` source file's own on-disk bytes — use `cpmextract.py`'s `CpmDisk`/`DPB01`/`decode_directory` helpers to locate the exact physical DMK sector, then a CRC-16/CCITT-safe patch (seed `0xCDB4`, computed over the DAM byte plus 512 data bytes) — not to any already-compiled binary elsewhere on the disk. A binary-only `OFF` revert that silently had no effect on the next rebuild cost a wasted round.

## The `src ST 225/egcpm001/` reference folder isn't fully trustworthy

`src ST 225/egcpm001/HD2.MAC` (the repo's reference copy) has `fdhead: cp 26` (wrong), while the actual compiled `HD2.MAC` on the working disk (extracted live via `cpmextract.py`) has `cp 17` (correct, matching `LDRBIOHD.MAC`). The reference folder was consolidated from a different disk/version than the one under test. Re-verify anything precision-critical by extracting live from the working DMK (`cpmextract.py path/to/egcpm02a.dmk -o outdir`), not from the `src ST 225/egcpm001/` copies.

## Root-cause detail: CPM3.SYS's real bytes don't match its directory entry

Characterized but not fixed at the instruction level (worked around by the recipe above):

- `CPM3.SYS`'s directory entry (found correctly once `OFF=2` was restored) declares block pointers `[16,17,18,19,20,21]`.
- Block 16 (file offset `0x21100`–`0x22100`) is always, reproducibly, entirely blank (`0xE5`) — across multiple rebuilds, whether the disk was hand-filled with `0xE5` or formatted with real `HDNDF` (after the FORMAT fix). It is never written.
- The real `SYSTEM.SUB`/`COPYSYS`/`GENCPM` write trace shows the `CPM3.SYS` payload landing in two places: a 4096-byte chunk at file offset `0x1ed00` (not block-aligned — 3072 bytes into block 13) containing `CPM3.SYS`'s genuine header/signature (`\xfe\x0c\xe0?\x00\xf8...` followed by "Copyright (C) 1982, Digital Research", byte-for-byte structurally matching the floppy's reference `CPM3.SYS`), and a cleanly block-aligned run from `0x22100` (block 17) through roughly `0x26d00` (within block 21).
- Attempted workaround: patched the directory entry's block-pointer list from `[16..21]` to `[17..21]` (dropping the bogus leading block 16) on `HDV/g3s-omti-fixed5.hdv`. Mechanically it worked — CPMLDR jumped straight to block 17 and read all 5 blocks (40 sectors, zero OMTI errors), covering the dense-content region — but it still failed with the same "failed to read CPM3.SYS". Block 17's first bytes are zeros, not the "Copyright..." header, so block 17 is mid-file, not the true start; and the true start (`0x1ed00`) is non-block-aligned, so no standard CP/M directory entry can correctly describe the file's real location.
- This points at a bug inside `COPYSYS.COM` itself (proprietary, no source): an inconsistency between where it writes `CPM3.SYS`'s bytes and how it computes the directory entry — not fixable from the emulator side or by further disk-image patching.
- Next diagnostic step (not yet attempted): live Z80 breakpoint tracing inside `CPMLDR.COM` (via `zbx`, e.g. `break <addr>` at its directory-search/open routine) to see exactly what validation triggers "failed to read", and whether a smarter workaround exists — CPMLDR may not require the file to start at the first block it reads, worth checking before concluding it's unfixable.

The boot goal is already met by the recipe workaround; this detail is retained for reference.

## Historical note

Direct EPROM boot originally got through `HDBOOTER` relocation, real `CPMLDR` execution (which prints its genuine "CP/M V3.0 Loader / Copyright (C) 1982, Digital Research" banner), correctly located `CPM3.SYS`'s directory entry, and read the blocks that entry pointed to (zero OMTI-level I/O errors), but failed with `CPMLDR error: failed to read CPM3.SYS`. The characterization above narrowed that to the `COPYSYS.COM` bug.

## What works (validated)

- Direct hard-disk boot, no floppy, raw EPROM (`g3s_hd-omti_bootrom_2764.bin`) → working `C>`. See `HDV/g3s-omti-WORKING.hdv` and `dmk-working/build_working_hdv.py`.
- Floppy boot (`egcpm01.dmk`/`egcpm02a.dmk` via the standard ROM `g3s_8501004_bootrom_2732.bin`) → real `hd2.mac` driver → full OMTI read/write round-trip (`dir c:`, `PIP`, read-back all confirmed).
- `egcpm02a.dmk`'s real `SYSTEM.SUB` builds and installs a working boot/loader image (cyl 0–1) onto a target OMTI disk cleanly and repeatably (94 write commands, 0 errors, every time). This part of `COPYSYS`'s job is correct; only its `CPM3.SYS` placement is buggy.
- Alt-H GUI screen shows `omti0`/`omti1` status correctly.

## Leads, resolved or open

1. Controller is an OMTI 5527, not "5010" — confirmed from the `DISKIO.MAC` header comment ("Version for Egbert Schröer hard disk Seagate ST225, 21.4 MB, ST412 interface, MFM format, operated with OMTI 5527"). Corrected throughout the code and docs. Resolved.
2. Original archived DMKs having a historically working `$TEMP$`/`CPM3.SYS` — this led to the fix, though not as first framed: rather than a leftover `$TEMP$`, it was re-extracting `CPM3.SYS`/`CCP.COM` cleanly from the source floppy `egcpm02a.dmk` (via `cpmextract.py`, bypassing `COPYSYS`'s buggy hard-disk write) and placing them correctly. Resolved / superseded.
3. `SYSTEM.SUB`'s last line `copy a:ccp.com c:` — not separately investigated; moot now, since `CCP.COM` is pulled directly from `egcpm02a.dmk` by the build script. Not blocking.
4. Possible cylinder-count typo in "Installation des Holte CPM-Plus.md" — not investigated. Worth a look since geometry (615 cyl) underpins everything, but direct boot no longer depends on it. Open, lower priority.

## Notes for resuming

- Working recipe: `dmk-working/build_working_hdv.py` (see docstring) — build a target `.hdv` via `SYSTEM.SUB`, extract `CPM3.SYS`/`CCP.COM` fresh from `egcpm02a.dmk` via `cpmextract.py`, then run the script for a directly bootable image.
- `HDV/g3s-omti-WORKING.hdv` is the confirmed working image (gitignored, like all of `HDV/`, full 615-cyl/21.4 MB). Boots to `C>` with no floppy via `g3s_hd-omti_bootrom_2764.bin`; `dir c:` shows only the two real files, `dir d:` shows `No File`.
- Working DMK copies live at `dmk-working/egcpm02a.dmk` and `dmk-working/egcpm34.dmk` (repo root, gitignored) with the `OFF=2`/`DSM`/`DRM` `LDRBIOHD.MAC` text fixes already applied. Always work from these repo-root copies.
- `egcpm34.dmk` is the safe bootstrap floppy (doesn't auto-try booting C:); `egcpm02a.dmk` has the real `SYSTEM.SUB`. To rebuild the boot-image base for `build_working_hdv.py`: `egcpm34.dmk` on disk0, `egcpm02a.dmk` on disk1 (B:), the standard ROM `g3s_8501004_bootrom_2732.bin`, and a target `.hdv` on `-omti0`.
- Fresh blank target for the `SYSTEM.SUB` base step: 256-byte Reed header (reuse from any existing `g3s-omti-*.hdv`) plus a `0xE5`-filled data area. 1 MB is enough headroom for this intermediate; `build_working_hdv.py` extends the final output to the full 615-cyl/21.4 MB itself, needed for D:.
- Debug with `-io 0xc` and `grep "trs_omti: command\|ERROR"` on the log.
- `cpmextract.py` (`~/Documents/GitHub/cpmextract/cpmextract.py`) extracts files live from any DMK — use it instead of the `src ST 225/egcpm001/` reference copies when precision matters.
- Never attach a file directly from `~/Documents/GitHub/GenieIIIs/` — always work from copies (`dmk-working/` in the repo root, or a throwaway scratch copy).
