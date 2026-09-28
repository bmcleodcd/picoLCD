#include <assert.h>
#define xdo_new test_xdo_new
#define media_start test_media_start
#define media_snapshot test_media_snapshot
#define media_stop test_media_stop
#include "src/panel.c"
static int reads, connections, row_writes[2];
static int saw_kodi, saw_retroarch, saw_return_to_date;
struct media_monitor *test_media_start(void) { return (struct media_monitor *)&reads; }
void test_media_stop(struct media_monitor *monitor) { (void)monitor; }
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
    if (row == 1) assert(strncmp(text, "CPU ", 4) == 0);
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
    assert(row_writes[1] >= 1 && row_writes[1] <= 2);
    assert(connections >= 3 && connections <= 4);
    assert(saw_kodi && saw_retroarch && saw_return_to_date);
    dbus_shutdown();
    printf("Loop test passed: %d timeouts, %d X retries, %d/%d row writes\n",
           reads, connections, row_writes[0], row_writes[1]);
    return 0;
}
