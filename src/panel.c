/* Minibox panel: bounded display updates, direct NVML and systemd D-Bus. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <dlfcn.h>
#include <dbus/dbus.h>
#include <xdo.h>
#include "panel.h"
#include "media.h"
#include "picolcd-common.h"
#include "rc5.h"

#define LCD_WIDTH 20
#define KEY_COUNT 16
#define F1_KEY 3
#define MARQUEE_INTERVAL 0.4
#define MARQUEE_GAP 3

static const char *const keymap[KEY_COUNT] = {
    NULL, "XF86AudioRaiseVolume", "XF86AudioLowerVolume", NULL,
    "XF86AudioPlay", "XF86AudioStop", NULL, "Escape",
    "Left", "Right", "Up", "Down", "Return", NULL, NULL, NULL
};
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
static int apply_keys(xdo_t *x, unsigned int *previous, unsigned int state)
{
    unsigned int released = *previous & ~state;
    unsigned int pressed = state & ~*previous;
    int key;
    if (x) {
        for (key = 1; key < KEY_COUNT; ++key)
            if (keymap[key] && (released & (1u << key)))
                xdo_send_keysequence_window_up(x, CURRENTWINDOW, keymap[key], 0);
        for (key = 1; key < KEY_COUNT; ++key)
            if (keymap[key] && (pressed & (1u << key)))
                xdo_send_keysequence_window_down(x, CURRENTWINDOW, keymap[key], 0);
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

/* Xlib cannot recover a dead connection safely. The service restarts on this
 * failure; ordinary missing-display connections are retried in the loop. */
static int x_connection_lost(Display *display)
{
    static const char message[] = "X11 connection lost; restarting panel connection\n";
    (void)display;
    ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)written;
    _exit(EXIT_FAILURE);
}

int panel_run(usblcd_operations *lcd)
{
    struct gpu_reader gpu = {0};
    char cpu_path[256] = "";
    char previous[2][LCD_WIDTH + 1] = {{0}};
    double next_temperature = 0, next_clock = 0, next_x = 0, next_bus = 0;
    double next_scroll = 0;
    size_t scroll_offset = 0;
    char date_text[64] = "Time unavailable";
    char temperature_text[64] = "CPU -- GPU -- C";
    char last_media[MEDIA_TITLE_SIZE] = "";
    enum media_source last_source = MEDIA_NONE;
    struct media_monitor *media = media_start();
    if (!media) fprintf(stderr, "Media monitor unavailable; showing date\n");
    xdo_t *x = NULL;
    unsigned int physical = 0, sent = 0;
    DBusConnection *bus = NULL;
    DBusPendingCall *pending = NULL;
    rc5decoder *rc5 = rc5_init();
    struct sigaction action = {0};
    int result = 0;
    stopping = 0;
    action.sa_handler = stop_panel;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    XSetIOErrorHandler(x_connection_lost);

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
            unsigned int temperature;
            char cpu_text[12] = "--", gpu_text[12] = "--";
            if (read_cpu_temperature(cpu_path, sizeof(cpu_path), &cpu) == 0)
                snprintf(cpu_text, sizeof(cpu_text), "%.1f", cpu);
            if (gpu_temperature(&gpu, &temperature) == 0)
                snprintf(gpu_text, sizeof(gpu_text), "%u.0", temperature);
            snprintf(temperature_text, sizeof(temperature_text), "CPU %s GPU %s C", cpu_text, gpu_text);
            next_temperature = now + 5;
        }
        struct media_playback playback;
        char status_row[LCD_WIDTH + 1];
        media_get_playback(media, &playback);
        media_status_row(&playback, status_row);
        update_row(lcd, 1, previous[1], status_row[0] ? status_row : temperature_text);
        if (!x && now >= next_x) {
            x = xdo_new(":0");
            next_x = now + 2;
            sent = 0;
            if (x) apply_keys(x, &sent, physical);
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
        errno = 0;
        usblcd_event *event = picolcd_read_events_timeout(lcd, 200);
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
            apply_keys(x, &sent, state);
            if (launch && !pending) {
                /* Close X before systemd stops Kodi's X server. */
                if (x) {
                    apply_keys(x, &sent, 0);
                    xdo_free(x);
                    x = NULL;
                }
                next_x = monotonic_seconds() + 2;
                if (bus) pending = start_retroarch(bus);
                else fprintf(stderr, "Cannot start RetroArch: system bus unavailable\n");
            }
        } else if (event->type == 1 && rc5) {
            rc5_decode(rc5, event->data, event->length);
        }
        free(event->data);
        free(event);
    }
    if (x) { apply_keys(x, &sent, 0); xdo_free(x); }
    if (pending) { dbus_pending_call_cancel(pending); dbus_pending_call_unref(pending); }
    if (bus) { dbus_connection_close(bus); dbus_connection_unref(bus); }
    gpu_close(&gpu);
    media_stop(media);
    if (rc5) rc5_close(rc5);
    return result;
}
