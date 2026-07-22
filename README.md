# sdltrsOMTI

`sdltrsXebec` is a fork of [`sdltrsOMTI`](https://github.com/Egbert-Azure/sdltrsOMTI) (itself a fork of [SDL2TRS](https://gitlab.com/jengun/sdltrs)/xtrs), cloned as a starting point with full history preserved (`git remote -v` shows `sdltrsomti-upstream`, intentionally renamed from `origin` so nothing here accidentally pushes back to that project). a
Goald is adding emulation of the Xebec SASI/MFM S1510A 
5.25 INCH WINCHESTER DISK CONTROLLER 
hard disk controller used by the GdDos 2.4, alongside the existing WD1000/1010 controller.

The existing OMTI ports (0x40-0x43) come from disassembling Arnulf Sopp's 1986 retrofit ROM that was modified to speak OMTI — not from genuine Xebec-native hardware. The sibling repo's omti_boot_crash_investigation.md explicitly says the stock Genie IIIs ROM never touches hard-disk ports at all, and the "real" Xebec-speaking boot ROM is [text](ROM/g3s_8501004_bootrom_2732.bin).
The modificated EPROM to boot from HD (regardless which controller) is [text](ROM/g3s_hd-omti_bootrom_2764.bin)

## Building

```sh
mkdir -p build && cd build && cmake .. && cmake --build .
```
or
```sh
./autogen.sh && ./configure --enable-zbx --enable-readline && make
```

## License

[BSD 2-Clause](LICENSE), inherited from the upstream SDL2TRS/xtrs projects
(see [xtrs.LICENSE](xtrs.LICENSE) for the xtrs-derived portions).
