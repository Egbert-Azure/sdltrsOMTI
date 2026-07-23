/*
 * Shared Reed-format hard-disk image handling.  See trs_hard_image.h for
 * the rationale: the WD1000/1010, OMTI 5527 and Xebec S1410 backends all
 * store disks in the same Reed .hdv format and decode geometry from it
 * identically, so that common code lives here rather than in triplicate.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs_hard_image.h"

int hard_image_open(HardImage *d, int unit, const char *label,
                    int sec_per_trk, int maxheads)
{
  ReedHardHeader rhh;
  size_t res;
  int secs;

  if (d->filename[0] == 0)
    goto fail;

  if (d->file != NULL) {
    fclose(d->file);
    d->file = NULL;
  }

  /* First try opening for reading and writing */
  d->file = fopen(d->filename, "rb+");
  if (d->file == NULL) {
    /* No luck; try read-only (write protected) */
    if (errno == EACCES || errno == EROFS)
      d->file = fopen(d->filename, "rb");
    if (d->file == NULL) {
      file_error("open %s%d: '%s'", label, unit, d->filename);
      goto fail;
    }
    d->writeprot = 1;
  } else {
    d->writeprot = 0;
  }

  /* Read the Reed header and check some basic magic numbers (not all) */
  res = fread(&rhh, sizeof(rhh), 1, d->file);
  if (res != 1 || rhh.id1 != 0x56 || rhh.id2 != 0xcb || rhh.ver >= 0x20) {
    error("unrecognized %s%d drive image: '%s'", label, unit, d->filename);
    goto fail;
  }

  if (rhh.flag1 & 0x80) d->writeprot = 1;

  /* Number of cylinders from the header (0/0 means 256, per reed.h) */
  d->cyls = (rhh.cylhi << 8) | (rhh.cyllo & 0xff);

  secs = rhh.sec ? rhh.sec : 256;
  if (rhh.heads == 0) {
    /* Header gives only sectors/cylinder; assume sec_per_trk and derive
       the head count from it. */
    d->secs  = sec_per_trk;
    d->heads = secs / sec_per_trk;
  } else {
    d->heads = rhh.heads;
    d->secs  = secs / d->heads;
  }

  if ((secs % d->secs) != 0 || d->heads <= 0 || d->heads > maxheads) {
    error("unusable geometry (%d heads/%d secs) in %s%d image: '%s'",
          d->heads, d->secs, label, unit, d->filename);
    goto fail;
  }

  return 0;

fail:
  if (d->file) fclose(d->file);
  d->file = NULL;
  d->filename[0] = 0;
  return -1;
}

long hard_image_offset(const HardImage *d, int secsize,
                       int cyl, int head, int sec)
{
  return sizeof(ReedHardHeader) +
         (long)secsize * ((cyl * d->heads + head) * d->secs + sec);
}
