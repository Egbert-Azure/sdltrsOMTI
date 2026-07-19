# sdltrsOMTI

Fork of [SDL2TRS](https://gitlab.com/jengun/sdltrs) (TRS-80 Model I/III/4/4P
emulator by Jens Guenther) adding emulation of the OMTI 5527-class SASI/MFM
hard disk controller used by the TCS Genie IIIs (Thomas Holte's CP/M 3.0
BIOS driver), alongside the existing WD1000/1010 controller.

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
