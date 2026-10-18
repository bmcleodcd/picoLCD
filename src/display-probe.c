/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
/* Optional XRandR worker: X failures never affect panel input. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <X11/extensions/Xrandr.h>
#include "display-monitor.h"
static int display_mode_from_timing(const XRRModeInfo *timing,
                                    struct display_mode *mode)
{
    if (!timing->width || !timing->height || !timing->dotClock ||
        !timing->hTotal || !timing->vTotal) return 0;
    double hz = (double)timing->dotClock / timing->hTotal / timing->vTotal;
    if (timing->modeFlags & RR_Interlace) hz *= 2;
    if (timing->modeFlags & RR_DoubleScan) hz /= 2;
    if (hz <= 0 || hz > 1000) return 0;
    mode->width = timing->width;
    mode->height = timing->height;
    mode->centihz = (unsigned int)(hz * 100 + 0.5);
    mode->interlaced = !!(timing->modeFlags & RR_Interlace);
    return 1;
}

/* Read the physical output timing, not the combined desktop dimensions.
 * Without an enabled primary, use the first connected, enabled output. */
static int read_display_mode(Display *display, struct display_mode *mode)
{
    Window root = DefaultRootWindow(display);
    RROutput primary = XRRGetOutputPrimary(display, root);
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(display, root);
    int valid = 0;
    if (!resources) return 0;
    for (int i = -1; i < resources->noutput; ++i) {
        RROutput id = i < 0 ? primary : resources->outputs[i];
        if (id == None || (i >= 0 && id == primary)) continue;
        XRROutputInfo *output = XRRGetOutputInfo(display, resources, id);
        if (!output) continue;
        RRCrtc crtc_id = output->connection == RR_Connected ? output->crtc : None;
        XRRFreeOutputInfo(output);
        if (!crtc_id) continue;
        XRRCrtcInfo *crtc = XRRGetCrtcInfo(display, resources, crtc_id);
        if (!crtc) continue;
        for (int j = 0; j < resources->nmode; ++j) {
            if (resources->modes[j].id == crtc->mode) {
                valid = display_mode_from_timing(&resources->modes[j], mode);
                break;
            }
        }
        XRRFreeCrtcInfo(crtc);
        if (valid) break;
    }
    XRRFreeScreenResources(resources);
    return valid;
}


#ifndef DISPLAY_PROBE_NO_MAIN
int main(void)
{
 if(!getenv("XAUTHORITY"))setenv("XAUTHORITY","/home/kodi/.Xauthority",0);
 const char *name=getenv("DISPLAY");
 Display *display=XOpenDisplay(name && *name?name:":0");if(!display)return 1;
 int major=1,minor=3;
 if(!XRRQueryVersion(display,&major,&minor) || major<1 || (major==1 && minor<3))return 1;
 do {
  struct display_mode mode;
  if(read_display_mode(display,&mode) && write(STDOUT_FILENO,&mode,sizeof(mode))!=(ssize_t)sizeof(mode))break;
  usleep(500000);
 } while(1);
 XCloseDisplay(display);return 0;
}
#endif
