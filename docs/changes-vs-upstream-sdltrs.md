<!-- /docs/changes-vs-upstream-sdltrs.md -->

# Changes vs. upstream sdltrs

How `sdltrsXebec` diverges from stock SDL2TRS ([`gitlab.com/jengun/sdltrs`](https://gitlab.com/jengun/sdltrs), `sdl2` branch), which is the true upstream.

The baseline for comparison is the upstream commit immediately before any hard-disk-controller work (`17f2f19d`, "Refactor check of changed bits"). Two layers sit on top of it:

1. **OMTI layer**, inherited from [`sdltrsOMTI`](https://github.com/Egbert-Azure/sdltrsOMTI): OMTI 5527 SASI controller emulation and its wiring.
2. **Xebec layer**, this fork: the Xebec S1410 controller, the shared image/dispatch refactor, and the single-active-controller model.

Upstream's `src/` holds 47 files: 32 byte-for-byte identical, 15 modified, none removed. Of the 15, 13 appear in the Source table below; the other two are `src/Makefile` and `src/BSDmakefile`. This fork adds 8 further files to `src/`, bringing it to 55. The two remaining modified build files, `CMakeLists.txt` and `Makefile.am`, sit at the repo root and so fall outside the 47. `configure.ac`, also root-level, is unchanged.

## Files added (not in upstream)

| File | Layer | What it is |
|------|-------|------------|
| `src/trs_omti.c` / `.h` | OMTI | OMTI 5527 SASI/MFM controller: phase state machine (idle → CDB → data → status), 6-byte CDBs, ports `0x40`–`0x43`. |
| `src/trs_xebec.c` / `.h` | Xebec | Xebec S1410 SASI controller with two host-adapter interfaces onto one core: Holte's CP/M adapter at `0x40`–`0x42`, and the TCS onboard adapter at `0x00`–`0x02` (256-byte sectors) that GDOS 2.4 uses. |
| `src/trs_hard_image.c` / `.h` | Xebec | Shared Reed-header `.hdv` open, geometry decode and sector-offset math (`hard_image_open` / `hard_image_offset`). Replaces three near-identical copies that had lived in the WD1000, OMTI and Xebec backends. |
| `src/trs_hdctl.c` / `.h` | Xebec | `(controller-type, unit)` dispatch over the three backends, plus the single active-controller setting (`hdctl_get_active` / `_set_active`). A real Genie IIIs is fitted with exactly one hard-disk controller. |

## Files modified (from upstream)

### Build

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add the four new sources (`trs_omti.c`, `trs_xebec.c`, `trs_hard_image.c`, `trs_hdctl.c`). Primary build. |
| `Makefile.am` | Same four sources, for the autotools build. |
| `src/Makefile`, `src/BSDmakefile` | OMTI-era only added `trs_omti.c`; not yet updated for `trs_xebec.c` / `trs_hard_image.c` / `trs_hdctl.c`. Known gap — use the CMake or autotools build. |

`configure.ac` is unchanged.

### Source

| File | +/− vs upstream | Why |
|------|-----------------|-----|
| `src/trs_io.c` | +47 / −2 | GENIE3S port dispatch for the SASI controllers (`0x40`–`0x43`, `0x00`–`0x02`) and the WD1000 remap, gated so only the active controller answers the bus (`hdctl_get_active()`). Replaces the earlier image-presence heuristic. |
| `src/trs_sdl_gui.c` | +125 / −51 | Hard Disk Management menu: controller selector, generic per-controller drive slots, create-disk routing to any controller, write-protect on all three controllers. `MENU.type` de-`const`ed so the menu can be built at runtime. |
| `src/trs_options.c` | +59 / −2 | CLI flags (`-omti0/1`, `-x0/1`, `-hard2/3`) and config keys (`omti%d`, `xebec%d`, `hardcontroller`), parsing and config-file writing. |
| `src/trs_hard.c` | +25 / −96 | WD1000/1010 backend refactored onto the shared `trs_hard_image` helpers (net shrink), plus a LUN/drive bounds guard. |
| `src/trs_hard.h` | +1 / −1 | `TRS_HARD_MAXDRIVES` 2 → 4 (WD1000's 2-bit SDH drive field). |
| `src/trs_mkdisk.c` | +25 / −25 | `trs_write_protect` routed through the `trs_hdctl` dispatch so it covers OMTI and Xebec, not just WD1000. |
| `src/trs_mkdisk.h` | +2 | `OMTI_DRIVE` / `XEBEC_DRIVE` menu-type constants. |
| `src/trs_state_save.c` | +8 / −1 | Save/load OMTI, Xebec and the active controller; state version bumped to 17. |
| `src/trs_state_save.h` | +4 | Declarations for the OMTI/Xebec save/load hooks. |
| `src/trs_disk.c` | +4 | `trs_disk_init` also powers on the OMTI and Xebec controllers. |
| `src/trs.h` | +2 | Declarations for `trs_omti_debug` / `trs_xebec_debug`. |
| `src/debug.c` | +9 | `zbx` commands `omtidump`/`od` and `xebecdump`/`xd` to print controller state. |
| `src/trs_memory.c` | +1 / −1 | Comment only: note that `trs_disk_init` now also inits the hard-disk controllers. |

Drive caps differ per controller. WD1000 is capped at 4 (`TRS_HARD_MAXDRIVES` in `trs_hard.h`, matching its 2-bit SDH drive field). OMTI and Xebec are capped at 2, the hardware limit of their 1-bit SASI LUN, and those caps live in the added `trs_omti.h` / `trs_xebec.h` rather than in any upstream file.

## Not touched

The Z80 core (`z80.c`), disassembler (`dis.c`), video/CRTC, keyboard, cassette, stringy-floppy, printer, UART, interrupt, clone-model and paste code are unchanged from upstream. The changes are confined to the hard-disk-controller path and the glue needed to wire it in (init, state-save, debug, GUI, options).
