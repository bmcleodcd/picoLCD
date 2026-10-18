/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#ifndef PANEL_ORTEK_H
#define PANEL_ORTEK_H
#include <stddef.h>
struct ortek_decoder { int widths[40]; unsigned count; double last_packet; };
typedef void (*ortek_callback)(void *, unsigned, unsigned);
/* Signed little-endian picoLCD pulse widths, preserved across USB reports. */
void ortek_feed(struct ortek_decoder *, const unsigned char *, size_t, double,
                ortek_callback, void *);
struct ortek_repeat { unsigned key; int active; double started, last, seen; };
int ortek_action(struct ortek_repeat *, unsigned, unsigned, double, int);
void ortek_mouse_feed(struct ortek_decoder *,const unsigned char *,size_t,double,
                      ortek_callback,void *);
struct ortek_mouse_state {
    unsigned direction,buttons;
    double seen,started,next_step;
};
void ortek_mouse_update(struct ortek_mouse_state *,unsigned,unsigned,double);
void ortek_mouse_step(struct ortek_mouse_state *,double,int *,int *,unsigned *);
#endif
