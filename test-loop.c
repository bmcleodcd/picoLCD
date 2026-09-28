#include <assert.h>
#define xdo_new test_xdo_new
#define media_start test_media_start
#define media_snapshot test_media_snapshot
#define media_stop test_media_stop
#define media_get_playback test_media_get_playback
#include "src/panel.c"
static int reads, connections, row_writes[2];
static int saw_kodi, saw_retroarch, saw_return_to_date;
static int saw_progress, saw_volume, saw_pause;
struct media_monitor *test_media_start(void) { return (struct media_monitor *)&reads; }
void test_media_stop(struct media_monitor *monitor) { (void)monitor; }
void test_media_get_playback(struct media_monitor *monitor, struct media_playback *playback)
{
    (void)monitor;
    memset(playback, 0, sizeof(*playback));
    if (reads >= 4 && reads < 10) {
        playback->active = playback->time_valid = 1;
        playback->elapsed = 12; playback->total = 240;
        playback->paused = reads >= 8;
        playback->volume_visible = reads >= 6 && reads < 8;
        playback->volume = 71;
    }
}
enum media_source test_media_snapshot(struct media_monitor *monitor, char *title, size_t size)
{
    (void)monitor;
    if (reads >= 4 && reads < 10) {
        snprintf(title, size, "Kodi movie with a long title");
        return MEDIA_KODI;
    }
    if (reads >= 10 && reads < 18) {
        snprintf(title, size, "RetroArch game with a long title");
        return MEDIA_RETROARCH;
    }
    title[0] = '\0';
    return MEDIA_NONE;
}
xdo_t *test_xdo_new(const char *display)
{
    (void)display;
    ++connections;
    return NULL; /* X unavailable throughout: panel must keep working and retry. */
}
usblcd_event *picolcd_read_events_timeout(usblcd_operations *lcd, int timeout_ms)
{
    (void)lcd;
    assert(timeout_ms == 200);
    usleep(timeout_ms * 1000);
    if (++reads == 31) stop_panel(SIGTERM);
    errno = ETIMEDOUT;
    return NULL;
}
static void capture(usblcd_operations *lcd, unsigned int row, unsigned int col, char *text)
{
    (void)lcd; (void)col;
    assert(row < 2 && strlen(text) == 20);
    if (row == 1) {
        if (reads >= 4 && reads < 6) { assert(strcmp(text, "   00:12 / 04:00    ") == 0); saw_progress = 1; }
        else if (reads >= 6 && reads < 8) { assert(strcmp(text, "     Volume 71%     ") == 0); saw_volume = 1; }
        else if (reads >= 8 && reads < 10) { assert(strcmp(text, "    Paused 00:12    ") == 0); saw_pause = 1; }
        else { while (*text == ' ') ++text; assert(strncmp(text, "CPU ", 4) == 0); }
    }
    if (row == 0 && reads >= 4 && reads < 10 && !saw_kodi) {
        assert(strncmp(text, "Kodi movie", 10) == 0); saw_kodi = 1;
    }
    if (row == 0 && reads >= 10 && reads < 18 && !saw_retroarch) {
        assert(strncmp(text, "RetroArch game", 14) == 0); saw_retroarch = 1;
    }
    if (row == 0 && reads >= 18 && !saw_return_to_date) {
        time_t now = time(NULL);
        struct tm local;
        char weekday[32];
        localtime_r(&now, &local);
        strftime(weekday, sizeof(weekday), "%A", &local);
        assert(strncmp(text, weekday, strlen(weekday)) == 0);
        saw_return_to_date = 1;
    }
    ++row_writes[row];
}
int main(void)
{
    usblcd_operations lcd = {0};
    lcd.settext = capture;
    assert(panel_run(&lcd) == 0);
    assert(row_writes[0] >= 10 && row_writes[0] <= 17);
    assert(row_writes[1] >= 5 && row_writes[1] <= 6);
    assert(connections >= 3 && connections <= 4);
    assert(saw_kodi && saw_retroarch && saw_return_to_date);
    assert(saw_progress && saw_volume && saw_pause);
    dbus_shutdown();
    printf("Loop test passed: %d timeouts, %d X retries, %d/%d row writes\n",
           reads, connections, row_writes[0], row_writes[1]);
    return 0;
}
