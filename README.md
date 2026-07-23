<!-- /README.md — sdltrs-MultiHDC -->

# sdltrs-MultiHDC

`sdltrs-MultiHDC` is a TRS-80 / TCS Genie IIIs emulator with **emulation of three
distinct hard-disk controllers**: the OMTI 5527, the WD1000/1010, and the
**Xebec S1410 SASI** controller. It is a fork of `sdltrsOMTI`, itself a fork of
[SDL2TRS](https://gitlab.com/jengun/sdltrs) / xtrs, with full history preserved.

The single remote is `origin` →
[`github.com/Egbert-Azure/sdltrs-MultiHDC`](https://github.com/Egbert-Azure/sdltrs-MultiHDC)
(the GitHub repo was renamed from `sdltrsOMTI`; old URLs auto-redirect). Development
happens on `main`.

This fork had **two goals**, both now done and verified:

1. **Boot Holte's CP/M 3.0 from hard disk on the OMTI controller**, using Arnulf
   Sopp's 1986 HD-boot EPROM — i.e. get the OMTI 5527 path working end-to-end from
   the boot ROM through the CP/M loader.
2. **Emulate the Xebec S1410** — the controller GDOS 2.4 and Klaus Kaempf's CP/M
   port for the TCS Genie IIIs actually use. The S1410 is closer to the OMTI 5527
   than to the WD1000, but compatible with neither.

Along the way the three hard-disk backends were unified onto shared image handling
and a single active-controller model.

![Holte CP/M 3.0 booting from hard disk on the OMTI controller with Arnulf Sopp's EPROM: BIOS b (851120, Thomas Holte 1985), CP/M V3.0 loader, 60K TPA, two Seagate ST 225 (21.4 MB) drives, C> prompt](images/holte-cpm3-hd-boot.png)

## Status: both goals working, verified end-to-end

**Goal 1 — OMTI + Holte CP/M 3.0 from hard disk** (the screenshot above): with Arnulf
Sopp's HD-boot EPROM, the OMTI 5527 path boots Thomas Holte's CP/M 3.0 straight off
the hard disk — RESBIOS3 / BNKBIOS3 / RESBDOS3 / BNKBDOS3 load, 60K TPA, two drives
recognized, `C>` prompt. See [`docs/HOWTO.md`](docs/HOWTO.md) and
[`docs/OMTI_CONTROLLER.md`](docs/OMTI_CONTROLLER.md).

**Goal 2 — Xebec S1410 under GDOS 2.4:**

![GDOS 2.4 booted on the emulator: `pd 0` showing drives 5 and 6, then `dir 5` and `dir 6` listing their directories](images/gdos24-drives-5-6.png)

`src/trs_xebec.c` / `.h` implements the Xebec S1410 controller core with **two
host-adapter interfaces** onto one shared SASI state machine:

- **Thomas Holte's CP/M host adapter** at ports `0x40`–`0x42` — the same range the
  OMTI uses. On real hardware only one controller chip is ever installed, so
  `src/trs_io.c` routes that range to whichever controller is active. This path uses
  512-byte sectors.
- **The TCS Genie IIIs onboard SASI adapter** at ports `0x00`–`0x02`, which is what
  GDOS 2.4's resident hard-disk driver probes at boot (found by live-disassembling
  the driver in high RAM with scripted `zbx`). This path uses 256-byte sectors, and
  was the missing piece behind the long-standing "drives 5/6 never recognized"
  problem.

Both interfaces share one phase-based SASI state machine (idle → CDB → data in/out →
status, with real REQ/BUSY/C-D/I-O status bits and command opcodes verified against
Holte's `hd2.mac`). All three controllers read and write the same Matthew Reed
`.hdv` header format (`src/reed.h`).

Under real GDOS 2.4: `PD 5` / `PD 6` return drive data, `HDFORMAT` completes both
passes, GDOS partitions the unit into logical drives 5 and 6, `dir` lists them, and
files copied to those drives persist inside the `.hdv` image across reboots.

## Hard-disk architecture

- **`src/trs_hard_image.c` / `.h`** — shared Reed-header `.hdv` open, geometry
  decode, and sector-offset math, used by all three backends.
- **`src/trs_hdctl.c` / `.h`** — `(controller-type, unit)` dispatch plus the
  **single active-controller** model. A real Genie IIIs is fitted with exactly one
  controller, so exactly one answers on the bus. The active controller is chosen
  explicitly (GUI / CLI / config) or auto-resolved from attached images
  (Xebec → OMTI → WD1000) when unset.
- **Drive caps** reflect each controller's real maximum: WD1000 = 4 (2-bit SDH drive
  field), OMTI / Xebec = 2 (1-bit SASI LUN).
- **Config / CLI**: `hardcontroller = wd1000|omti|xebec`; flags `-hard2`/`-hard3`,
  `-omti1`, `-xebec1`.

## Boot ROMs

On a real Genie IIIs the boot EPROM and the hard-disk controller are a matched pair
(see [`docs/boot-eprom-controller-pairing.md`](docs/boot-eprom-controller-pairing.md)).

- [`ROM/g3s_8501004_bootrom_2732.bin`](ROM/g3s_8501004_bootrom_2732.bin) — the
  standard 4 KB (2732) Genie IIIs boot ROM, the genuine Xebec-speaking ROM.
- [`ROM/g3s_hd-omti_bootrom_2764.bin`](ROM/g3s_hd-omti_bootrom_2764.bin) — Arnulf
  Sopp's 1986 8 KB (2764) retrofit that boots from hard disk; the original OMTI port
  addresses (`0x40`–`0x43`) came from disassembling this ROM.

## Building

```sh
mkdir -p build && cd build && cmake .. && cmake --build .
```

or

```sh
./autogen.sh && ./configure --enable-zbx --enable-readline && make
```

## Documentation

Longer references and the investigation history live in [`docs/`](docs):

| Document | What it covers |
| --- | --- |
| [xebec-s1410-story.md](docs/xebec-s1410-story.md) | Narrative overview: why this fork exists, a SASI primer, how the emulation is built, the drives-5/6 mystery and its fix |
| [HOWTO.md](docs/HOWTO.md) | Running the emulator with a working hard disk: build, image selection, launching, booting, GUI hard-disk management |
| [OMTI_CONTROLLER.md](docs/OMTI_CONTROLLER.md) | The OMTI 5527 controller emulation (`trs_omti.c`/`.h`) and its protocol |
| [boot-eprom-controller-pairing.md](docs/boot-eprom-controller-pairing.md) | How the two boot EPROMs pair with the hard-disk controllers |
| [MODIFICATIONS.md](docs/MODIFICATIONS.md) | File-by-file record of what this fork changed in the C source, and why |
| [changes-vs-upstream-sdltrs.md](docs/changes-vs-upstream-sdltrs.md) | How the fork diverges from stock SDL2TRS (files added / modified / untouched) |
| [G-DOS 2-4.md](docs/G-DOS%202-4.md) | Overview of G-DOS 2.4 and its cross-Genie model detection |
| [gdos-screen-formats.md](docs/gdos-screen-formats.md) | G-DOS 2.4 screen formats |
| [Installation des Holte CPM-Plus.md](docs/Installation%20des%20Holte%20CPM-Plus.md) | Installing Holte's CP/M Plus on the Genie 3s (German) |
| [Xebec SASI:MFM S1510A Signal DEf.md](docs/Xebec%20SASI%3AMFM%20S1510A%20Signal%20DEf.md) | Xebec SASI/MFM S1510A bus signal definitions |
| [Xebec S1410A Owner Manual_text.pdf](docs/Xebec%20S1410A%20Owner%20Manual_text.pdf) | Primary protocol reference — the Xebec S1410A owner's manual |
| [gdos_auszug-aus-manual.pdf](docs/gdos_auszug-aus-manual.pdf) · [geniecpm.pdf](docs/geniecpm.pdf) | Scanned manual excerpts (G-DOS, Genie CP/M) |

See also [`changelog.md`](changelog.md) for the dated development history.

## License

[BSD 2-Clause](LICENSE), inherited from the upstream SDL2TRS / xtrs projects (see
[xtrs.LICENSE](xtrs.LICENSE) for the xtrs-derived portions).
