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
 * Emulation of the Xebec S1410/S1410A SASI/MFM hard disk controller. See
 * trs_xebec.h for the port map / status bit sourcing (confirmed against
 * Thomas Holte's hd2.mac).
 *
 * Command set taken from the Xebec S1410A Owner's Manual: a SELECT
 * strobe, a 6-byte Device Control Block (DCB) sent one byte at a time
 * gated by the REQ status bit, a data phase (sector data for READ/WRITE,
 * 4 bytes for REQUEST SENSE STATUS, or an 8-byte parameter block for
 * INITIALIZE DRIVE CHARACTERISTICS), and a two-byte completion status
 * read back from the data port. Unlike OMTI, drives are addressed by a
 * single flat logical block number (DCB bytes 1-3) rather than raw
 * cylinder/head/sector; this emulator converts that to a file offset
 * using the attached image's own geometry, same as OMTI/WD1000.
 *
 * Disk images use the same 256-byte Reed header (reed.h) as trs_hard.c
 * and trs_omti.c. As with OMTI, the manual gives no live sector-size
 * register, so a fixed 512 bytes/sector (typical of the ST-506/MFM
 * drives the S1410A was paired with) is used unconditionally.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs.h"
#include "trs_hard_image.h"
#include "trs_xebec.h"
#include "trs_state_save.h"

#define XEBECDEBUG1 (1 << 4)  /* show detail on all port i/o */
#define XEBECDEBUG2 (1 << 5)  /* show all commands */

#define XEBEC_SEC_PER_TRK 32     /* fallback if the header omits head count */
#define XEBEC_MAXHEADS 8
#define XEBEC_DEFAULT_SECSIZE 512
#define XEBEC_SECBUFSIZE 1024

/*
 * Composite status-register values for each phase, built from the
 * REQ/BUSY/CD/IO bits in trs_xebec.h. Standard SASI phase encoding:
 * C/D and I/O together select the phase (host writes command bytes when
 * CD is set and IO is clear; data flows host->controller when both are
 * clear, controller->host when IO is set; status is CD+IO together).
 */
#define XEBEC_STATUS_IDLE     0x00
#define XEBEC_STATUS_DCB      (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ | TRS_XEBEC_ST_CD)
#define XEBEC_STATUS_DATA_OUT (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ)
#define XEBEC_STATUS_DATA_IN  (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ | TRS_XEBEC_ST_IO)
#define XEBEC_STATUS_STATUS   (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ | TRS_XEBEC_ST_CD | TRS_XEBEC_ST_IO)

typedef enum {
  XEBEC_PH_IDLE,
  XEBEC_PH_DCB,
  XEBEC_PH_DATA_IN,
  XEBEC_PH_DATA_OUT,
  XEBEC_PH_STATUS
} XebecPhase;

/* One drive (LUN); geometry decoding is shared (trs_hard_image.h) */

/* Structure describing controller state */
typedef struct {
  int present;
  XebecPhase phase;
  Uint8 status;

  Uint8 dcb[TRS_XEBEC_DCBLEN];
  int dcb_index;
  Uint8 command;
  int lun;

  Uint8 buf[XEBEC_SECBUFSIZE];
  Uint8 fillbuf[XEBEC_SECBUFSIZE];
  int bytesdone;
  int datalen;
  int secsize;
  int blocks;           /* sectors left in the current READ/WRITE (DCB byte 4) */
  Uint8 busdata;        /* last byte written to the bus while idle (selection ID echo) */
  Uint8 final_status;   /* next-to-last status byte */
  int status_index;     /* 0 = next-to-last byte pending, 1 = last (zero) byte pending */

  HardImage d[TRS_XEBEC_MAXDRIVES];
} State;

static State state;

static int  xebec_open(int drive);
static int  xebec_seek(int lun, long lba);
static void xebec_command(void);
static void xebec_finish(int ok);
static int  xebec_data_in(void);
static void xebec_data_out(int value);

#ifdef ZBX
void trs_xebec_debug(void)
{
  int i;

  printf("Xebec hard disk controller state:");
  if (state.present == 0) {
    puts(" DISABLED");
    return;
  }

  printf("\n  phase:%d, lun:%d, command:0x%02X, status:0x%02X, secsize:%d\n",
      state.phase, state.lun, state.command, state.status, state.secsize);

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++) {
    if (state.d[i].file) {
      printf("\nxebec%d: '%s'\n", i, state.d[i].filename);
      printf("\theads %d, cyls %4d, secs %4d, writeprot %d\n",
          state.d[i].heads, state.d[i].cyls, state.d[i].secs,
          state.d[i].writeprot);
    }
  }
}
#endif

/* Powerup or reset button */
void trs_xebec_init(int poweron)
{
  state.phase = XEBEC_PH_IDLE;
  state.status = XEBEC_STATUS_IDLE;
  state.dcb_index = 0;
  state.command = 0;
  state.lun = 0;
  state.bytesdone = 0;
  state.datalen = 0;
  state.blocks = 0;
  state.busdata = 0;
  state.final_status = 0;
  state.status_index = 0;
  memset(state.buf, 0, sizeof(state.buf));

  if (poweron) {
    int i;

    state.present = 0;
    state.secsize = XEBEC_DEFAULT_SECSIZE;
    memset(state.fillbuf, 0xe5, sizeof(state.fillbuf));

    for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++) {
      state.d[i].writeprot = 0;
      state.d[i].cyls = 0;
      state.d[i].heads = 0;
      state.d[i].secs = 0;

      if (xebec_open(i) == 0) state.present = 1;
    }
  }
}

void trs_xebec_attach(int drive, const char *diskname)
{
  snprintf(state.d[drive].filename, FILENAME_MAX, "%s", diskname);

  if (xebec_open(drive) != 0)
    trs_xebec_remove(drive);
}

void trs_xebec_remove(int drive)
{
  if (state.d[drive].file != NULL)
    fclose(state.d[drive].file);

  state.d[drive].filename[0] = 0;
  state.d[drive].file = NULL;
  state.d[drive].writeprot = 0;
  state.d[drive].cyls = 0;
  state.d[drive].heads = 0;
  state.d[drive].secs = 0;
}

const char*
trs_xebec_getfilename(int unit)
{
  return state.d[unit].filename;
}

int
trs_xebec_getwriteprotect(int unit)
{
  return state.d[unit].writeprot;
}

void
trs_xebec_getgeometry(int unit, int *cyls, int *head, int *secs)
{
  if (state.d[unit].file) {
    *cyls = state.d[unit].cyls;
    *head = state.d[unit].heads;
    *secs = state.d[unit].secs;
  }
}

/* Read from an I/O port mapped to the controller */
int trs_xebec_in(int port)
{
  int v = 0xff;

  if (state.present) {
    switch (port) {
    case TRS_XEBEC_PORT:
      v = xebec_data_in();
      break;
    case TRS_XEBEC_STATUS:
      v = state.status;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG1)
    debug("[PC=%04X] trs_xebec_in(%02X) => %02X\n", Z80_PC, port, v);
#endif
  return v;
}

/* Write to an I/O port mapped to the controller */
void trs_xebec_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG1)
    debug("[PC=%04X] trs_xebec_out(%02X), %02X\n", Z80_PC, port, value);
#endif
  switch (port) {
  case TRS_XEBEC_PORT:
    xebec_data_out(value);
    break;
  case TRS_XEBEC_STATUS:
    /* Any write is a software reset: abort to idle */
    trs_xebec_init(0);
    break;
  case TRS_XEBEC_SELECT:
    if (state.phase == XEBEC_PH_IDLE) {
      state.secsize = XEBEC_DEFAULT_SECSIZE;
      state.phase = XEBEC_PH_DCB;
      state.dcb_index = 0;
      state.status = XEBEC_STATUS_DCB;
    }
    break;
  }
}

/*
 * TCS Genie IIIs onboard host adapter at ports 0x00-0x02 (see
 * trs_xebec.h): same controller, rawer bus interface. GDOS 2.4's driver
 * selects by writing the controller ID to the data port, verifying the
 * bus by reading it back, then strobing SEL and polling for BUSY.
 */
int trs_xebec_tcs_in(int port)
{
  int v = 0xff;

  if (state.present) {
    switch (port) {
    case TRS_XEBEC_TCS_DATA:
      v = state.phase == XEBEC_PH_IDLE ? state.busdata : xebec_data_in();
      break;
    case TRS_XEBEC_TCS_CTRL:
      v = state.status;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG1)
    debug("[PC=%04X] trs_xebec_tcs_in(%02X) => %02X\n", Z80_PC, port, v);
#endif
  return v;
}

void trs_xebec_tcs_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG1)
    debug("[PC=%04X] trs_xebec_tcs_out(%02X), %02X\n", Z80_PC, port, value);
#endif
  switch (port) {
  case TRS_XEBEC_TCS_DATA:
    if (state.phase == XEBEC_PH_IDLE)
      state.busdata = (Uint8)value;
    else
      xebec_data_out(value);
    break;
  case TRS_XEBEC_TCS_CTRL:
    /* Bus release/deselect */
    trs_xebec_init(0);
    break;
  case TRS_XEBEC_TCS_SEL:
    /* Respond to selection only when addressed as controller 0 (data
     * bus bit 0), the only ID GDOS's driver ever selects. */
    if (state.phase == XEBEC_PH_IDLE && (state.busdata & 0x01)) {
      state.secsize = TRS_XEBEC_TCS_SECSIZE;
      state.phase = XEBEC_PH_DCB;
      state.dcb_index = 0;
      state.status = XEBEC_STATUS_DCB;
    }
    break;
  }
}

static void xebec_finish(int ok)
{
  state.phase = XEBEC_PH_STATUS;
  state.status = XEBEC_STATUS_STATUS;
  state.final_status = ok == 0 ? 0 : TRS_XEBEC_ST_ERROR;
  state.status_index = 0;
}

static void xebec_command(void)
{
  long lba;

  state.command = state.dcb[0];
  state.lun     = (state.dcb[1] & TRS_XEBEC_DCB1_LUNMASK) >> TRS_XEBEC_DCB1_LUNSHIFT;
  lba           = ((long)(state.dcb[1] & TRS_XEBEC_DCB1_ADDRMASK) << 16)
                | ((long)state.dcb[2] << 8)
                | state.dcb[3];

  state.bytesdone = 0;

#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG2)
    debug("trs_xebec: command 0x%02X lun:%d lba:%ld\n",
        state.command, state.lun, lba);
#endif

  /* LUN is a 1-bit field in the DCB, so guest software can address a
   * second unit even though only TRS_XEBEC_MAXDRIVES is emulated: treat
   * it as not-present rather than indexing state.d[] out of bounds. */
  if (state.lun >= TRS_XEBEC_MAXDRIVES) {
    xebec_finish(-1);
    return;
  }

  /* DCB byte 4 is the block count for READ/WRITE (0 means 256) */
  state.blocks = state.dcb[4] ? state.dcb[4] : 256;

  switch (state.command) {
  case TRS_XEBEC_READ:
    if (xebec_seek(state.lun, lba) == 0) {
      FILE *f = state.d[state.lun].file;

      if (f && fread(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
        if (ferror(f)) {
          file_error("reading xebec%d", state.lun);
          xebec_finish(-1);
          break;
        }
      }
      state.datalen = state.secsize;
      state.phase = XEBEC_PH_DATA_IN;
      state.status = XEBEC_STATUS_DATA_IN;
    } else {
      xebec_finish(-1);
    }
    break;

  case TRS_XEBEC_WRITE:
    if (xebec_seek(state.lun, lba) == 0) {
      state.datalen = state.secsize;
      state.phase = XEBEC_PH_DATA_OUT;
      state.status = XEBEC_STATUS_DATA_OUT;
    } else {
      xebec_finish(-1);
    }
    break;

  case TRS_XEBEC_FORMAT_TRACK:
  case TRS_XEBEC_FORMAT_DRIVE:
    if (xebec_seek(state.lun, lba) == 0) {
      FILE *f = state.d[state.lun].file;

      if (f && fwrite(state.fillbuf, 1, state.secsize, f) != (size_t)state.secsize) {
        if (errno) {
          file_error("formatting xebec%d", state.lun);
          xebec_finish(-1);
          break;
        }
      }
      xebec_finish(0);
    } else {
      xebec_finish(-1);
    }
    break;

  case TRS_XEBEC_INIT_DRIVE_CHAR:
    state.datalen = TRS_XEBEC_CHARLEN;
    state.phase = XEBEC_PH_DATA_OUT;
    state.status = XEBEC_STATUS_DATA_OUT;
    break;

  case TRS_XEBEC_SEEK:
    xebec_finish(xebec_seek(state.lun, lba));
    break;

  case TRS_XEBEC_RECALIBRATE:
    xebec_finish(xebec_seek(state.lun, 0));
    break;

  case TRS_XEBEC_TEST_DRIVE_READY:
    xebec_finish(state.d[state.lun].file != NULL ||
                xebec_open(state.lun) == 0 ? 0 : -1);
    break;

  case TRS_XEBEC_REQUEST_SENSE:
    /* No detailed error tracking is modeled: always report "no error",
     * matching the all-zero encoding the manual defines for that case. */
    memset(state.buf, 0, TRS_XEBEC_SENSELEN);
    state.datalen = TRS_XEBEC_SENSELEN;
    state.phase = XEBEC_PH_DATA_IN;
    state.status = XEBEC_STATUS_DATA_IN;
    break;

  case TRS_XEBEC_READ_BUFFER:
    /* Return the sector buffer as-is, no disk access */
    state.datalen = state.secsize;
    state.phase = XEBEC_PH_DATA_IN;
    state.status = XEBEC_STATUS_DATA_IN;
    break;

  case TRS_XEBEC_WRITE_BUFFER:
    /* Fill the sector buffer, no disk access */
    state.datalen = state.secsize;
    state.phase = XEBEC_PH_DATA_OUT;
    state.status = XEBEC_STATUS_DATA_OUT;
    break;

  default:
    error("trs_xebec: unknown command 0x%02X", state.command);
    xebec_finish(-1);
    break;
  }
}

static void xebec_data_out(int value)
{
  switch (state.phase) {
  case XEBEC_PH_DCB:
    state.dcb[state.dcb_index++] = (Uint8)value;
    if (state.dcb_index == TRS_XEBEC_DCBLEN)
      xebec_command();
    break;

  case XEBEC_PH_DATA_OUT:
    if (state.bytesdone < state.datalen) {
      state.buf[state.bytesdone++] = (Uint8)value;
      if (state.bytesdone == state.datalen) {
        if (state.command == TRS_XEBEC_WRITE) {
          FILE *f = state.d[state.lun].file;

          if (f && fwrite(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
            if (errno) {
              file_error("writing xebec%d", state.lun);
              xebec_finish(-1);
              break;
            }
          }
          if (--state.blocks > 0) {
            /* More sectors in this command: keep the data phase open,
             * file position is already at the next sector */
            state.bytesdone = 0;
            break;
          }
        }
        /* INITIALIZE DRIVE CHARACTERISTICS is acknowledged but does not
         * change addressing geometry: as with trs_omti.c's SET DRIVE
         * CHARACTERISTICS, real geometry stays keyed to the attached
         * image's own Reed header (the actual physical disk), not to
         * whatever a boot ROM/driver declares here. */
        xebec_finish(0);
      }
    }
    break;

  default:
    break;
  }
}

static int xebec_data_in(void)
{
  switch (state.phase) {
  case XEBEC_PH_DATA_IN:
    if (state.bytesdone < state.datalen) {
      int v = state.buf[state.bytesdone++];

      if (state.bytesdone == state.datalen) {
        if (state.command == TRS_XEBEC_READ && --state.blocks > 0) {
          /* More sectors in this command: refill the buffer from the
           * next sector, file position is already there */
          FILE *f = state.d[state.lun].file;

          if (f && fread(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
            if (ferror(f)) {
              file_error("reading xebec%d", state.lun);
              xebec_finish(-1);
              return v;
            }
          }
          state.bytesdone = 0;
        } else {
          xebec_finish(0);
        }
      }
      return v;
    }
    break;

  case XEBEC_PH_STATUS: {
    /* Two bytes of completion status are passed to the host: the
     * next-to-last (error+LUN) byte, then a final zero byte that signals
     * "done". Reading the last byte returns the bus to idle. */
    int v = state.status_index == 0 ? state.final_status : 0;

    if (state.status_index == 0) {
      state.status_index = 1;
    } else {
      trs_xebec_init(0);
    }
    return v;
  }

  default:
    break;
  }
  return 0xff;
}

/*
 * Convert a flat logical block address to cyl/head/sector using the
 * attached drive's geometry, then position the file at the start of
 * that sector. Returns 0 if OK, -1 otherwise.
 */
static int xebec_seek(int lun, long lba)
{
  HardImage *d = &state.d[lun];
  long cyl, head, sector;

  if (d->file == NULL && xebec_open(lun) != 0) return -1;

  sector = lba % d->secs;
  cyl    = lba / d->secs;
  head   = cyl % d->heads;
  cyl    = cyl / d->heads;

  if (d->file && fseek(d->file,
      hard_image_offset(d, state.secsize, cyl, head, sector), 0) != 0) {
    file_error("xebec%d: fseek '%s'", lun, d->filename);
    return -1;
  }

  if (trs_show_led)
    trs_hard_led(lun, 1);

  return 0;
}

/*
 * Open (if needed) the image for a drive, parse its Reed header, and
 * derive geometry. As with trs_omti.c, this protocol has no live
 * sector-size register, so a fixed size is used unconditionally.
 */
static int xebec_open(int drive)
{
  HardImage *d = &state.d[drive];

  if (hard_image_open(d, drive, "xebec",
                      XEBEC_SEC_PER_TRK, XEBEC_MAXHEADS) != 0)
    return -1;

  state.status = XEBEC_STATUS_IDLE;
  return 0;
}

static void trs_save_xebecdrive(FILE *file, HardImage *d)
{
  int file_not_null = (d->file != NULL);

  trs_save_int(file, &file_not_null, 1);
  trs_save_filename(file, d->filename);
  trs_save_int(file, &d->writeprot, 1);
  trs_save_int(file, &d->cyls, 1);
  trs_save_int(file, &d->heads, 1);
  trs_save_int(file, &d->secs, 1);
}

static void trs_load_xebecdrive(FILE *file, HardImage *d)
{
  int file_not_null;

  trs_load_int(file, &file_not_null, 1);

  d->file = file_not_null ? (FILE *) 1 : NULL;

  trs_load_filename(file, d->filename);
  trs_load_int(file, &d->writeprot, 1);
  trs_load_int(file, &d->cyls, 1);
  trs_load_int(file, &d->heads, 1);
  trs_load_int(file, &d->secs, 1);
}

void trs_xebec_save(FILE *file)
{
  int i;
  int phase = (int)state.phase;

  trs_save_int(file, &state.present, 1);
  trs_save_int(file, &phase, 1);
  trs_save_uint8(file, &state.status, 1);
  trs_save_uint8(file, state.dcb, TRS_XEBEC_DCBLEN);
  trs_save_int(file, &state.dcb_index, 1);
  trs_save_uint8(file, &state.command, 1);
  trs_save_int(file, &state.lun, 1);
  trs_save_uint8(file, state.buf, XEBEC_SECBUFSIZE);
  trs_save_uint8(file, state.fillbuf, XEBEC_SECBUFSIZE);
  trs_save_int(file, &state.bytesdone, 1);
  trs_save_int(file, &state.datalen, 1);
  trs_save_int(file, &state.secsize, 1);
  trs_save_int(file, &state.blocks, 1);
  trs_save_uint8(file, &state.busdata, 1);
  trs_save_uint8(file, &state.final_status, 1);
  trs_save_int(file, &state.status_index, 1);

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++)
    trs_save_xebecdrive(file, &state.d[i]);
}

void trs_xebec_load(FILE *file)
{
  int i;
  int phase;

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++) {
    if (state.d[i].file != NULL)
      fclose(state.d[i].file);
  }

  trs_load_int(file, &state.present, 1);
  trs_load_int(file, &phase, 1);
  state.phase = (XebecPhase)phase;
  trs_load_uint8(file, &state.status, 1);
  trs_load_uint8(file, state.dcb, TRS_XEBEC_DCBLEN);
  trs_load_int(file, &state.dcb_index, 1);
  trs_load_uint8(file, &state.command, 1);
  trs_load_int(file, &state.lun, 1);
  trs_load_uint8(file, state.buf, XEBEC_SECBUFSIZE);
  trs_load_uint8(file, state.fillbuf, XEBEC_SECBUFSIZE);
  trs_load_int(file, &state.bytesdone, 1);
  trs_load_int(file, &state.datalen, 1);
  trs_load_int(file, &state.secsize, 1);
  trs_load_int(file, &state.blocks, 1);
  trs_load_uint8(file, &state.busdata, 1);
  trs_load_uint8(file, &state.final_status, 1);
  trs_load_int(file, &state.status_index, 1);

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++) {
    trs_load_xebecdrive(file, &state.d[i]);

    if (state.d[i].file != NULL) {
      state.d[i].file = fopen(state.d[i].filename, "rb+");
      if (state.d[i].file == NULL) {
        state.d[i].file = fopen(state.d[i].filename, "rb");
        if (state.d[i].file == NULL) {
          file_error("load xebec%d: '%s'", i, state.d[i].filename);
          state.d[i].filename[0] = 0;
          state.d[i].writeprot = 0;
          continue;
        }
        state.d[i].writeprot = 1;
      } else {
        state.d[i].writeprot = 0;
      }
    }
  }
}
