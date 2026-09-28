#include <assert.h>
#include <errno.h>
#include "lib/picolcd-common.c"
static int read_result;
static unsigned char input[256];
int usb_interrupt_read(usb_dev_handle *handle, int endpoint, char *bytes, int size, int timeout)
{
    (void)handle; (void)endpoint; (void)size;
    assert(timeout == 200 || timeout == 10000);
    if (read_result > 0) memcpy(bytes, input, read_result);
    return read_result;
}
int main(void)
{
    hid_device device = {0};
    hid_operations hid = {0};
    usblcd_operations lcd = {0};
    usblcd_event *event;
    int i;
    hid.hiddev = &device; lcd.hid = &hid; lcd.max_data_len = 24;
    read_result = -ETIMEDOUT;
    for (i = 0; i < 100000; ++i) {
        assert(!picolcd_read_events_timeout(&lcd, 200));
        assert(errno == ETIMEDOUT);
    }
    read_result = -ENODEV;
    assert(!picolcd_read_events_timeout(&lcd, 200) && errno == ENODEV);
    read_result = 1; input[0] = IN_REPORT_KEY_STATE;
    assert(!picolcd_read_events_timeout(&lcd, 200));
    read_result = 3; input[1] = 8; input[2] = 9;
    event = picolcd_read_events(&lcd);
    assert(event && event->type == 0 && event->length == 2 && event->data[1] == 9);
    free(event->data); free(event);
    input[0] = IN_REPORT_IR_DATA; input[1] = 255;
    assert(!picolcd_read_events_timeout(&lcd, 200));
    input[1] = 1; input[2] = 42;
    event = picolcd_read_events_timeout(&lcd, 200);
    assert(event && event->type == 1 && event->length == 1 && event->data[0] == 42);
    free(event->data); free(event);
    puts("USB timeout and malformed-packet tests passed");
    return 0;
}
