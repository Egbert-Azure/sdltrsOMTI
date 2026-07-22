/*
 * Copyright (c) 2026, Egbert Schroeer
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Emulation of the Xebec S1410/S1410A SASI/MFM hard disk controller,
 * believed to be the controller genuinely used by the TCS Genie IIIs'
 * built-in hard disk (GDOS 2.4, Klaus Kaempf's CP/M port).
 *
 * Command set (class/opcode values) is taken from the real Xebec S1410A
 * Owner's Manual. The port map and status register bit layout are taken
 * directly from the "COND SASI ;Xebec part" conditional-assembly block of
 * Thomas Holte's CP/M 3.0 BIOS driver (hd2.mac) -- the same source
 * trs_omti.h's ports were reverse-engineered from. That source confirms
 * Xebec shares the exact same base port as OMTI (0x40): on real hardware
 * these are mutually exclusive alternatives on the same host-adapter slot
 * (only one controller chip is ever physically installed), never present
 * simultaneously -- see trs_io.c's GENIE3S dispatch, which routes
 * 0x40-0x42 to whichever of trs_omti/trs_xebec actually has an image
 * attached.
 *
 * DCB addressing (flat logical block number rather than OMTI's raw
 * cylinder/head/sector) and the two-byte completion status are unique to
 * this protocol and come from the manual, not from hd2.mac.
 */

#ifndef _TRS_XEBEC_H
#define _TRS_XEBEC_H

extern void  trs_xebec_init(int poweron);
extern void  trs_xebec_attach(int drive, const char *diskname);
extern void  trs_xebec_remove(int drive);
extern int   trs_xebec_in(int port);
extern void  trs_xebec_out(int port, int value);
extern int   trs_xebec_tcs_in(int port);
extern void  trs_xebec_tcs_out(int port, int value);
extern const char *trs_xebec_getfilename(int unit);
extern int   trs_xebec_getwriteprotect(int unit);
extern void  trs_xebec_getgeometry(int unit, int *cyls, int *head, int *secs);

#define TRS_XEBEC_MAXDRIVES 1 /* limited to 1 unit in the sdltrs UI/config; the
                                * real protocol's 1-bit LUN addresses 2 (hd2.mac's
                                * MAXDRIVE), guarded against in trs_xebec.c */

/*
 * Port map, relative to base 0x40 -- shared with OMTI (trs_omti.h), per
 * hd2.mac's WPORT0-2/RPORT0-1 equates. Only 3 ports are used (no 4th
 * "mask" register like OMTI's TRS_OMTI_MASK).
 */
#define TRS_XEBEC_PORT   0x40 /* WPORT0/RPORT0: data (read/write) */
#define TRS_XEBEC_STATUS 0x41 /* WPORT1: software reset (write) / RPORT1: status (read) */
#define TRS_XEBEC_SELECT 0x42 /* WPORT2: controller select (write only, no read) */

/*
 * Second host-adapter interface: the TCS Genie IIIs' own onboard SASI
 * adapter at ports 0x00-0x02, as used by GDOS 2.4's resident hard-disk
 * driver (reverse-engineered from a live disassembly of the driver at
 * F000h-F4FFh on a booted G3S-GDOS24.DMK; selection routine at F1B6h).
 * It is a much rawer adapter than Holte's 0x40-0x42 one: the same
 * REQ/BUSY/CD/IO status bits (identical bit positions -- both boards
 * expose the raw SASI control signals), but selection is done the real
 * SASI way: the host writes the controller ID (01h) to the data port,
 * reads it back to verify the bus, then pulses SEL via port 2 and waits
 * for the controller to assert BUSY. A write to port 1 releases the bus.
 * Data-phase transfers are auto-handshaked 256-byte INIR/OTIR bursts
 * (GDOS runs the S1410 with 256-byte sectors; Holte's CP/M used 512).
 */
#define TRS_XEBEC_TCS_DATA 0x00 /* SASI data bus (read/write) */
#define TRS_XEBEC_TCS_CTRL 0x01 /* read: status bits / write: deselect (bus release) */
#define TRS_XEBEC_TCS_SEL  0x02 /* SEL strobe (write only) */

#define TRS_XEBEC_TCS_SECSIZE 256

/*
 * Status register bits polled at TRS_XEBEC_STATUS, per hd2.mac's
 * REQMASK/BUSYMASK/CDMASK/IOMASK equates (which also match the Xebec
 * manual's own sample Z80 driver code, CDBIT/CDMASK/IOBIT/IOMASK).
 * Standard SASI phase encoding: C/D and I/O together select the phase.
 */
#define TRS_XEBEC_ST_REQ  0x01 /* request: a byte is ready to transfer */
#define TRS_XEBEC_ST_BUSY 0x02 /* controller busy (selected, mid-command) */
#define TRS_XEBEC_ST_CD   0x08 /* command/[data]: set during command and status phases */
#define TRS_XEBEC_ST_IO   0x10 /* [host->controller]/controller->host: set when controller is driving (data-in, status) */

/*
 * Device Control Block (DCB): 6 bytes, per Xebec S1410A manual section
 * "COMMANDS". Byte 0 top 3 bits are the command class; classes 1-6 are
 * reserved, so for the class 0 commands this emulator implements, byte 0
 * equals the opcode directly. Opcode values confirmed against hd2.mac's
 * own $TSTDRV/$REST/$STATUS/$$READ/$$WRITE/$INIDRV equates.
 */
#define TRS_XEBEC_DCBLEN 6

#define TRS_XEBEC_TEST_DRIVE_READY 0x00
#define TRS_XEBEC_RECALIBRATE      0x01
#define TRS_XEBEC_REQUEST_SENSE    0x03
#define TRS_XEBEC_FORMAT_DRIVE     0x04
#define TRS_XEBEC_FORMAT_TRACK     0x06
#define TRS_XEBEC_READ             0x08
#define TRS_XEBEC_WRITE            0x0a
#define TRS_XEBEC_SEEK             0x0b
#define TRS_XEBEC_INIT_DRIVE_CHAR  0x0c
#define TRS_XEBEC_READ_BUFFER      0x0e
#define TRS_XEBEC_WRITE_BUFFER     0x0f

/* DCB byte 1: LUN bit and high 5 bits of the 21-bit logical block address */
#define TRS_XEBEC_DCB1_ADDRMASK 0x1f
#define TRS_XEBEC_DCB1_LUNMASK  0x20
#define TRS_XEBEC_DCB1_LUNSHIFT 5

/*
 * Next-to-last completion status byte (first of the two bytes read back
 * from TRS_XEBEC_PORT once the status phase is reached). The second
 * (last) status byte is always zero -- it just signals "done" to the
 * host. Error bit confirmed against hd2.mac's own ERROR equate (02H).
 */
#define TRS_XEBEC_ST_ERROR 0x02

/*
 * Request Sense Status (opcode 0x03) reply: 4 bytes.
 * Byte 0: bits 0-3 error code, bits 4-5 error type, bit 7 address valid.
 * Bytes 1-3: logical block address (MSB first) associated with the error.
 */
#define TRS_XEBEC_SENSELEN 4

/*
 * Initialize Drive Characteristics (opcode 0x0C) parameter block: 8
 * bytes sent by the host after the DCB. Only cylinder count and head
 * count affect this emulator's addressing (mirroring trs_omti.c's
 * SET_CHARACTERISTICS: real geometry is keyed to the attached image's
 * own Reed header, not to whatever a boot ROM/driver declares here).
 */
#define TRS_XEBEC_CHARLEN 8
#define TRS_XEBEC_CHAR_CYLHI 0
#define TRS_XEBEC_CHAR_CYLLO 1
#define TRS_XEBEC_CHAR_HEADS 2

#endif /* _TRS_XEBEC_H */
