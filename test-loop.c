#include <assert.h>
#define xdo_new test_xdo_new
#include "src/panel.c"
static int reads, connections, row_writes[2];
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
    ++row_writes[row];
}
int main(void)
{
    usblcd_operations lcd = {0};
    lcd.settext = capture;
    assert(panel_run(&lcd) == 0);
    assert(row_writes[0] >= 1 && row_writes[0] <= 2);
    assert(row_writes[1] >= 1 && row_writes[1] <= 2);
    assert(connections >= 3 && connections <= 4);
    dbus_shutdown();
    printf("Loop test passed: %d timeouts, %d X retries, %d/%d row writes\n",
           reads, connections, row_writes[0], row_writes[1]);
    return 0;
}
