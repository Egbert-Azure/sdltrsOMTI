<!-- /MODIFICATIONS.md — modifications from upstream sdltrs/xtrs -->

# Modifications from upstream sdltrs/xtrs

What this fork changed in the emulator's C source, file by file, and why. For how the resulting OMTI protocol emulation works, see `OMTI_CONTROLLER.md`; for day-to-day usage, see `HOWTO.md`.

## New files (don't exist in upstream at all)

### `src/trs_omti.c` / `src/trs_omti.h`

The entire OMTI 5527 SASI/MFM hard-disk-controller emulation, and the point of the fork. Genie IIIs machines could be fitted with an OMTI 5527 card (typically driving a Seagate ST225), a different interface from the WD1000/1010 controller (`trs_hard.c`) that upstream sdltrs already emulated. Phase-based SASI protocol (CDB clocked in 6 bytes at a time, then a data phase, then a status phase) at I/O ports 0x40–0x43, independent of and running alongside WD1000. See `OMTI_CONTROLLER.md` for the full protocol writeup; it was validated against genuine 1990s driver software, which surfaced several real controller bugs, since fixed.

## Existing sdltrs files modified to wire OMTI in

### `src/trs_io.c`

Added four `case` entries to the port-dispatch `switch` statements for both `IN` and `OUT` (mirrors how the pre-existing WD1000 ports are dispatched right next to them):

```c
case 0x40: /* TRS_OMTI_PORT */
case 0x41: /* TRS_OMTI_STATUS */
case 0x42: /* TRS_OMTI_SELECT */
case 0x43: /* TRS_OMTI_MASK */
  trs_omti_out(port, value);   /* and trs_omti_in(port) on the read side */
  break;
```

This is the only place that connects the Z80's `IN`/`OUT` instructions to `trs_omti.c`; without it the controller code would exist but never receive any port traffic. Ports 0x40–0x43 are Genie-IIIs-specific (relocated from where a stock Model I/III/4 would have other peripherals), in the same model-conditional dispatch block as WD1000's own relocated ports (0x50–0x57 internally, remapped to 0xC8–0xCF).

### `src/trs_options.c`

Added `-omti0`/`-omti1` as recognized command-line options (`opt_file` type, same pattern as the existing `-hard0`..`-hard3`), the code that calls `trs_omti_attach`/`trs_omti_remove` when those flags are parsed or when a slot is cleared, and a line in the config-file writer (`~/.sdltrs.t8c`) so attached OMTI images persist across runs the same way floppy and WD1000 attachments already did.

### `src/trs_sdl_gui.c` / `src/trs_mkdisk.h`

Added `omti0`/`omti1` as two more rows on the existing "Hard Disk Management" screen (Alt-H), alongside the four WD1000 rows it already had. This needed a new `OMTI_DRIVE` menu-entry-type constant (`trs_mkdisk.h`) to tell the GUI's generic menu code that these two rows are OMTI, not WD1000, plus small additions to the screen's `DELETE`/`INSERT`/`RETURN`/`TAB` key handlers and its geometry-refresh check so attach, detach, and insert work on OMTI rows just as they already did on WD1000 rows. Without this, OMTI drives were only attachable via the `-omti0`/`-omti1` CLI flags, with no way to see or change them while the emulator was running.

### `CMakeLists.txt` / `Makefile.am` / `meson.build`

Added `trs_omti.c` to each build system's source-file list. All three build systems exist in parallel in this codebase and have to be kept in sync by hand (there's no single source of truth): miss one and that build system silently produces a binary with no OMTI support at all, and no compile error.

## Everything else in `src/`

`z80.c`, `trs_memory.c`, `trs_hard.c`, `trs_cassette.c`, `trs_interrupt.c`, `trs_sdl_interface.c`, `trs_sdl_keyboard.c`, `trs_disk.c`, `trs_printer.c`, `trs_stringy.c`, `trs_uart.c`, `trs_cp500.c`, `trs_clones.c`, `main.c`, `debug.c`, `dis.c`, `error.c`, and so on: untouched, byte-for-byte inherited from upstream xtrs/SDL2TRS. None of the OMTI work required touching the Z80 core, the memory system, or any other peripheral emulation.