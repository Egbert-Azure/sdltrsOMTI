#!/usr/bin/env python3
"""
Builds a working direct-EPROM-bootable OMTI hard-disk image (.hdv).

Root cause this works around: COPYSYS.COM's own write of CPM3.SYS onto the
hard disk (via SYSTEM.SUB/GENCPM) places the file's real bytes at a location
inconsistent with the directory entry it creates for it (see omti_protocol_bugs.md
memory for the full characterization). Likely explanation, confirmed by the
original author's own comment in HDBOOTER.MAC: COPYSYS writes the hard disk's
system area as if it had 20-sector (128-byte-unit-80) floppy-style tracks --
HDBOOTER.MAC's own cold-boot read loop has to copy that same wrong assumption
(inherited verbatim from BOOTER.MAC, the floppy loader, where 20 sectors/track
is actually correct) just to read back what COPYSYS wrote. If COPYSYS uses
this same wrong per-track boundary for CPM3.SYS's raw bytes too, that would
produce exactly the kind of non-block-aligned drift this script works around.
COPYSYS.COM itself is a proprietary compiled binary (no source), so this was
never fixed at the root, only worked around here. This script instead
takes a hard disk that already has a *correctly-built boot/loader image*
(cyl0-cyl1, written correctly by COPYSYS — that part isn't buggy) and replaces
just the CPM3.SYS/CCP.COM data and their directory entries with cleanly
extracted copies of those same files from the source floppy (egcpm02a.dmk),
placed at simple, correctly block-aligned locations with a standard,
single-entry (ex=0) directory encoding.

Usage:
    1. Build BASE_HDV normally: boot egcpm34.dmk (A:) + egcpm02a.dmk (B:) via
       the standard ROM against a blank target .hdv, run B:SYSTEM.SUB. This
       gets a correct boot image but a broken CPM3.SYS/CCP.COM.
    2. Extract CPM3.SYS/CCP.COM from egcpm02a.dmk directly (not from the hard
       disk) via cpmextract.py:
           python3 cpmextract.py dmk-working/egcpm02a.dmk -o EXTRACT_DIR
    3. Run this script.
    4. Boot the output .hdv with zero floppy via the raw EPROM
       (g3s_hd-omti_bootrom_2764.bin) and confirm it reaches C>.

The output is always extended to the full 615-cylinder disk size (padded
with 0xE5) regardless of BASE_HDV's size, so D: (the second logical
partition, DPBHD2 in DISKIO1.MAC, OFF=307 cylinders -- i.e. right where C:
naturally ends) comes up as a valid empty drive ("No File" on `dir d:`)
rather than reading garbage from past EOF. D: needs no files placed on it
by this script -- it's just a blank data partition.
"""
import sys

HDR = 256
HEADS = 4
SECS = 17
SECSIZE = 512
CYL_BYTES = HEADS * SECS * SECSIZE  # 34816
OFF_CYLS = 2  # reserved system tracks; data area (directory) starts here
BLOCK_SIZE = 4096

DATA_AREA = HDR + OFF_CYLS * CYL_BYTES  # 0x11100
CPM3_FIRST_BLOCK = 16
CCP_BLOCK = 22


def dirent(name, ext, ex, s1, s2, rc, blocks):
    e = bytearray(32)
    e[0] = 0  # user 0
    e[1:9] = name.ljust(8).encode("ascii")
    e[9:12] = ext.ljust(3).encode("ascii")
    e[12], e[13], e[14], e[15] = ex, s1, s2, rc
    for i, blk in enumerate(blocks):
        e[16 + 2 * i] = blk & 0xFF
        e[16 + 2 * i + 1] = (blk >> 8) & 0xFF
    return bytes(e)


TOTAL_CYLS = 615  # full physical disk; needed for D: (DPBHD2's OFF=307) to work


def build(base_hdv, cpm3sys_path, ccpcom_path, out_path):
    with open(base_hdv, "rb") as f:
        buf = bytearray(f.read())
    with open(cpm3sys_path, "rb") as f:
        cpm3sys = f.read()
    with open(ccpcom_path, "rb") as f:
        ccpcom = f.read()

    # Extend to the full physical disk size if the base image was built
    # truncated (fine for C:-only testing, but D: -- a second logical
    # partition sharing this same physical disk, OFF=307 cylinders in --
    # needs the file to actually be that large or its reads land past EOF
    # and come back as zero bytes, which CP/M misreads as a directory full
    # of garbage rather than empty (0xE5).
    full_size = HDR + TOTAL_CYLS * CYL_BYTES
    if len(buf) < full_size:
        buf.extend(b"\xe5" * (full_size - len(buf)))

    cpm3_blocks_needed = -(-len(cpm3sys) // BLOCK_SIZE)  # ceil div
    ccp_blocks_needed = -(-len(ccpcom) // BLOCK_SIZE)
    assert CPM3_FIRST_BLOCK + cpm3_blocks_needed <= CCP_BLOCK, (
        "CPM3.SYS grew too large to fit before the fixed CCP.COM block; "
        "adjust CCP_BLOCK"
    )

    # Clear the ENTIRE directory area (blocks 0-15, per AL0/AL1=0xFFFF) back
    # to 0xE5 first, not just the target data blocks. COPYSYS's buggy write
    # leaves real-looking garbage scattered through this whole range (it's
    # where the misplaced CPM3.SYS fragment we're working around actually
    # landed) -- DIR.COM happily displays every non-E5 32-byte slot in here
    # as a garbled "file", so a base image built from a COPYSYS run must have
    # this fully wiped or `dir` shows garbage after the two real entries.
    DIR_BLOCKS = 16
    dir_len = DIR_BLOCKS * BLOCK_SIZE
    buf[DATA_AREA:DATA_AREA + dir_len] = b"\xe5" * dir_len

    cpm3_off = DATA_AREA + CPM3_FIRST_BLOCK * BLOCK_SIZE
    buf[cpm3_off:cpm3_off + len(cpm3sys)] = cpm3sys

    ccp_off = DATA_AREA + CCP_BLOCK * BLOCK_SIZE
    buf[ccp_off:ccp_off + len(ccpcom)] = ccpcom

    # RC: number of 128-byte records in the file's last logical (16K) extent.
    # A file this size needs only one directory entry (ex=0) as long as it
    # fits within EXM+1 logical extents' worth of block pointers (8 for
    # 16-bit block numbers) -- true for anything up to 32KB here.
    def rc_for(size):
        records = -(-size // 128)
        return records % 128 or 128

    cpm3_blocks = list(range(CPM3_FIRST_BLOCK, CPM3_FIRST_BLOCK + cpm3_blocks_needed))
    ccp_blocks = list(range(CCP_BLOCK, CCP_BLOCK + ccp_blocks_needed))

    cpm3_entry = dirent("CPM3", "SYS", 0, 0, 0, rc_for(len(cpm3sys)), cpm3_blocks)
    ccp_entry = dirent("CCP", "COM", 0, 0, 0, rc_for(len(ccpcom)), ccp_blocks)

    buf[DATA_AREA:DATA_AREA + 32] = cpm3_entry
    buf[DATA_AREA + 32:DATA_AREA + 64] = ccp_entry

    with open(out_path, "wb") as f:
        f.write(buf)
    print(f"wrote {out_path}")
    print(f"  CPM3.SYS: {len(cpm3sys)} bytes, blocks {cpm3_blocks}, rc={rc_for(len(cpm3sys))}")
    print(f"  CCP.COM:  {len(ccpcom)} bytes, blocks {ccp_blocks}, rc={rc_for(len(ccpcom))}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print(f"usage: {sys.argv[0]} BASE_HDV CPM3_SYS CCP_COM OUT_HDV", file=sys.stderr)
        sys.exit(1)
    build(*sys.argv[1:5])
