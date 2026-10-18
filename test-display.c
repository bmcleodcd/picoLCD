#include <assert.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#undef DefaultRootWindow
#define DefaultRootWindow(display) ((Window)1)
#define XRRGetOutputPrimary fake_primary
#define XRRGetScreenResourcesCurrent fake_resources
#define XRRGetOutputInfo fake_output
#define XRRGetCrtcInfo fake_crtc
#define XRRFreeScreenResources fake_free_resources
#define XRRFreeOutputInfo fake_free_output
#define XRRFreeCrtcInfo fake_free_crtc
#include "src/panel.c"
#define DISPLAY_PROBE_NO_MAIN
#include "src/display-probe.c"

static RROutput primary = 2, outputs[] = {1, 2};
static XRRModeInfo timings[] = {
    {.id=10, .width=1920, .height=1080, .dotClock=148500000, .hTotal=2200, .vTotal=1125},
    {.id=20, .width=3840, .height=2160, .dotClock=297000000, .hTotal=4400, .vTotal=2250}
};
static XRRScreenResources resources = {.noutput=2, .outputs=outputs, .nmode=2, .modes=timings};
static XRROutputInfo output_info[2] = {
    {.crtc=1, .connection=RR_Connected}, {.crtc=2, .connection=RR_Connected}
};
static XRRCrtcInfo crtc_info[2] = {{.mode=10}, {.mode=20}};
static int missing, resources_freed, outputs_freed, crtcs_freed;
RROutput fake_primary(Display *d, Window w) { (void)d; (void)w; return primary; }
XRRScreenResources *fake_resources(Display *d, Window w)
{ (void)d; (void)w; return missing ? NULL : &resources; }
XRROutputInfo *fake_output(Display *d, XRRScreenResources *r, RROutput o)
{ (void)d; assert(r == &resources); return &output_info[o-1]; }
XRRCrtcInfo *fake_crtc(Display *d, XRRScreenResources *r, RRCrtc c)
{ (void)d; assert(r == &resources); return &crtc_info[c-1]; }
void fake_free_resources(XRRScreenResources *r) { assert(r == &resources); ++resources_freed; }
void fake_free_output(XRROutputInfo *o) { assert(o); ++outputs_freed; }
void fake_free_crtc(XRRCrtcInfo *c) { assert(c); ++crtcs_freed; }

int main(void)
{
    {
        char directory[] = "/tmp/picolcd-display-test-XXXXXX", path[256];
        assert(mkdtemp(directory));
        snprintf(path, sizeof(path), "%s/mode", directory);
        struct display_overlay restored = {0};
        display_restore(path, &restored);
        assert(!restored.initialized); /* Cold boot has no remembered mode. */
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            struct display_mode retro = {1920, 1080, 5000, 0};
            display_save(path, &retro, 0);
            _exit(1); /* Same abrupt exit as the Xlib I/O error handler. */
        }
        int status;
        assert(waitpid(child, &status, 0) == child && WEXITSTATUS(status) == 1);
        display_restore(path, &restored);
        assert(restored.initialized && restored.mode.centihz == 5000);
        struct display_mode kodi = {3840, 2160, 3000, 0};
        display_observe(&restored, &kodi, 10);
        assert(strstr(display_status(&restored, 10, 0, "CPU"), "3840x2160 @ 30.00Hz"));
        assert(strcmp(display_status(&restored, 13, 0, "CPU"), "CPU") == 0);
        display_save(path, &kodi, 0);
        restored = (struct display_overlay){0};
        display_restore(path, &restored);
        display_observe(&restored, &kodi, 14);
        assert(restored.until == 0); /* Restart at the same mode stays silent. */
        /* RetroArch can restore the desktop mode immediately before X exits.
         * Replay that interrupted popup even though the restored mode matches. */
        display_save(path, &kodi, 1);
        restored = (struct display_overlay){0};
        display_restore(path, &restored);
        assert(restored.replay);
        display_observe(&restored, &kodi, 20);
        assert(restored.until == 23 && !restored.replay);
        assert(strstr(display_status(&restored, 22.99, 0, "CPU"), "3840x2160"));
        assert(strcmp(display_status(&restored, 23, 0, "CPU"), "CPU") == 0);
        FILE *bad = fopen(path, "w");
        assert(bad); fputs("v1 0 1080 5000 0\n", bad); fclose(bad);
        restored = (struct display_overlay){0};
        display_restore(path, &restored);
        assert(!restored.initialized);
        assert(unlink(path) == 0 && rmdir(directory) == 0);
    }
    struct display_mode mode;
    struct display_overlay overlay = {0};
    assert(read_display_mode(NULL, &mode));
    assert(mode.width == 3840 && mode.height == 2160 && mode.centihz == 3000);
    assert(resources_freed == 1 && outputs_freed == 1 && crtcs_freed == 1);
    display_observe(&overlay, &mode, 1);
    assert(strcmp(display_status(&overlay, 1, 0, "CPU"), "CPU") == 0);
    display_observe(&overlay, &mode, 2);
    assert(overlay.until == 0); /* No startup or repeated-sample popup. */
    primary = None;
    assert(read_display_mode(NULL, &mode) && mode.width == 1920);
    display_observe(&overlay, &mode, 3);
    assert(strcmp(display_status(&overlay, 3, 0, "progress"), "1920x1080 @ 60.00Hz ") == 0);
    assert(strcmp(display_status(&overlay, 5.99, 0, "progress"), overlay.text) == 0);
    assert(strcmp(display_status(&overlay, 6, 0, "progress"), "progress") == 0);
    timings[0].dotClock = 148351648;
    assert(read_display_mode(NULL, &mode) && mode.centihz == 5994);
    display_observe(&overlay, &mode, 7);
    assert(strncmp(overlay.text, "1920x1080 @ 59.94Hz", 18) == 0);
    assert(strcmp(display_status(&overlay, 8, 1, "Volume 65%"), "Volume 65%") == 0);
    assert(overlay.until == 11);
    assert(strcmp(display_status(&overlay, 10.99, 0, "CPU"), overlay.text) == 0);
    assert(strcmp(display_status(&overlay, 11, 0, "CPU"), "CPU") == 0);
    /* New change replaces an existing popup and starts a fresh interval. */
    mode.centihz = 2398;
    display_observe(&overlay, &mode, 12);
    mode.centihz = 2400;
    display_observe(&overlay, &mode, 13);
    assert(overlay.until == 16 && strstr(overlay.text, "24.00Hz"));
    output_info[0].connection = RR_Disconnected;
    assert(read_display_mode(NULL, &mode) && mode.width == 3840);
    primary = 1; /* A disabled primary must not hide the active HDMI output. */
    assert(read_display_mode(NULL, &mode) && mode.width == 3840);
    primary = None;
    output_info[1].crtc = None;
    assert(!read_display_mode(NULL, &mode));
    missing = 1;
    assert(!read_display_mode(NULL, &mode));
    assert(overlay.mode.centihz == 2400); /* Failed reads never change the baseline. */
    XRRModeInfo timing = timings[0];
    timing.dotClock = 74250000;
    timing.modeFlags = RR_Interlace;
    assert(display_mode_from_timing(&timing, &mode) && mode.centihz == 6000 && mode.interlaced);
    display_observe(&overlay, &mode, 17);
    assert(strstr(overlay.text, "1080i @ 60.00Hz"));
    timing.modeFlags = RR_DoubleScan;
    assert(display_mode_from_timing(&timing, &mode) && mode.centihz == 1500);
    timing.hTotal = 0;
    assert(!display_mode_from_timing(&timing, &mode));
    mode = (struct display_mode){7680, 4320, 14400, 0};
    display_observe(&overlay, &mode, 18);
    assert(strlen(overlay.text) == 20 && strstr(overlay.text, "144.00Hz"));
    puts("Display tests passed: primary/fallback, timings, changes, expiry, volume priority, unavailable output");
    return 0;
}
