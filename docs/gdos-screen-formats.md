<!-- /gdos-screen-formats.md -->

# G-DOS 2.4 — screen formats

## Original (German)

**Umschalten der Bildschirm-Formate**

Zum Umschalten der Formate gibt es mehrere Möglichkeiten:

- Die Umschaltcodes 10H–13H (16–19) für die Formate 64×16, 64×24, 64×32 und 80×25.
- Die G-DOS-Befehle 64 (für 64×16) und 80 (für 80×25).
- Den G-DOS-Befehl ##.

**Der G-DOS-Befehl ##,Parameter**

Der Befehl ## kennt 4 mögliche Parameter:

- `##,H` „Halb" schaltet auf 64×16 (wie der Befehl 64)
- `##,S` „Spezial" setzt 64×24 mit 4 geschützten Zeilen am oberen und unteren Bildrand
- `##,V` „Voll" schaltet auf 64×32
- `##,X` „eXtended" schaltet auf 80×25 (wie der Befehl 80)

## English translation

**Switching screen formats**

There are several ways to switch between screen formats:

- Switch codes 10H–13H (16–19) select the 64×16, 64×24, 64×32, and 80×25 formats.
- The G-DOS commands `64` (for 64×16) and `80` (for 80×25).
- The G-DOS command `##`.

**The `##,parameter` command**

The `##` command takes four parameters:

- `##,H` ("Halb", half) switches to 64×16, the same as command `64`.
- `##,S` ("Spezial", special) sets 64×24 with four protected lines at the top and bottom of the screen.
- `##,V` ("Voll", full) switches to 64×32.
- `##,X` ("eXtended") switches to 80×25, the same as command `80`.

## Recommended working mode

For everyday use, and especially under emulation, **80×25 gives the clearest and most workable display**. Reach it with the command `80` or with `##,X`. An 80-column text screen on a Z80 machine of this class was uncommon for its time, and it is one of the things that still makes G-DOS 2.4 comfortable to work in.
