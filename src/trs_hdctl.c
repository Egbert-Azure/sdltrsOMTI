/*
 * Generic hard-disk controller dispatch.  See trs_hdctl.h for rationale:
 * route a (controller-type, unit) pair to the matching WD1000/OMTI/Xebec
 * backend so callers need not special-case each controller.
 */

#include <stddef.h>
#include "trs_hard.h"
#include "trs_hdctl.h"
#include "trs_mkdisk.h"
#include "trs_omti.h"
#include "trs_xebec.h"

int hdctl_is_hard_type(int type)
{
  return type == HARD_DRIVE || type == OMTI_DRIVE || type == XEBEC_DRIVE;
}

/* 0 = no explicit choice pinned yet; otherwise a hard-controller type. */
static int active_type;

void hdctl_set_active(int type)
{
  if (hdctl_is_hard_type(type))
    active_type = type;
}

int hdctl_get_active(void)
{
  if (hdctl_is_hard_type(active_type))
    return active_type;

  /* Nothing pinned: infer from attached images, preferring the SASI
     controllers the Genie IIIs actually uses (Xebec first, as it is the
     genuine hardware). */
  if (hdctl_getfilename(XEBEC_DRIVE, 0)[0] != 0)
    return XEBEC_DRIVE;
  if (hdctl_getfilename(OMTI_DRIVE, 0)[0] != 0)
    return OMTI_DRIVE;
  return HARD_DRIVE;
}

int hdctl_maxdrives(int type)
{
  switch (type) {
    case HARD_DRIVE:  return TRS_HARD_MAXDRIVES;
    case OMTI_DRIVE:  return TRS_OMTI_MAXDRIVES;
    case XEBEC_DRIVE: return TRS_XEBEC_MAXDRIVES;
    default:          return 0;
  }
}

void hdctl_attach(int type, int unit, const char *filename)
{
  switch (type) {
    case HARD_DRIVE:  trs_hard_attach(unit, filename);  break;
    case OMTI_DRIVE:  trs_omti_attach(unit, filename);  break;
    case XEBEC_DRIVE: trs_xebec_attach(unit, filename); break;
  }
}

void hdctl_remove(int type, int unit)
{
  switch (type) {
    case HARD_DRIVE:  trs_hard_remove(unit);  break;
    case OMTI_DRIVE:  trs_omti_remove(unit);  break;
    case XEBEC_DRIVE: trs_xebec_remove(unit); break;
  }
}

const char *hdctl_getfilename(int type, int unit)
{
  switch (type) {
    case HARD_DRIVE:  return trs_hard_getfilename(unit);
    case OMTI_DRIVE:  return trs_omti_getfilename(unit);
    case XEBEC_DRIVE: return trs_xebec_getfilename(unit);
    default:          return "";
  }
}

int hdctl_getwriteprotect(int type, int unit)
{
  switch (type) {
    case HARD_DRIVE:  return trs_hard_getwriteprotect(unit);
    case OMTI_DRIVE:  return trs_omti_getwriteprotect(unit);
    case XEBEC_DRIVE: return trs_xebec_getwriteprotect(unit);
    default:          return 0;
  }
}

void hdctl_getgeometry(int type, int unit, int *cyls, int *heads, int *secs)
{
  switch (type) {
    case HARD_DRIVE:  trs_hard_getgeometry(unit, cyls, heads, secs);  break;
    case OMTI_DRIVE:  trs_omti_getgeometry(unit, cyls, heads, secs);  break;
    case XEBEC_DRIVE: trs_xebec_getgeometry(unit, cyls, heads, secs); break;
  }
}
