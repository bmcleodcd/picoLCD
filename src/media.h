#ifndef PICOLCD_MEDIA_H
#define PICOLCD_MEDIA_H
#include <stddef.h>
#define MEDIA_TITLE_SIZE 1024
enum media_source { MEDIA_NONE, MEDIA_KODI, MEDIA_RETROARCH };
struct media_monitor;
struct media_monitor *media_start(void);
enum media_source media_snapshot(struct media_monitor *monitor, char *title, size_t size);
void media_stop(struct media_monitor *monitor);
#endif
