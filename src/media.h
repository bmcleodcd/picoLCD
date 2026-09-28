#ifndef PICOLCD_MEDIA_H
#define PICOLCD_MEDIA_H
#include <stddef.h>
#define MEDIA_TITLE_SIZE 1024
enum media_source { MEDIA_NONE, MEDIA_KODI, MEDIA_RETROARCH };
struct media_monitor;
struct media_playback {
    int active, time_valid, paused, live;
    int elapsed, total;
    int volume_visible, volume, muted;
};
struct media_monitor *media_start(void);
enum media_source media_snapshot(struct media_monitor *monitor, char *title, size_t size);
void media_get_playback(struct media_monitor *monitor, struct media_playback *playback);
void media_status_row(const struct media_playback *playback, char row[21]);
void media_stop(struct media_monitor *monitor);
#endif
