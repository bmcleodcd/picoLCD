/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#ifndef PANEL_DISPLAY_MONITOR_H
#define PANEL_DISPLAY_MONITOR_H
#include <sys/types.h>
struct display_mode {
    unsigned int width, height, centihz;
    int interlaced;
};

struct display_monitor { pid_t pid; int fd; double next_start,last_sample; };
int display_monitor_poll(struct display_monitor *,double,struct display_mode *);
void display_monitor_close(struct display_monitor *);
#endif
