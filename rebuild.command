#!/bin/bash
# Double-click this in Finder (or run from a terminal) to (re)compile this
# repo's own sdl2trs into build/sdl2trs. This build is fully isolated to
# this repository -- nothing is installed system-wide, and the other
# sdltrs checkouts (sdltrsOMTI etc.) are untouched.
#
# After a successful build, test with:
#   boot_gdos24_xebec.command        -- boot GDOS 2.4 with the Xebec disk
#   boot_gdos24_xebec_debug.command  -- same, with the zbx debugger enabled

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"

mkdir -p "$REPO/build"

echo "Configuring (cmake) ..."
cmake -S "$REPO" -B "$REPO/build"

echo
echo "Building ..."
cmake --build "$REPO/build"

echo
echo "Done: $REPO/build/sdl2trs"
ls -l "$REPO/build/sdl2trs"
echo
echo "Run boot_gdos24_xebec.command to boot GDOS 2.4 against it."
read -n 1 -s -r -p "Press any key to close..."
echo
