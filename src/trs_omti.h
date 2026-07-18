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
 * Emulation of the OMTI 5010-class SASI/MFM hard disk controller used
 * in the TCS Genie IIIs, mapped at ports 0x40-0x43.  This is a separate,
 * independent interface from the Western Digital WD1000/WD1010 emulated
 * in trs_hard.c (which the Genie IIIs also has, relocated to 0xC8-0xCF).
 *
 * Protocol reverse-engineered from Thomas Holte's CP/M 3.0 BIOS driver
 * (hd2.mac, by Peter Petersen / H. Bernhardt / V. Dose/E. Schroeer).
 */

#ifndef _TRS_OMTI_H
#define _TRS_OMTI_H

extern void  trs_omti_init(int poweron);
extern void  trs_omti_attach(int drive, const char *diskname);
extern void  trs_omti_remove(int drive);
extern int   trs_omti_in(int port);
extern void  trs_omti_out(int port, int value);
extern const char *trs_omti_getfilename(int unit);
extern int   trs_omti_getwriteprotect(int unit);
extern void  trs_omti_getgeometry(int unit, int *cyls, int *head, int *secs);

#define TRS_OMTI_MAXDRIVES 2 /* one SASI LUN bit -> 2 addressable units */

/*
 * Port map, relative to base 0x40
 */
#define TRS_OMTI_PORT    0x40 /* data/command port (read/write) */
#define TRS_OMTI_STATUS  0x41 /* status (read) / software reset (write) */
#define TRS_OMTI_SELECT  0x42 /* select strobe (write) / card-present ID, reads 0xFA (read) */
#define TRS_OMTI_MASK    0x43 /* DMA/interrupt mask (write, not emulated) */

/*
 * Status byte values polled at TRS_OMTI_STATUS
 */
#define TRS_OMTI_IDLE       0xc0 /* idle, ready to accept SELECT */
#define TRS_OMTI_DATA_OUT   0xc9 /* data phase: byte wanted at TRS_OMTI_PORT */
#define TRS_OMTI_DATA_IN    0xcb /* data phase: byte available at TRS_OMTI_PORT */
#define TRS_OMTI_STATUS_RDY 0xcf /* command complete, final status ready */
#define TRS_OMTI_REQ        0x01 /* bit 0: request for next CDB/data byte */

/*
 * SASI/OMTI 6-byte command descriptor block (CDB) commands
 */
#define TRS_OMTI_CDBLEN 6

#define TRS_OMTI_TEST_UNIT_READY 0x00
#define TRS_OMTI_REZERO          0x01
#define TRS_OMTI_REQUEST_SENSE   0x03
#define TRS_OMTI_FORMAT          0x04
#define TRS_OMTI_READ            0x08
#define TRS_OMTI_WRITE           0x0a
#define TRS_OMTI_SEEK            0x0b
#define TRS_OMTI_SET_CHARACTERISTICS 0x0c

/* Final status byte (read from TRS_OMTI_PORT once status is 0xCF) */
#define TRS_OMTI_ST_ERROR 0x02

/* CDB byte 1: LUN bit and cylinder bit 10 */
#define TRS_OMTI_CDB1_HEADMASK 0x1f
#define TRS_OMTI_CDB1_LUNMASK  0x20
#define TRS_OMTI_CDB1_LUNSHIFT 5
#define TRS_OMTI_CDB1_CYL10    0x80

/* CDB byte 2: sector number and cylinder bits 9-8 */
#define TRS_OMTI_CDB2_SECMASK  0x3f
#define TRS_OMTI_CDB2_CYLMASK  0xc0
#define TRS_OMTI_CDB2_CYLSHIFT 6

/* SET DRIVE CHARACTERISTICS parameter block length and layout: byte 0-1
 * are the cylinder count (MSB first), byte 2 is the head count. The
 * remaining bytes (reduced write current cylinder, write precompensation
 * cylinder, step rate/ECC) don't affect sector addressing and are unused. */
#define TRS_OMTI_CHARLEN 8
#define TRS_OMTI_CHAR_CYLHI 0
#define TRS_OMTI_CHAR_CYLLO 1
#define TRS_OMTI_CHAR_HEADS 2

#endif /* _TRS_OMTI_H */
