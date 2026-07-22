#!/bin/bash
# Double-click this in Finder (or run from a terminal) to boot GDOS 2.4 with
# a hard disk attached via the new Xebec S1410 controller emulation
# (trs_xebec.c). Boots with dmk-working/G3S-GDOS24.DMK on disk0 by default --
# once running, use Alt-D (or Alt-F) to open the Floppy Disk Management menu
# and swap in a different GDOS build from dmk-working/ to test, without
# needing to relaunch. Alt-H opens Hard Disk Management if you need to
# swap the xebec0 image too.
#
# Every run logs full port-level I/O tracing (every single Z80 IN/OUT, any
# port, via trs_io.c's generic IODEBUG_IN/IODEBUG_OUT bits, plus the
# OMTI/Xebec-specific debug bits) to logs/, so whatever you see on screen
# can be cross-checked afterward: does PD 5, HDFORMAT, or anything else
# ever touch ports 0x40-0x42 (shared OMTI/Xebec range) or anywhere else
# unexpected. Just tell me what you tried and what happened on screen --
# I can read the log myself, no need to relay port numbers back manually.
#
# Every disk/hard/omti/xebec slot is passed explicitly (empty string to
# clear) so this never boots whatever was last left in ~/.sdltrs.t8c.
# Runs this repo's own build in place.

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"

ROM_PATH="$REPO/ROM/g3s_8501004_bootrom_2732.bin"
DISK0_PATH="$REPO/dmk-working/G3S-GDOS24.DMK"
XEBEC_HDV="$REPO/HDV/g3s-gdos24-omti-10mb.hdv"
LOG_DIR="$REPO/logs"
LOG_FILE="$LOG_DIR/boot_gdos24_xebec_$(date +%Y%m%d_%H%M%S).log"

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
echo "ROM (standard):  $ROM_PATH"
echo "disk0 (default): $DISK0_PATH"
echo "xebec0:          $XEBEC_HDV"
echo
echo "Alt-D (or Alt-F): Floppy Disk Management -- swap disk0 to test a"
echo "                  different GDOS build from dmk-working/"
echo "Alt-H:            Hard Disk Management -- swap the xebec0 image"
echo
echo "At the GDOS prompt, try: PD 5   (or run HDFORMAT, confirm with JA)"
echo

"$REPO/build/sdl2trs" -model 1 \
  -rom "$ROM_PATH" \
  -disk0 "$DISK0_PATH" \
  -disk1 "" -disk2 "" -disk3 "" -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 "" -hard1 "" \
  -omti0 "" \
  -x0 "$XEBEC_HDV" \
  -diskdebug 0x3 -io 0x3f 2>&1 | tee "$LOG_FILE"

echo
echo "sdl2trs exited. Log saved at: $LOG_FILE"
read -n 1 -s -r -p "Press any key to close..."
