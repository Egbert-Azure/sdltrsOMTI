<!-- /README.md — sdltrsXebec -->

# sdltrsXebec

`sdltrsXebec` is a fork of [`sdltrsOMTI`](https://github.com/Egbert-Azure/sdltrsOMTI) (itself a fork of [SDL2TRS](https://gitlab.com/jengun/sdltrs)/xtrs), cloned with full history preserved. `git remote -v` shows `sdltrsomti-upstream`, renamed from `origin` so nothing here can accidentally push back to that project.

The goal is to emulate the **Xebec S1410 SASI hard-disk controller** — the controller GDOS 2.4 and Klaus Kaempf's CP/M port for the TCS Genie IIIs actually use — alongside the OMTI 5527 and WD1000/1010 controllers `sdltrsOMTI` already emulates. The S1410 is closer to the OMTI than to the WD1000, but compatible with neither.

## Status: working, verified end-to-end under real GDOS 2.4

![GDOS 2.4 booted on the emulator: `pd 0` showing drives 5 and 6, then `dir 5` and `dir 6` listing their directories](image.png)

`src/trs_xebec.c` / `.h` implements the S1410 controller core with two host-adapter interfaces onto it:

- **Thomas Holte's CP/M host adapter** at ports `0x40`–`0x42`, the same range the OMTI uses. On real hardware only one controller chip is ever installed, so `src/trs_io.c` routes that range to whichever of the two has an image attached.
- **The TCS Genie IIIs onboard SASI adapter** at ports `0x00`–`0x02`, which is what GDOS 2.4's resident hard-disk driver probes at boot. I found this by live-disassembling the driver in high RAM with scripted `zbx`. This path uses 256-byte sectors where Holte's uses 512, and it was the missing piece behind the long-standing "drives 5/6 never recognized" problem.

Both interfaces share one phase-based SASI state machine (idle → CDB → data in/out → status, with real REQ/BUSY/C-D/I-O status bits and command opcodes verified against `hd2.mac`), and both read and write the same Matthew Reed `.hdv` header format (`src/reed.h`) as the OMTI and WD1000/1010 code.

## Boot ROMs

The original OMTI ports (`0x40`–`0x43`) came from disassembling Arnulf Sopp's 1986 retrofit ROM, which was modified to speak OMTI — not from Xebec-native hardware.

- [`ROM/g3s_8501004_bootrom_2732.bin`](ROM/g3s_8501004_bootrom_2732.bin) — the genuine Xebec-speaking boot ROM.
- [`ROM/g3s_hd-omti_bootrom_2764.bin`](ROM/g3s_hd-omti_bootrom_2764.bin) — the modified EPROM that boots from hard disk regardless of controller.

## Building

```sh
mkdir -p build && cd build && cmake .. && cmake --build .
```

or

```sh
./autogen.sh && ./configure --enable-zbx --enable-readline && make
```

## License

[BSD 2-Clause](LICENSE), inherited from the upstream SDL2TRS/xtrs projects (see [xtrs.LICENSE](xtrs.LICENSE) for the xtrs-derived portions).