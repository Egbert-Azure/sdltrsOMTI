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
 * independent hard disk interface from the Western Digital WD1000/WD1010
 * emulated in trs_hard.c (which the Genie IIIs also supports, relocated
 * to 0xC8-0xCF).
 *
 * Protocol reverse-engineered from Thomas Holte's CP/M 3.0 BIOS driver
 * (hd2.mac, by Peter Petersen / H. Bernhardt / V. Dose): a SELECT strobe,
 * a 6-byte SASI command descriptor block (CDB) sent one byte at a time
 * gated by bit 0 of the status register, a data phase (sector data for
 * READ/WRITE, or an 8-byte parameter block for SET DRIVE CHARACTERISTICS),
 * and a final status byte read back from the command port.
 *
 * Disk images use the same 256-byte Reed header (reed.h) as trs_hard.c's
 * WD1000/1010 images.  Unlike WD1010, this protocol has no live
 * sector-size register; the OMTI 5010's drives (e.g. the Seagate ST225)
 * are fixed at 512 bytes/sector, so that size is used unconditionally.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs.h"
#include "trs_omti.h"
#include "trs_state_save.h"

#define OMTIDEBUG1 (1 << 2)  /* show detail on all port i/o */
#define OMTIDEBUG2 (1 << 3)  /* show all commands */

#define OMTI_SEC_PER_TRK 32     /* fallback if the header omits head count */
#define OMTI_MAXHEADS 8
#define OMTI_DEFAULT_SECSIZE 512
#define OMTI_SECBUFSIZE 1024

typedef enum {
  OMTI_PH_IDLE,
  OMTI_PH_CDB,
  OMTI_PH_DATA_IN,
  OMTI_PH_DATA_OUT,
  OMTI_PH_STATUS
} OmtiPhase;

/* Structure describing one drive (LUN) */
typedef struct {
  FILE* file;
  char filename[FILENAME_MAX];
  int writeprot;
  int cyls;
  int heads;
  int secs;
} Drive;

/* Structure describing controller state */
typedef struct {
  int present;
  OmtiPhase phase;
  Uint8 status;
  Uint8 mask;

  Uint8 cdb[TRS_OMTI_CDBLEN];
  int cdb_index;
  Uint8 command;
  int lun;

  Uint8 buf[OMTI_SECBUFSIZE];
  int bytesdone;
  int datalen;
  int secsize;
  Uint8 final_status;

  Drive d[TRS_OMTI_MAXDRIVES];
} State;

static State state;

static int  omti_open(int drive);
static int  omti_seek(int lun, int cyl, int head, int sector);
static void omti_command(void);
static void omti_finish(int ok);
static int  omti_data_in(void);
static void omti_data_out(int value);

#ifdef ZBX
void trs_omti_debug(void)
{
  int i;

  printf("OMTI hard disk controller state:");
  if (state.present == 0) {
    puts(" DISABLED");
    return;
  }

  printf("\n  phase:%d, lun:%d, command:0x%02X, status:0x%02X, secsize:%d\n",
      state.phase, state.lun, state.command, state.status, state.secsize);

  for (i = 0; i < TRS_OMTI_MAXDRIVES; i++) {
    if (state.d[i].file) {
      printf("\nomti%d: '%s'\n", i, state.d[i].filename);
      printf("\theads %d, cyls %4d, secs %4d, writeprot %d\n",
          state.d[i].heads, state.d[i].cyls, state.d[i].secs,
          state.d[i].writeprot);
    }
  }
}
#endif

/* Powerup or reset button */
void trs_omti_init(int poweron)
{
  state.phase = OMTI_PH_IDLE;
  state.status = TRS_OMTI_IDLE;
  state.cdb_index = 0;
  state.command = 0;
  state.lun = 0;
  state.bytesdone = 0;
  state.datalen = 0;
  state.final_status = 0;
  memset(state.buf, 0, sizeof(state.buf));

  if (poweron) {
    int i;

    state.present = 0;
    state.mask = 0;

    for (i = 0; i < TRS_OMTI_MAXDRIVES; i++) {
      state.d[i].writeprot = 0;
      state.d[i].cyls = 0;
      state.d[i].heads = 0;
      state.d[i].secs = 0;

      if (omti_open(i) == 0) state.present = 1;
    }
  }
}

void trs_omti_attach(int drive, const char *diskname)
{
  snprintf(state.d[drive].filename, FILENAME_MAX, "%s", diskname);

  if (omti_open(drive) != 0)
    trs_omti_remove(drive);
}

void trs_omti_remove(int drive)
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
trs_omti_getfilename(int unit)
{
  return state.d[unit].filename;
}

int
trs_omti_getwriteprotect(int unit)
{
  return state.d[unit].writeprot;
}

void
trs_omti_getgeometry(int unit, int *cyls, int *head, int *secs)
{
  if (state.d[unit].file) {
    *cyls = state.d[unit].cyls;
    *head = state.d[unit].heads;
    *secs = state.d[unit].secs;
  }
}

/* Read from an I/O port mapped to the controller */
int trs_omti_in(int port)
{
  int v = 0xff;

  if (state.present) {
    switch (port) {
    case TRS_OMTI_PORT:
      v = omti_data_in();
      break;
    case TRS_OMTI_STATUS:
      v = state.status;
      break;
    case TRS_OMTI_SELECT:
      /* Not used by the CP/M BIOS driver (hd2.mac), which only ever
       * writes here to strobe SELECT. But the OMTI boot EPROM reads
       * this port at reset to detect card presence and expects a
       * fixed 0xFA signature byte back before it will proceed. */
      v = 0xfa;
      break;
    case TRS_OMTI_MASK:
      v = state.mask;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & OMTIDEBUG1)
    debug("[PC=%04X] trs_omti_in(%02X) => %02X\n", Z80_PC, port, v);
#endif
  return v;
}

/* Write to an I/O port mapped to the controller */
void trs_omti_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & OMTIDEBUG1)
    debug("[PC=%04X] trs_omti_out(%02X), %02X\n", Z80_PC, port, value);
#endif
  switch (port) {
  case TRS_OMTI_PORT:
    omti_data_out(value);
    break;
  case TRS_OMTI_STATUS:
    /* Any write is a software reset: abort to idle */
    trs_omti_init(0);
    break;
  case TRS_OMTI_SELECT:
    if (state.phase == OMTI_PH_IDLE) {
      state.phase = OMTI_PH_CDB;
      state.cdb_index = 0;
      state.status = TRS_OMTI_DATA_OUT; /* bit0 set: ready for CDB byte */
    }
    break;
  case TRS_OMTI_MASK:
    state.mask = value; /* DMA/interrupt mask; not emulated */
    break;
  }
}

static void omti_finish(int ok)
{
  state.phase = OMTI_PH_STATUS;
  state.status = TRS_OMTI_STATUS_RDY;
  state.final_status = ok == 0 ? 0 : TRS_OMTI_ST_ERROR;
}

static void omti_command(void)
{
  int head, sector, cyl;

  state.command = state.cdb[0];
  state.lun     = (state.cdb[1] & TRS_OMTI_CDB1_LUNMASK) >> TRS_OMTI_CDB1_LUNSHIFT;
  head          = state.cdb[1] & TRS_OMTI_CDB1_HEADMASK;
  sector        = state.cdb[2] & TRS_OMTI_CDB2_SECMASK;
  cyl           = ((state.cdb[1] & TRS_OMTI_CDB1_CYL10) ? 0x400 : 0)
                | (((state.cdb[2] & TRS_OMTI_CDB2_CYLMASK)
                     >> TRS_OMTI_CDB2_CYLSHIFT) << 8)
                | state.cdb[3];

  state.bytesdone = 0;

#if ZBX
  if (trs_io_debug_flags & OMTIDEBUG2)
    debug("trs_omti: command 0x%02X lun:%d cyl:%d head:%d sec:%d\n",
        state.command, state.lun, cyl, head, sector);
#endif

  switch (state.command) {
  case TRS_OMTI_READ:
    if (omti_seek(state.lun, cyl, head, sector) == 0) {
      FILE *f = state.d[state.lun].file;

      if (f && fread(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
        if (ferror(f)) {
          file_error("reading omti%d", state.lun);
          omti_finish(-1);
          break;
        }
      }
      state.datalen = state.secsize;
      state.phase = OMTI_PH_DATA_IN;
      state.status = TRS_OMTI_DATA_IN;
    } else {
      omti_finish(-1);
    }
    break;

  case TRS_OMTI_WRITE:
    if (omti_seek(state.lun, cyl, head, sector) == 0) {
      state.datalen = state.secsize;
      state.phase = OMTI_PH_DATA_OUT;
      state.status = TRS_OMTI_DATA_OUT;
    } else {
      omti_finish(-1);
    }
    break;

  case TRS_OMTI_FORMAT:
    if (omti_seek(state.lun, cyl, head, sector) == 0) {
      FILE *f = state.d[state.lun].file;

      memset(state.buf, 0, sizeof(state.buf));
      if (f && fwrite(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
        if (errno) {
          file_error("formatting omti%d", state.lun);
          omti_finish(-1);
          break;
        }
      }
      omti_finish(0);
    } else {
      omti_finish(-1);
    }
    break;

  case TRS_OMTI_SET_CHARACTERISTICS:
    state.datalen = TRS_OMTI_CHARLEN;
    state.phase = OMTI_PH_DATA_OUT;
    state.status = TRS_OMTI_DATA_OUT;
    break;

  case TRS_OMTI_SEEK:
    omti_finish(omti_seek(state.lun, cyl, head, sector));
    break;

  case TRS_OMTI_REZERO:
    omti_finish(omti_seek(state.lun, 0, 0, 0));
    break;

  case TRS_OMTI_TEST_UNIT_READY:
    omti_finish(state.d[state.lun].file != NULL ||
                omti_open(state.lun) == 0 ? 0 : -1);
    break;

  case TRS_OMTI_REQUEST_SENSE:
    omti_finish(0);
    break;

  default:
    error("trs_omti: unknown command 0x%02X", state.command);
    omti_finish(-1);
    break;
  }
}

static void omti_data_out(int value)
{
  switch (state.phase) {
  case OMTI_PH_CDB:
    state.cdb[state.cdb_index++] = (Uint8)value;
    if (state.cdb_index == TRS_OMTI_CDBLEN)
      omti_command();
    break;

  case OMTI_PH_DATA_OUT:
    if (state.bytesdone < state.datalen) {
      state.buf[state.bytesdone++] = (Uint8)value;
      if (state.bytesdone == state.datalen) {
        if (state.command == TRS_OMTI_WRITE) {
          FILE *f = state.d[state.lun].file;

          if (f && fwrite(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
            if (errno) {
              file_error("writing omti%d", state.lun);
              omti_finish(-1);
              break;
            }
          }
        } else if (state.command == TRS_OMTI_SET_CHARACTERISTICS) {
          /* The guest driver is telling us the real geometry of the
           * drive it's talking to. Adopt it: the Reed header's cyl/head
           * values are only a bootstrap guess made at attach time, and
           * a real OMTI controller is programmed for its drive via this
           * command, not by inspecting the media. */
          Drive *d = &state.d[state.lun];
          int cyls  = (state.buf[TRS_OMTI_CHAR_CYLHI] << 8)
                    | state.buf[TRS_OMTI_CHAR_CYLLO];
          int heads = state.buf[TRS_OMTI_CHAR_HEADS];

          if (cyls > 0)
            d->cyls = cyls;
          if (heads > 0 && heads <= OMTI_MAXHEADS)
            d->heads = heads;
        }
        omti_finish(0);
      }
    }
    break;

  default:
    break;
  }
}

static int omti_data_in(void)
{
  switch (state.phase) {
  case OMTI_PH_DATA_IN:
    if (state.bytesdone < state.datalen) {
      int v = state.buf[state.bytesdone++];

      if (state.bytesdone == state.datalen)
        omti_finish(0);
      return v;
    }
    break;

  case OMTI_PH_STATUS: {
    int v = state.final_status;

    /* Reading the final status byte returns the bus to idle. */
    trs_omti_init(0);
    return v;
  }

  default:
    break;
  }
  return 0xff;
}

/*
 * Check whether the requested head/sector are in bounds and position the
 * file at the start of the requested sector.  Cylinder bounds are not
 * checked, matching trs_hard.c's hard_sector() (some drivers issue
 * harmlessly out-of-range seeks).  Returns 0 if OK, -1 otherwise.
 */
static int omti_seek(int lun, int cyl, int head, int sector)
{
  Drive *d = &state.d[lun];

  if (d->file == NULL && omti_open(lun) != 0) return -1;

  /* Some drivers address sectors as a flat index across the whole
   * cylinder (0..heads*secs-1) rather than pre-splitting into head and
   * a 0..secs-1 sector-within-track, leaving head at 0 in the CDB and
   * letting "sector" run past secs-1. Carry any overflow into head here. */
  head += sector / d->secs;
  sector %= d->secs;

  if (head >= d->heads) {
    error("omti%d: requested cyl:%d, head:%d, sec:%d (cyls:%d, heads:%d, secs:%d)",
        lun, cyl, head, sector, d->cyls, d->heads, d->secs);
    return -1;
  }

  if (d->file && fseek(d->file, sizeof(ReedHardHeader) +
      (long)state.secsize * ((cyl * d->heads + head) * d->secs + sector), 0) != 0) {
    file_error("omti%d: fseek '%s'", lun, d->filename);
    return -1;
  }

  if (trs_show_led)
    trs_hard_led(lun, 1);

  return 0;
}

/*
 * Open (if needed) the image for a drive, parse its Reed header, and
 * derive geometry.  Also derives the controller's sector size from the
 * file's data size and that geometry, since this protocol (unlike
 * WD1000/1010) has no live sector-size register.
 */
static int omti_open(int drive)
{
  Drive *d = &state.d[drive];
  ReedHardHeader rhh;
  size_t res;
  int secs;

  if (d->filename[0] == 0)
    goto fail;

  if (d->file != NULL) {
    fclose(d->file);
    d->file = NULL;
  }

  d->file = fopen(d->filename, "rb+");
  if (d->file == NULL) {
    if (errno == EACCES || errno == EROFS)
      d->file = fopen(d->filename, "rb");
    if (d->file == NULL) {
      file_error("open omti%d: '%s'", drive, d->filename);
      goto fail;
    }
    d->writeprot = 1;
  } else {
    d->writeprot = 0;
  }

  res = fread(&rhh, sizeof(rhh), 1, d->file);
  if (res != 1 || rhh.id1 != 0x56 || rhh.id2 != 0xcb || rhh.ver >= 0x20) {
    error("unrecognized omti%d drive image: '%s'", drive, d->filename);
    goto fail;
  }

  if (rhh.flag1 & 0x80) d->writeprot = 1;

  d->cyls = (rhh.cylhi << 8) | (rhh.cyllo & 0xff);

  secs = rhh.sec ? rhh.sec : 256;
  if (rhh.heads == 0) {
    d->secs  = OMTI_SEC_PER_TRK;
    d->heads = secs / OMTI_SEC_PER_TRK;
  } else {
    d->heads = rhh.heads;
    d->secs  = secs / d->heads;
  }

  if ((secs % d->secs) != 0 || d->heads <= 0 || d->heads > OMTI_MAXHEADS) {
    error("unusable geometry (%d heads/%d secs) in omti%d image: '%s'",
        d->heads, d->secs, drive, d->filename);
    goto fail;
  }

  /* No sector-size field exists in this protocol. The OMTI 5010 and the
   * ST-506/MFM drives it was paired with (e.g. the Seagate ST225) are
   * fixed at 512 bytes/sector, so use that rather than trying to infer
   * a size from the image file. */
  state.secsize = OMTI_DEFAULT_SECSIZE;

  state.status = TRS_OMTI_IDLE;
  return 0;

fail:
  if (d->file) fclose(d->file);
  d->file = NULL;
  d->filename[0] = 0;
  return -1;
}

static void trs_save_omtidrive(FILE *file, Drive *d)
{
  int file_not_null = (d->file != NULL);

  trs_save_int(file, &file_not_null, 1);
  trs_save_filename(file, d->filename);
  trs_save_int(file, &d->writeprot, 1);
  trs_save_int(file, &d->cyls, 1);
  trs_save_int(file, &d->heads, 1);
  trs_save_int(file, &d->secs, 1);
}

static void trs_load_omtidrive(FILE *file, Drive *d)
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

void trs_omti_save(FILE *file)
{
  int i;
  int phase = (int)state.phase;

  trs_save_int(file, &state.present, 1);
  trs_save_int(file, &phase, 1);
  trs_save_uint8(file, &state.status, 1);
  trs_save_uint8(file, &state.mask, 1);
  trs_save_uint8(file, state.cdb, TRS_OMTI_CDBLEN);
  trs_save_int(file, &state.cdb_index, 1);
  trs_save_uint8(file, &state.command, 1);
  trs_save_int(file, &state.lun, 1);
  trs_save_uint8(file, state.buf, OMTI_SECBUFSIZE);
  trs_save_int(file, &state.bytesdone, 1);
  trs_save_int(file, &state.datalen, 1);
  trs_save_int(file, &state.secsize, 1);
  trs_save_uint8(file, &state.final_status, 1);

  for (i = 0; i < TRS_OMTI_MAXDRIVES; i++)
    trs_save_omtidrive(file, &state.d[i]);
}

void trs_omti_load(FILE *file)
{
  int i;
  int phase;

  for (i = 0; i < TRS_OMTI_MAXDRIVES; i++) {
    if (state.d[i].file != NULL)
      fclose(state.d[i].file);
  }

  trs_load_int(file, &state.present, 1);
  trs_load_int(file, &phase, 1);
  state.phase = (OmtiPhase)phase;
  trs_load_uint8(file, &state.status, 1);
  trs_load_uint8(file, &state.mask, 1);
  trs_load_uint8(file, state.cdb, TRS_OMTI_CDBLEN);
  trs_load_int(file, &state.cdb_index, 1);
  trs_load_uint8(file, &state.command, 1);
  trs_load_int(file, &state.lun, 1);
  trs_load_uint8(file, state.buf, OMTI_SECBUFSIZE);
  trs_load_int(file, &state.bytesdone, 1);
  trs_load_int(file, &state.datalen, 1);
  trs_load_int(file, &state.secsize, 1);
  trs_load_uint8(file, &state.final_status, 1);

  for (i = 0; i < TRS_OMTI_MAXDRIVES; i++) {
    trs_load_omtidrive(file, &state.d[i]);

    if (state.d[i].file != NULL) {
      state.d[i].file = fopen(state.d[i].filename, "rb+");
      if (state.d[i].file == NULL) {
        state.d[i].file = fopen(state.d[i].filename, "rb");
        if (state.d[i].file == NULL) {
          file_error("load omti%d: '%s'", i, state.d[i].filename);
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
