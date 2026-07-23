<!-- /xebec-s1410-story.md -->

# sdltrsXebec — emulating the Xebec S1410 for the TCS Genie IIIs

## Why this fork exists

`sdltrsXebec` is a fork of my own `sdltrsOMTI`, which in turn descends from Jens Günther's SDL2 port of xtrs. The goal is narrow: add emulation of the **Xebec S1410 SASI/MFM hard-disk controller**, because that is the controller G-DOS 2.4 and Klaus Kämpf's CP/M port for the TCS Genie IIIs actually drive.

The Genie IIIs world has three different hard-disk controllers, and I now emulate all three:

- **WD1000/1010** — register-based, up to four drives, addressed by SDH bits. Inherited from upstream.
- **OMTI 5527** — phase-based SASI, two drives via LUN. Added in `sdltrsOMTI`.
- **Xebec S1410** — also SASI, but not compatible with either of the others.

The Xebec is closer to the OMTI than to the WD1000, but "close" is not "same". When I first tried to boot G-DOS 2.4 against the existing OMTI emulation, the exchange got partway through a real SASI command and then the controller state fell apart. That failure pattern — a clean start followed by corruption — is what a near-miss protocol looks like, not a bug in the OMTI code. It was the evidence that pushed me to treat the Xebec as its own controller.

## A short SASI primer

SASI (the pre-standard ancestor of SCSI) is a handshake between a **host adapter** and a **controller** over a shared data bus plus a handful of status lines. The controller drives most of those lines; the host adapter reads them to know what phase the bus is in and which direction data is flowing. Four signals matter for this story, all qualified by the REQ strobe:

- **I/O (Input/Output)** — driven by the controller. Low means the controller is putting data onto the bus (toward the host); high means the host adapter drives data out. The host adapter watches this line to enable or disable its own bus drivers.
- **C/D (Command/Data)** — tells the host adapter whether the byte on the bus is a command byte (low) or a data byte (high).
- **BUSY** — the controller raises this in response to the host adapter's SEL pulse and address bits. It is the controller announcing "I'm here and ready."
- **MSG (Message)** — the controller raises this to say the current command has completed. While MSG is asserted, I/O is held low so the controller can drive the bus.

A command runs through phases in a fixed order: idle, then the command block goes out, then an optional data phase in or out, then a status/message phase, then back to idle. Both the Genie IIIs boards expose these raw SASI signals, so the same bit encoding shows up whichever adapter you talk to (REQ = 0x01, BSY = 0x02, C/D = 0x08, I/O = 0x10).

## How the emulation is built

I used the OMTI code as a structural template — the *shape*, not the protocol. Both are phase-based state machines driven by port I/O, and both read and write Matthew Reed's 256-byte-header `.hdv` format. Beyond that they differ.

The Xebec code (`src/trs_xebec.c` / `.h`) has one controller core behind **two host-adapter interfaces**:

- **Holte's CP/M adapter** at ports `0x40`–`0x42` — the same range the OMTI uses. On real hardware you only ever have one controller chip installed, so OMTI and Xebec are mutually exclusive on that slot. The port dispatch in `src/trs_io.c` routes `0x40`–`0x42` dynamically to whichever of the two has an image attached, rather than letting both claim the range. (`0x43`, the OMTI's DMA/mask register, has no Xebec equivalent and stays OMTI-only.)
- **The TCS onboard SASI adapter** at ports `0x00`–`0x02` — which, as it turned out, is the one that mattered.

The command opcodes, status bit-flags, and DCB block addressing came straight from the Xebec S1410A owner's manual and were cross-checked against Thomas Holte's CP/M 3.0 BIOS driver (`hd2.mac`). The status register is a real SASI bit-flag byte, not the single-magic-value-per-phase shortcut the OMTI code took; addressing is a flat logical block number translated to a file offset through the image's Reed-header geometry.

## The mystery: drives 5 and 6 that never appeared

For a long time G-DOS 2.4 simply refused to see its hard disk. `PD 5` and `PD 6` returned "Bauteil nicht erreichbar" every time, and nothing I did on the `0x40`–`0x42` side changed that.

The break came from a full-port I/O trace. One line repeated 1024 times:

```
[PC=F1BB] in (0x01) = 0xFF
```

That is a timeout loop. G-DOS keeps its resident hard-disk driver in high RAM at `F000`–`F4FF`, and at boot it runs an automatic probe. The probe was polling port **`0x01`** — the TCS onboard SASI adapter — reading `0xFF` (nothing there), and setting error code `0x0F` at `F1E4`. Holte's `0x40`–`0x42` ports belong only to *his* CP/M host adapter. G-DOS was knocking on an entirely different door than the one I had built.

I recovered the driver by scripting the `zbx` debugger over stdin (`sdl2trs … -zbx < script.txt`, with `stop f1bb` / `go` / `pe f000,f7ff` / `dis`). The breakpoint fires during the boot-time probe with no keyboard interaction, which sidestepped the fact that interactive `zbx` entry has never worked on this Mac. From the disassembly, the driver's real behaviour was unambiguous:

- **Selection** (`F1B6`): wait for BSY to clear on port `0x01`, write the controller ID `0x01` to port `0x00`, read it back to verify, pulse SEL on port `0x02`, then poll port `0x01` for BSY. Status bits on `0x01` are identical to Holte's.
- **Commands**: 6-byte SASI Class-0 CDBs built at `F1ED` (opcode, LBA, count, control), sent byte-by-byte on port `0x00` under REQ. Opcodes `0x00`/`0x01`/`0x03`/`0x04`/`0x06`/`0x08`/`0x0A`/`0x0F`.
- **Data**: **256-byte** sectors, moved as one auto-handshaked `INIR`/`OTIR` burst through port `0x00`. Status and message bytes follow.
- The boot probe is selection-only. On success it patches a driver vector at `F018`; the drive table is filled in later by the `PD`/SYS6 path.

Two flag bytes (`5996h` and `440Ch`) had eaten a lot of my earlier time as suspected configuration switches. The disassembly settled them: they are downstream consequences of a successful probe, not independent config. Once the probe worked, they set themselves.

## The fix

`trs_xebec.c` now exposes a second interface, `trs_xebec_tcs_in` / `_out`, at ports `0x00`–`0x02`, sharing the same controller state machine as the Holte path. Commands that come in over the TCS adapter use 256-byte sectors; the Holte path stays at 512. I added CDB block-count support for multi-sector reads and writes, plus the `FORMAT TRACK` and read/write-sector-buffer opcodes G-DOS uses. Both interfaces are routed in the `GENIE3S` blocks of `trs_io.c`, gated on which controller is active, and the state-save version is bumped to 16.

A successful selection now shows up in the log as the full handshake:

```
in 01=00 → out 00,01 → in 00=01 → out 02 → in 01=0B
```

That last byte, `0x0B` = BUSY | REQ | C/D, is the controller answering.

## Verification under real G-DOS 2.4

Tested interactively on `G3S-GDOS24.DMK` with `HDV/g3s-gdos24-omti-10mb.hdv`:

- `PD 5` and `PD 6` return real drive data instead of "Bauteil nicht erreichbar".
- `HDFORMAT` completes both passes ("Durchgang 1/2").
- G-DOS partitions the single physical unit into two logical drives with its own geometry table: **drive 5** at 40 tracks / 1536 units, **drive 6** at 163 tracks / 984 units. (Per the G-DOS 2.4 manual, drive 6 deliberately uses a non-standard format and can't be read by `DIRCHECK`.)
- `dir 5` and `dir 6` list directories.
- `copy reset/job:0 :5` followed by `list reset/job:5` writes a real file and reads it back — and the bytes land in the `.hdv` file itself and survive a reboot, which closes the write path.

All of this runs on the standard Genie IIIs hard-disk geometry. The working image is exactly 306 × 4 × 17 × 512 bytes plus a 256-byte header — the Tandon-style 306/4/17 layout of the stock 10 MB drive — so no special or regenerated image is needed.

The `*` in `PD 0` never appeared for drives 5 or 6. After all of the above works without it, it clearly marks something other than hard-disk detection and has no functional effect. I've stopped chasing it. The temporary memory instrumentation I used to watch the two flag bytes has been removed.

---

*Primary protocol reference: Xebec S1410A Owner's Manual. Driver facts derived from live disassembly of a booted `G3S-GDOS24.DMK` and cross-checked against Thomas Holte's `hd2.mac`.*