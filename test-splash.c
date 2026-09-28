#include <assert.h>
#include "lib/splash.c"
static unsigned char memory[256], response[24];
static int response_size, writes, bad_ack, version_pending;
static int fake_write(void *handle, hid_params *request)
{
    (void)handle;
    unsigned char *packet = request->packet;
    unsigned int address = packet[1] | (packet[2] << 8), count = packet[3];
    assert(count && count <= 20 && address + count <= 256);
    assert(packet[0] == OUT_REPORT_INT_EE_WRITE || packet[0] == OUT_REPORT_INT_EE_READ);
    if (packet[0] == OUT_REPORT_INT_EE_WRITE) {
        assert(request->packetlen == count + 4);
        memcpy(memory + address, packet + 4, count);
        ++writes;
    } else assert(request->packetlen == 4);
    response[0] = IN_REPORT_INT_EE_DATA;
    memcpy(response + 1, packet + 1, 3);
    memcpy(response + 4, memory + address, count);
    if (packet[0] == OUT_REPORT_INT_EE_WRITE && bad_ack) { response[4] ^= 1; bad_ack = 0; }
    response_size = count + 4;
    return request->packetlen;
}
int usb_interrupt_read(usb_dev_handle *handle, int endpoint, char *data, int size, int timeout)
{
    (void)handle; (void)endpoint; (void)timeout;
    if (version_pending) {
        version_pending = 0;
        data[0] = HID_REPORT_GET_VERSION; data[1] = 1; data[2] = 50;
        return 3;
    }
    assert(size >= response_size);
    memcpy(data, response, response_size);
    return response_size;
}
int main(void)
{
    unsigned char image[SPLASH_SIZE], original[256], backup[256];
    FILE *file = fopen("cat-splash.txt", "r");
    assert(file && splash_parse(file, image) == 0); fclose(file);
    assert(image[0] == 2 && image[1] == 0);
    assert(memcmp(image + 5, "       /\\_/\\        ", 20) == 0);
    assert(memcmp(image + 26, "      ( o.o )       ", 20) == 0);
    assert(image[25] == 0 && image[46] == 0);
    for (int frame = 0; frame < 5; ++frame) {
        assert(image[frame * SLOT_SIZE + 4] == 0x80);
        assert(image[frame * SLOT_SIZE] == (frame == 0 ? 2 : 1));
    }
    assert(memcmp(image + 2 * SLOT_SIZE + 26, "      ( -.- )", 13) == 0);
    file = tmpfile(); assert(file);
    fputs(":splash\n00 05\n00 000\n00000000\n123456789012345678901\nline2\n", file);
    rewind(file); assert(splash_parse(file, image) < 0); fclose(file);
    file = tmpfile(); assert(file);
    fputs(":splash\n", file);
    int i;
    for (i = 0; i < 6; ++i) fputs("00 05\n00 000\n00000000\nline1\nline2\n", file);
    rewind(file); assert(splash_parse(file, image) < 0); fclose(file);
    hid_device device = {0};
    hid_operations hid = {.hiddev=&device, .interrupt_write=fake_write};
    usblcd_operations lcd = {.vendorid=0x04d8, .productid=2, .rows=1, .cols=19,
        .max_splashes=5, .max_data_len=24, .hid=&hid};
    for (i = 0; i < 256; ++i) memory[i] = original[i] = (unsigned char)i;
    version_pending = 1;
    assert(picolcd_eeprom_read(&lcd, backup) == 0 && !memcmp(backup, original, 256));
    assert(!version_pending);
    assert(picolcd_setsplash_checked(&lcd, "cat-splash.txt") == 0);
    assert(writes == 12); /* 11 complete chunks and a 15-byte final chunk. */
    assert(!memcmp(memory + 235, original + 235, 21)); /* Unrelated EEPROM untouched. */
    int old_writes = writes;
    assert(picolcd_setsplash_checked(&lcd, "cat-splash.txt") == 0 && writes == old_writes);
    assert(picolcd_eeprom_restore(&lcd, backup) == 0 && !memcmp(memory, original, 256));
    bad_ack = 1;
    assert(picolcd_setsplash_checked(&lcd, "cat-splash.txt") < 0);
    assert(!memcmp(memory, original, 256)); /* Failed write was rolled back. */
    puts("Splash layout, bounds, backup, verification and rollback tests passed");
    return 0;
}
