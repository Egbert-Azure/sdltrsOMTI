#!/bin/bash
# Same as boot_gdos24_xebec.command, but with the zbx debugger enabled so
# you can freeze execution at an exact moment and inspect memory/registers.
#
# HOW TO USE:
#   1. Double-click this. It starts in FULLSCREEN automatically (needed for
#      the debugger hotkey to work at all -- see below).
#   2. Boot to the GDOS prompt as usual, and type PD 5 like before -- let
#      it finish and show "Bauteil nicht erreichbar" as normal.
#   3. THEN press F9. This does NOT freeze the graphics window by itself --
#      it drops the TERMINAL WINDOW (the one showing this script's text
#      output, not the emulator graphics) into a "(zbx)" debugger prompt.
#      Switch to that Terminal window to see it. The graphics window will
#      look frozen/unresponsive while you're in the debugger -- that's
#      expected, not a hang.
#   4. In the Terminal, type exactly these three lines (one at a time):
#        dis 4408,4410
#        dis 5992,599a
#        p
#   5. Paste me everything the Terminal shows from those three commands.
#      That's the two memory locations gating the drive-table write, plus
#      full register state, as they stand right after PD 5 finished.
#
# Every disk/hard/omti/xebec slot is passed explicitly (empty string to
# clear) so this never boots whatever was last left in ~/.sdltrs.t8c.

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"

ROM_PATH="$REPO/ROM/g3s_8501004_bootrom_2732.bin"
DISK0_PATH="$REPO/dmk-working/G3S-GDOS24.DMK"
XEBEC_HDV="$REPO/HDV/g3s-gdos24-omti-10mb.hdv"
LOG_DIR="$REPO/logs"
LOG_FILE="$LOG_DIR/boot_gdos24_xebec_debug_$(date +%Y%m%d_%H%M%S).log"

if [ ! -x "$REPO/build/sdl2trs" ]; then
  echo "sdl2trs not found or not executable at: $REPO/build/sdl2trs"
  echo "Build it first: mkdir -p build && cd build && cmake .. && cmake --build ."
  read -n 1 -s -r -p "Press any key to close..."
  exit 1
fi

for f in "$ROM_PATH" "$DISK0_PATH" "$XEBEC_HDV"; do
  if [ ! -f "$f" ]; then
    echo "Missing required file: $f"
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
  fi
done

mkdir -p "$LOG_DIR"
echo "Logging to: $LOG_FILE"
echo
echo "=============================================================="
echo "  Boot to the GDOS prompt, THEN press F9 and switch to THIS"
echo "  Terminal window (not the graphics window) for the (zbx)"
echo "  prompt. See the comment block at the top of this script for"
echo "  the exact commands to type."
echo "=============================================================="
echo

"$REPO/build/sdl2trs" -model 1 \
  -rom "$ROM_PATH" \
  -disk0 "$DISK0_PATH" \
  -disk1 "" -disk2 "" -disk3 "" -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 "" -hard1 "" \
  -omti0 "" \
  -x0 "$XEBEC_HDV" \
  -diskdebug 0x3 -io 0x3f \
  -zbx -fs 2>&1 | tee "$LOG_FILE"

echo
echo "sdl2trs exited. Log saved at: $LOG_FILE"
read -n 1 -s -r -p "Press any key to close..."
