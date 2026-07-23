<!-- /boot-eprom-controller-pairing.md -->

# Boot EPROM ↔ hard-disk controller pairing (Genie IIIs)

On a real TCS Genie IIIs the **boot EPROM and the hard-disk controller are a
matched pair**. In the emulator they are currently two *independent* settings
(the `romfile` you load, and which controller slot holds the `.hdv`), and
nothing cross-checks them — so it is possible to select a combination that
cannot work, which is exactly the "auto-select doesn't work" symptom that led
to this note.

## The two boot EPROMs

Both dumps live in `ROM/`:

| File | Size | EPROM | Role |
|------|------|-------|------|
| `g3s_8501004_bootrom_2732.bin` | 4 KB | 2732 | **standard** Genie IIIs boot ROM |
| `g3s_hd-omti_bootrom_2764.bin` | 8 KB | 2764 | **OMTI HD-boot** ROM (Arnulf Sopp's modification) |

The OMTI ROM is *double* the size (2764 vs 2732). That is Sopp doubling the
EPROM to fit the extra hard-disk-boot code — i.e. HD-boot support was bolted
onto the standard ROM, it was never in it. The size difference turns out to be
a clean, meaningful discriminator (see below).

## Confirmed facts

- **The standard EPROM talks only to the Xebec/TCS side, never to OMTI.**
  Its world drives the TCS onboard SASI adapter at ports `0x00`–`0x02` — the
  same interface GDOS 2.4's resident driver probes. OMTI lives on a different
  adapter (`0x40`–`0x42`, Holte's slot) and the stock ROM has no code for it.
  So **OMTI is unreachable without the OMTI ROM.** (Confirmed against the
  investigation in `xebec-s1410-story.md`.)

## Plausible but not yet byte-verified

- **OMTI ROM boot order: hard drive first, then floppy.**
- **Standard ROM boot order: floppy first** (effectively floppy-*only* for the
  boot step; GDOS reaches the Xebec HD only *after* the floppy boot, via its
  resident driver).

These match the hardware logic exactly — the whole point of an HD-boot EPROM
is "try the disk, fall back to floppy," while the stock ROM doesn't boot HD at
all. They have **not** been proven by disassembling the two `.bin` reset paths
yet. To settle it: load each ROM as the boot ROM and `dis 0000` via scripted
zbx, then read the boot sequence.

## The pairing (and why it isn't 1:1)

The boot ROM does **not** uniquely determine the controller — the standard ROM
serves two different controllers, decided by the OS on the floppy (which the
emulator cannot see):

| Boot ROM | + OS | Controller | Drive cap |
|----------|------|-----------|-----------|
| `8501004` (4 KB / 2732) | GDOS 2.4 | **Xebec** | 2 |
| `8501004` (4 KB / 2732) | CP/M | **WD1000** | 4 |
| `hd-omti` (8 KB / 2764) | — | **OMTI** | 2 |

So `8501004` → **Xebec *or* WD1000**; only the OMTI ROM is a clean 1:1.
A *full* auto-select is therefore impossible, but a **validity cross-check** is
straightforward — and it falls out of ROM size:

- **8 KB boot ROM (2764)** ⟹ OMTI-boot ⟹ controller **must** be OMTI.
- **4 KB boot ROM (2732)** ⟹ standard ⟹ controller **must** be Xebec or WD1000
  (OMTI is invalid); the OS picks which, so the user does too.

Drive caps (Xebec 2 / OMTI 2 / WD1000 4) already match this table in the
current build — no change needed there.

## Proposed emulator behaviour (not yet implemented)

A cross-check keyed on boot-ROM size could:

1. **Reject the impossible combos** — OMTI ROM + non-OMTI slot, or standard
   ROM + OMTI slot.
2. **Set a sane default** — OMTI ROM → OMTI; standard ROM → Xebec (the
   Genie IIIs / GDOS native case) — which fixes the auto-select miss, while
   still allowing a switch to WD1000 for CP/M.

Open design choices left to decide before building:

- **Strictness**: default-and-warn (allow overrides), hard-constrain the
  controller menu to valid choices, or warn-only with no auto-default.
- **ROM identification**: by size (4 KB vs 8 KB), by content hash of the two
  known dumps, or size with hash as a tiebreak.

## Practical workaround today (no code change)

Pick the boot ROM and the controller slot to agree:

- **GDOS 2.4 / Xebec:** load `g3s_8501004_bootrom_2732.bin` **and** attach the
  `.hdv` to the **Xebec** slot (`-xebec0`).
- **OMTI boot:** load `g3s_hd-omti_bootrom_2764.bin` **and** attach to the
  **OMTI** slot (`-omti0`).
- **CP/M / WD1000:** load `g3s_8501004_bootrom_2732.bin` **and** attach to the
  **WD1000** ("hard") slots (up to 4).

If the ROM and the slot disagree, nothing works — the disk simply isn't found.
