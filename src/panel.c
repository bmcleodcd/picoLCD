/* Minibox panel: bounded display updates, direct NVML and systemd D-Bus. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <dlfcn.h>
#include <dbus/dbus.h>
#include "input.h"
#include "display-monitor.h"
#include "panel.h"
#include "media.h"
#include "picolcd-common.h"
#include "ortek.h"

#define LCD_WIDTH 20
#define KEY_COUNT 16
#define F1_KEY 3
#define MARQUEE_INTERVAL 0.4
#define MARQUEE_GAP 3
#ifndef DISPLAY_STATE_PATH
#define DISPLAY_STATE_PATH "/run/picolcd-display-mode"
#endif

static const char *const keymap[KEY_COUNT] = {
    NULL, "XF86AudioRaiseVolume", "XF86AudioLowerVolume", NULL,
    "XF86AudioPlay", "XF86AudioStop", NULL, "Escape",
    "Left", "Right", "Up", "Down", "Return", NULL, NULL, NULL
};
/* Configuration lines: decimal IR command, repeat flag (0/1), X key sequence.
 * Only codes verified against the local capture are supplied by default. */
static double monotonic_seconds(void);
struct ir_binding { char sequence[96]; int repeat; };
static struct ir_binding ir_bindings[64];
struct ir_context {
    struct panel_input *input; struct ortek_repeat repeat; int dongle;
    struct ortek_mouse_state mouse;
    unsigned mouse_sent;
};
static int ortek_dongle_present(void)
{
    glob_t paths; int present=0;
    if(glob("/sys/bus/usb/devices/*/idVendor",0,NULL,&paths)!=0) return 0;
    for(size_t i=0;i<paths.gl_pathc;i++) {
        unsigned vendor=0,product=0; char path[512];
        FILE *f=fopen(paths.gl_pathv[i],"r");
        if(f) { if(fscanf(f,"%x",&vendor)!=1) vendor=0; fclose(f); }
        if(vendor!=0x05a4) continue;
        snprintf(path,sizeof(path),"%.*s/idProduct",(int)(strlen(paths.gl_pathv[i])-9),paths.gl_pathv[i]);
        f=fopen(path,"r");
        if(f) { if(fscanf(f,"%x",&product)!=1) product=0; fclose(f); }
        if(product==0x9881) { present=1; break; }
    }
    globfree(&paths); return present;
}
#ifndef ORTEK_MAP_PATH
#define ORTEK_MAP_PATH "/etc/picolcd-ortek.map"
#endif
static void ortek_load_map(void)
{
    strcpy(ir_bindings[21].sequence,"Up"); ir_bindings[21].repeat=1;
    strcpy(ir_bindings[32].sequence,"Down"); ir_bindings[32].repeat=1;
    strcpy(ir_bindings[34].sequence,"Return");
    FILE *f=fopen(ORTEK_MAP_PATH,"r");
    if(!f) return;
    char line[256],sequence[96]; unsigned key; int repeat;
    while(fgets(line,sizeof(line),f)) {
        if(sscanf(line,"%u %d %95s",&key,&repeat,sequence)==3 && key<64 && (repeat==0||repeat==1)) {
            strcpy(ir_bindings[key].sequence,sequence); ir_bindings[key].repeat=repeat;
        }
    }
    fclose(f);
}
static void ortek_received(void *opaque,unsigned key,unsigned phase)
{
    struct ir_context *ctx=opaque;
    int emit=ortek_action(&ctx->repeat,key,phase,monotonic_seconds(),ir_bindings[key].repeat);
    if(phase==0) fprintf(stderr,"Ortek IR command=%u mapping=%s receiver=%s\n",key,
        ir_bindings[key].sequence[0]?ir_bindings[key].sequence:"unmapped",ctx->dongle?"USB dongle present":"LCD");
    if(emit && ctx->input && !ctx->dongle && ir_bindings[key].sequence[0]) {
        panel_input_tap(ctx->input,ir_bindings[key].sequence);
    }
}
static void ortek_mouse_release(struct ir_context *ctx)
{
    if(ctx->input) for(unsigned bit=0;bit<2;bit++)
        if(ctx->mouse_sent&(1u<<bit)) panel_input_button(ctx->input,bit?3:1,0);
    ctx->mouse_sent=0;
    memset(&ctx->mouse,0,sizeof(ctx->mouse));
}
static void ortek_mouse_received(void *opaque,unsigned direction,unsigned buttons)
{
    struct ir_context *ctx=opaque;
    if(direction!=ctx->mouse.direction || buttons!=ctx->mouse.buttons)
        fprintf(stderr,"Ortek mouse direction=%u buttons=%u receiver=%s\n",direction,buttons,
                ctx->dongle?"USB dongle present":"LCD");
    if(ctx->dongle || !ctx->input) return;
    ortek_mouse_update(&ctx->mouse,direction,buttons,monotonic_seconds());
}
static void ortek_mouse_output(struct ir_context *ctx,double now)
{
    if(ctx->dongle || !ctx->input) { ortek_mouse_release(ctx);return; }
    int dx,dy;unsigned buttons;
    ortek_mouse_step(&ctx->mouse,now,&dx,&dy,&buttons);
    for(unsigned bit=0;bit<2;bit++) if((buttons^ctx->mouse_sent)&(1u<<bit)) {
        if(buttons&(1u<<bit)) panel_input_button(ctx->input,bit?3:1,1);
        else panel_input_button(ctx->input,bit?3:1,0);
    }
    ctx->mouse_sent=buttons;
    if(dx || dy) panel_input_move(ctx->input,dx,dy);
}
static volatile sig_atomic_t stopping;

static void stop_panel(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

static void make_row(char row[LCD_WIDTH + 1], const char *text)
{
    size_t n = strlen(text);
    if (n > LCD_WIDTH) n = LCD_WIDTH;
    memset(row, ' ', LCD_WIDTH);
    memcpy(row, text, n);
    row[LCD_WIDTH] = '\0';
}

static void update_row(usblcd_operations *lcd, int index,
                       char previous[LCD_WIDTH + 1], const char *text)
{
    char row[LCD_WIDTH + 1];
    make_row(row, text);
    if (index == 1) {
        size_t length = strlen(row);
        while (length && row[length - 1] == ' ') --length;
        size_t padding = (LCD_WIDTH - length) / 2;
        memmove(row + padding, row, length);
        memset(row, ' ', padding);
    }
    if (strcmp(previous, row) != 0) {
        if (index == 1 && strstr(row, "Hz"))
            fprintf(stderr, "Display popup sent to LCD: %s\n", row);
        lcd->settext(lcd, index, 0, row);
        memcpy(previous, row, sizeof(row));
    }
}

static void marquee_row(char row[LCD_WIDTH + 1], const char *text, size_t offset)
{
    size_t length = strlen(text), i;
    if (length <= LCD_WIDTH) {
        make_row(row, text);
        return;
    }
    for (i = 0; i < LCD_WIDTH; ++i) {
        size_t position = (offset + i) % (length + MARQUEE_GAP);
        row[i] = position < length ? text[position] : ' ';
    }
    row[LCD_WIDTH] = '\0';
}

struct display_overlay {
    struct display_mode mode;
    int initialized;
    int replay;
    double until;
    char text[LCD_WIDTH + 1];
};

static int display_modes_equal(const struct display_mode *a, const struct display_mode *b)
{
    return a->width == b->width && a->height == b->height &&
        a->centihz == b->centihz && a->interlaced == b->interlaced;
}

/* /run survives an X-loss service restart but is cleared at boot. Normal panel
 * shutdown removes this baseline, keeping an ordinary startup silent. Write
 * before X can fail: the fatal Xlib callback must not attempt stdio cleanup. */
static void display_save(const char *path, const struct display_mode *mode, int pending)
{
    char temporary[256];
    if (!path[0] || snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary))
        return;
    int fd = mkstemp(temporary);
    if (fd < 0) return;
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); unlink(temporary); return; }
    int ok = fprintf(file, "v1 %u %u %u %d %d\n", mode->width, mode->height,
                     mode->centihz, mode->interlaced, pending) > 0;
    if (fclose(file) != 0) ok = 0;
    if (ok && rename(temporary, path) == 0) return;
    unlink(temporary);
}

static void display_restore(const char *path, struct display_overlay *overlay)
{
    FILE *file = path[0] ? fopen(path, "r") : NULL;
    struct display_mode mode = {0};
    int pending = 0;
    if (!file) return;
    int fields = fscanf(file, "v1 %u %u %u %d %d", &mode.width, &mode.height,
                        &mode.centihz, &mode.interlaced, &pending);
    fclose(file);
    if ((fields != 4 && fields != 5) || (pending != 0 && pending != 1) ||
        !mode.width || mode.width > 65535 || !mode.height ||
        mode.height > 65535 || !mode.centihz || mode.centihz > 100000 ||
        (mode.interlaced != 0 && mode.interlaced != 1)) return;
    overlay->mode = mode;
    overlay->initialized = 1;
    overlay->replay = pending;
    fprintf(stderr, "Restored display baseline: %ux%u @ %u.%02uHz\n",
            mode.width, mode.height, mode.centihz / 100, mode.centihz % 100);
}

static void display_observe(struct display_overlay *overlay,
                            const struct display_mode *mode, double now)
{
    int changed = overlay->initialized &&
        (overlay->replay || !display_modes_equal(mode, &overlay->mode));
    overlay->replay = 0;
    if (!overlay->initialized || changed)
        fprintf(stderr, "Display sample (%s): %ux%u @ %u.%02uHz\n",
                changed ? "changed" : "initial", mode->width, mode->height,
                mode->centihz / 100, mode->centihz % 100);
    overlay->mode = *mode;
    overlay->initialized = 1;
    if (!changed) return;
    /* Omit spaces for unusually wide/high-rate modes to fit the character LCD. */
    char text[64];
    snprintf(text, sizeof(text), "%ux%u%s @ %u.%02uHz", mode->width, mode->height,
             mode->interlaced ? "i" : "", mode->centihz / 100, mode->centihz % 100);
    if (strlen(text) > LCD_WIDTH)
        snprintf(text, sizeof(text), "%ux%u%s@%u.%02uHz", mode->width, mode->height,
                 mode->interlaced ? "i" : "", mode->centihz / 100, mode->centihz % 100);
    make_row(overlay->text, text);
    overlay->until = now + 3;
}

static const char *display_status(struct display_overlay *overlay, double now,
                                  int volume_visible, const char *normal)
{
    if (now >= overlay->until) return normal;
    if (volume_visible) {
        /* Let volume finish first, then show the latest mode for three seconds. */
        overlay->until = now + 3;
        return normal;
    }
    return overlay->text;
}

static int read_cpu_temperature(char *path, size_t capacity, float *degrees)
{
    FILE *file;
    long millidegrees;
    if (!path[0]) {
        glob_t paths = {0};
        size_t i;
        if (glob("/sys/class/thermal/thermal_zone*/type", 0, NULL, &paths) == 0) {
            for (i = 0; i < paths.gl_pathc; ++i) {
                char type[64];
                file = fopen(paths.gl_pathv[i], "r");
                if (!file) continue;
                int valid = fscanf(file, "%63s", type) == 1;
                fclose(file);
                if (valid && strcmp(type, "x86_pkg_temp") == 0) {
                    snprintf(path, capacity, "%.*stemp",
                             (int)(strlen(paths.gl_pathv[i]) - 4), paths.gl_pathv[i]);
                    break;
                }
            }
        }
        globfree(&paths);
    }
    if (!path[0]) return -1;
    file = fopen(path, "r");
    if (!file) { path[0] = '\0'; return -1; }
    int valid = fscanf(file, "%ld", &millidegrees) == 1;
    fclose(file);
    if (!valid || millidegrees < -50000 || millidegrees > 200000) {
        path[0] = '\0';
        return -1;
    }
    *degrees = millidegrees / 1000.0f;
    return 0;
}

struct cpu_usage_reader {
    uint64_t counters[8];
    int initialized;
};

/* Aggregate CPU time: guest times are already included in user/nice.
 * Treat idle and I/O wait as idle, matching the usual CPU utilization view. */
static int cpu_usage_sample(struct cpu_usage_reader *reader, FILE *file,
                            unsigned int *percent)
{
    char line[512];
    uint64_t counters[8] = {0}, total = 0, idle = 0;
    int fields, i, valid = reader->initialized;
    if (!fgets(line, sizeof(line), file) || strncmp(line, "cpu ", 4) != 0)
        goto failed;
    fields = sscanf(line + 4,
                    "%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                    " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                    &counters[0], &counters[1], &counters[2], &counters[3],
                    &counters[4], &counters[5], &counters[6], &counters[7]);
    if (fields < 4) goto failed;
    for (i = 0; i < 8; ++i) {
        if (counters[i] < reader->counters[i]) valid = 0;
        uint64_t delta = counters[i] - reader->counters[i];
        if (UINT64_MAX - total < delta) valid = 0;
        total += delta;
        if (i == 3 || i == 4) idle += delta;
    }
    memcpy(reader->counters, counters, sizeof(counters));
    reader->initialized = 1;
    if (!valid || !total || idle > total) return -1;
    *percent = (unsigned int)(100.0 * (double)(total - idle) / (double)total + 0.5);
    return 0;
failed:
    reader->initialized = 0;
    return -1;
}

static int read_cpu_usage(struct cpu_usage_reader *reader, unsigned int *percent)
{
    FILE *file = fopen("/proc/stat", "r");
    if (!file) { reader->initialized = 0; return -1; }
    int result = cpu_usage_sample(reader, file, percent);
    fclose(file);
    return result;
}

static void sensor_row(char *text, size_t capacity, const char *cpu,
                       int gpu_valid, unsigned int gpu,
                       int usage_valid, unsigned int usage)
{
    if (gpu_valid)
        snprintf(text, capacity, "CPU %s GPU %u.0 C", cpu, gpu);
    else if (usage_valid)
        snprintf(text, capacity, "CPU %sC Load %u%%", cpu, usage);
    else
        snprintf(text, capacity, "CPU %sC Load --%%", cpu);
}

/* NVML's stable C ABI; runtime loading keeps NVIDIA optional and does not
 * require the CUDA toolkit to build this small application. */
typedef struct nvmlDevice_st *nvml_device;
struct gpu_reader {
    void *library;
    nvml_device device;
    int initialized;
    int (*init)(void);
    int (*shutdown)(void);
    int (*get_device)(unsigned int, nvml_device *);
    int (*get_temperature)(nvml_device, unsigned int, unsigned int *);
};

static void gpu_close(struct gpu_reader *gpu)
{
    if (gpu->initialized) gpu->shutdown();
    if (gpu->library) dlclose(gpu->library);
    memset(gpu, 0, sizeof(*gpu));
}

static int gpu_temperature(struct gpu_reader *gpu, unsigned int *temperature)
{
    if (!gpu->library) {
        gpu->library = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!gpu->library) return -1;
        gpu->init = dlsym(gpu->library, "nvmlInit_v2");
        gpu->shutdown = dlsym(gpu->library, "nvmlShutdown");
        gpu->get_device = dlsym(gpu->library, "nvmlDeviceGetHandleByIndex_v2");
        gpu->get_temperature = dlsym(gpu->library, "nvmlDeviceGetTemperature");
        if (!gpu->init || !gpu->shutdown || !gpu->get_device || !gpu->get_temperature)
            goto failed;
        if (gpu->init() != 0) goto failed;
        gpu->initialized = 1;
        if (gpu->get_device(0, &gpu->device) != 0) goto failed;
    }
    if (gpu->get_temperature(gpu->device, 0, temperature) == 0 && *temperature <= 200)
        return 0;
failed:
    gpu_close(gpu);
    return -1;
}

static unsigned int key_state(const unsigned char data[2])
{
    unsigned int state = 0;
    int i;
    for (i = 0; i < 2; ++i) {
        unsigned int key = data[i];
        if (key < KEY_COUNT && (keymap[key] || key == F1_KEY))
            state |= 1u << key;
    }
    return state;
}

/* Return the F1 release edge. Release old keys before pressing new keys;
 * repeated identical reports do not generate duplicate key-down events. */
static int apply_keys(struct panel_input *input, unsigned int *previous, unsigned int state)
{
    unsigned int released = *previous & ~state;
    unsigned int pressed = state & ~*previous;
    int key;
    if (input) {
        for (key = 1; key < KEY_COUNT; ++key)
            if (keymap[key] && (released & (1u << key)))
                panel_input_hold(input,keymap[key],0);
        for (key = 1; key < KEY_COUNT; ++key)
            if (keymap[key] && (pressed & (1u << key)))
                panel_input_hold(input,keymap[key],1);
    }
    *previous = state;
    return (released & (1u << F1_KEY)) != 0;
}

static DBusPendingCall *start_retroarch(DBusConnection *bus)
{
    DBusPendingCall *pending = NULL;
    const char *unit = "retroarch.service", *mode = "replace";
    DBusMessage *request = dbus_message_new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    if (!request) return NULL;
    if (!dbus_message_append_args(request, DBUS_TYPE_STRING, &unit,
                                 DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID) ||
        !dbus_connection_send_with_reply(bus, request, &pending, 2000))
        fprintf(stderr, "Cannot queue RetroArch start request\n");
    dbus_message_unref(request);
    return pending;
}

static void check_launch(DBusPendingCall **pending)
{
    if (*pending && dbus_pending_call_get_completed(*pending)) {
        DBusMessage *reply = dbus_pending_call_steal_reply(*pending);
        if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
            fprintf(stderr, "RetroArch start failed: %s\n", reply ?
                    dbus_message_get_error_name(reply) : "no reply");
        if (reply) dbus_message_unref(reply);
        dbus_pending_call_unref(*pending);
        *pending = NULL;
    }
}

int panel_run(usblcd_operations *lcd)
{
    struct gpu_reader gpu = {0};
    struct cpu_usage_reader usage_reader = {0};
    struct display_overlay display_overlay = {0};
    struct display_monitor display_monitor={.fd=-1};
    int display_pending = 0;
    char cpu_path[256] = "";
    char previous[2][LCD_WIDTH + 1] = {{0}};
    double next_temperature = 0, next_clock = 0, next_input = 0, next_bus = 0;
    double next_scroll = 0;
    size_t scroll_offset = 0;
    char date_text[64] = "Time unavailable";
    char temperature_text[64] = "CPU -- GPU -- C";
    char last_media[MEDIA_TITLE_SIZE] = "";
    enum media_source last_source = MEDIA_NONE;
    struct media_monitor *media = media_start();
    if (!media) fprintf(stderr, "Media monitor unavailable; showing date\n");
    struct panel_input input={.keyboard=-1,.mouse=-1};
    double input_ready=0;
    unsigned int physical = 0, sent = 0;
    DBusConnection *bus = NULL;
    DBusPendingCall *pending = NULL;
    struct ortek_decoder ortek = {0}, mouse_decoder = {0};
    struct ir_context ir = {0};
    double next_dongle_check=0;
    ortek_load_map();
    struct sigaction action = {0};
    int result = 0;
    stopping = 0;
    action.sa_handler = stop_panel;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    display_restore(DISPLAY_STATE_PATH, &display_overlay);

    while (!stopping) {
        double now = monotonic_seconds();
        if (now >= next_clock) {
            time_t wall = time(NULL);
            struct tm local;
            if (!localtime_r(&wall, &local) ||
                !strftime(date_text, sizeof(date_text), "%A %d %B %Y %H:%M", &local))
                strcpy(date_text, "Time unavailable");
            next_clock = now + 1;
        }
        if (now >= next_scroll) {
            char row[LCD_WIDTH + 1];
            char title[MEDIA_TITLE_SIZE];
            enum media_source source = media_snapshot(media, title, sizeof(title));
            if (source != last_source || strcmp(title, last_media) != 0) {
                scroll_offset = 0;
                last_source = source;
                snprintf(last_media, sizeof(last_media), "%s", title);
            }
            const char *text = source == MEDIA_NONE ? date_text : title;
            size_t cycle = strlen(text) + MARQUEE_GAP;
            marquee_row(row, text, scroll_offset);
            update_row(lcd, 0, previous[0], row);
            scroll_offset = (scroll_offset + 1) % cycle;
            next_scroll = now + MARQUEE_INTERVAL;
        }
        if (now >= next_temperature) {
            float cpu;
            unsigned int temperature = 0, usage = 0;
            char cpu_text[12] = "--";
            if (read_cpu_temperature(cpu_path, sizeof(cpu_path), &cpu) == 0)
                snprintf(cpu_text, sizeof(cpu_text), "%.1f", cpu);
            int gpu_valid = gpu_temperature(&gpu, &temperature) == 0;
            int usage_valid = read_cpu_usage(&usage_reader, &usage) == 0;
            sensor_row(temperature_text, sizeof(temperature_text), cpu_text,
                       gpu_valid, temperature, usage_valid, usage);
            /* Prime a baseline at startup, then show usage after one second.
             * Keep sampling with a GPU present so a later failure can fall back. */
            next_temperature = now + (usage_valid ? 5 : 1);
        }
        if(input.failed) {
            fprintf(stderr,"uinput write failed; recreating input devices\n");
            panel_input_close(&input);ir.input=NULL;sent=0;ir.mouse_sent=0;
            memset(&ir.mouse,0,sizeof(ir.mouse));next_input=now+2;
        }
        if(input.keyboard<0 && now>=next_input) {
            next_input=now+2;
            if(panel_input_open(&input)==0) {
                input_ready=now+0.3;
                fprintf(stderr,"uinput keyboard and mouse created\n");
            } else fprintf(stderr,"uinput unavailable: %s\n",strerror(errno));
        }
        ir.input=(input.keyboard>=0 && now>=input_ready)?&input:NULL;
        if(ir.input)apply_keys(ir.input,&sent,physical);
        struct display_mode mode;
        if(display_monitor_poll(&display_monitor,now,&mode)) {
            int save=!display_overlay.initialized || display_overlay.replay ||
                !display_modes_equal(&mode,&display_overlay.mode);
            display_observe(&display_overlay,&mode,now);
            if(save) {
                display_pending=now<display_overlay.until;
                display_save(DISPLAY_STATE_PATH,&mode,display_pending);
            }
        }
        struct media_playback playback;
        char status_row[LCD_WIDTH + 1];
        media_get_playback(media, &playback);
        media_status_row(&playback, status_row);
        update_row(lcd, 1, previous[1], display_status(&display_overlay, now,
                   playback.volume_visible, status_row[0] ? status_row : temperature_text));
        if (display_pending && now >= display_overlay.until) {
            display_save(DISPLAY_STATE_PATH, &display_overlay.mode, 0);
            display_pending = 0;
        }
        if (!bus && now >= next_bus) {
            DBusError error = DBUS_ERROR_INIT;
            bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
            if (bus) dbus_connection_set_exit_on_disconnect(bus, FALSE);
            else fprintf(stderr, "System bus unavailable: %s\n", error.message);
            dbus_error_free(&error);
            next_bus = now + 5;
        }
        if (bus) {
            dbus_connection_read_write_dispatch(bus, 0);
            check_launch(&pending);
            if (!dbus_connection_get_is_connected(bus)) {
                if (pending) { dbus_pending_call_cancel(pending); dbus_pending_call_unref(pending); pending = NULL; }
                dbus_connection_close(bus);
                dbus_connection_unref(bus);
                bus = NULL;
                next_bus = now + 5;
            }
        }
        if(now>=next_dongle_check) {
            ir.dongle=ortek_dongle_present(); next_dongle_check=now+1;
        }
        ortek_mouse_output(&ir,now);
        errno = 0;
        usblcd_event *event = picolcd_read_events_timeout(lcd, (ir.mouse.direction || ir.mouse_sent) ? 12 : 200);
        if (!event) {
            if (errno && errno != ETIMEDOUT && errno != EINTR) {
                fprintf(stderr, "Panel USB read failed: %s\n", strerror(errno));
                result = 1;
                stopping = 1;
            }
            continue;
        }
        if (event->type == 0 && event->length == 2) {
            unsigned int state = key_state(event->data);
            int launch = (physical & (1u << F1_KEY)) && !(state & (1u << F1_KEY));
            physical = state;
            if(ir.input)apply_keys(ir.input, &sent, state);
            if (launch && !pending) {
                /* Input remains available while Kodi's X server is replaced. */
                if(ir.input)apply_keys(ir.input,&sent,0);
                ortek_mouse_release(&ir);
                if (bus) pending = start_retroarch(bus);
                else fprintf(stderr, "Cannot start RetroArch: system bus unavailable\n");
            }
        } else if (event->type == 1) {
            double received_at=monotonic_seconds();
            ortek_feed(&ortek,event->data,event->length,received_at,ortek_received,&ir);
            ortek_mouse_feed(&mouse_decoder,event->data,event->length,received_at,ortek_mouse_received,&ir);
        }
        free(event->data);
        free(event);
    }
    ortek_mouse_release(&ir);
    panel_input_close(&input);
    display_monitor_close(&display_monitor);
    if (pending) { dbus_pending_call_cancel(pending); dbus_pending_call_unref(pending); }
    if (bus) { dbus_connection_close(bus); dbus_connection_unref(bus); }
    gpu_close(&gpu);
    media_stop(media);
    if (DISPLAY_STATE_PATH[0]) unlink(DISPLAY_STATE_PATH);
    return result;
}
