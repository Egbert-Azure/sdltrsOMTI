/*
 * Generic hard-disk controller dispatch.
 *
 * The emulator has three hard-disk controller backends — WD1000/1010
 * (trs_hard.c), OMTI 5527 (trs_omti.c) and Xebec S1410 (trs_xebec.c) —
 * each with its own attach/remove/getfilename/geometry entry points.  The
 * GUI, config and write-protect code shouldn't care which one a given
 * drive slot belongs to; these helpers dispatch a (controller-type, unit)
 * pair to the right backend so callers can treat all hard-disk slots
 * uniformly.
 *
 * The controller type is one of the HARD_DRIVE / OMTI_DRIVE / XEBEC_DRIVE
 * constants from trs_mkdisk.h (the same tags the GUI menu rows already
 * carry).  unit is the drive index within that controller.
 */

#ifndef _TRS_HDCTL_H
#define _TRS_HDCTL_H

/* True if type is one of the hard-disk controller types. */
extern int  hdctl_is_hard_type(int type);

/* Number of drive slots this controller exposes (0 if not a hard type). */
extern int  hdctl_maxdrives(int type);

extern void hdctl_attach(int type, int unit, const char *filename);
extern void hdctl_remove(int type, int unit);
extern const char *hdctl_getfilename(int type, int unit);
extern int  hdctl_getwriteprotect(int type, int unit);
extern void hdctl_getgeometry(int type, int unit, int *cyls, int *heads, int *secs);

#endif /* _TRS_HDCTL_H */
