/* Checked picoLCD 20x2 internal EEPROM access and splash serialization. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "picolcd-common.h"

#define EEPROM_SIZE 256
#define SLOT_SIZE 47 /* timeout(2), jump, repeat, LEDs, two 20-byte NUL-ended rows */
#define SLOT_COUNT 5
#define SPLASH_SIZE (SLOT_SIZE * SLOT_COUNT)

static int supported(usblcd_operations *lcd)
{
    return lcd && lcd->vendorid == 0x04d8 && lcd->productid == 0x0002 &&
        lcd->rows == 1 && lcd->cols == 19 && lcd->max_splashes == 5;
}

static long long splash_milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int eeprom_transfer(usblcd_operations *lcd, unsigned int address,
                           unsigned char *data, unsigned int length, int writing)
{
    unsigned char packet[24] = {0}, reply[24];
    if (!supported(lcd) || !length || length > 20 || address + length > EEPROM_SIZE) return -1;
    packet[0] = writing ? OUT_REPORT_INT_EE_WRITE : OUT_REPORT_INT_EE_READ;
    packet[1] = address & 255;
    packet[2] = address >> 8;
    packet[3] = length;
    if (writing) memcpy(packet + 4, data, length);
    hid_params request = { .endpoint = USB_ENDPOINT_OUT + 1,
        .packetlen = writing ? length + 4 : 4, .timeout = 1000, .packet = packet };
    int result = lcd->hid->interrupt_write(lcd->hid->hiddev->handle, &request);
    if (result != (int)request.packetlen) {
        fprintf(stderr, "EEPROM %s at %u: USB write returned %d (expected %u)\n",
                writing ? "write" : "read", address, result, request.packetlen);
        return -1;
    }
    long long deadline = splash_milliseconds() + 1500;
    while (splash_milliseconds() < deadline) {
        result = usb_interrupt_read(lcd->hid->hiddev->handle, USB_ENDPOINT_IN + 1,
                                    (char *)reply, sizeof(reply), 100);
        if (result == -ETIMEDOUT || result == -EINTR) continue;
        if (result <= 0) {
            fprintf(stderr, "EEPROM reply at %u: USB read returned %d\n", address, result);
            return -1;
        }
        /* init() sends GET_VERSION; its response may precede this request's
         * reply. Key/IR events can arrive at any time too. */
        if (reply[0] == IN_REPORT_KEY_STATE || reply[0] == IN_REPORT_IR_DATA ||
            reply[0] == HID_REPORT_GET_VERSION) continue;
        if (result < (int)length + 4 || reply[0] != IN_REPORT_INT_EE_DATA ||
            reply[1] != packet[1] || reply[2] != packet[2] || reply[3] != length) {
            fprintf(stderr, "Unexpected EEPROM reply at %u (%d bytes):", address, result);
            int i;
            for (i = 0; i < result; ++i) fprintf(stderr, " %02x", reply[i]);
            fputc('\n', stderr);
            return -1;
        }
        if (writing) {
            if (memcmp(reply + 4, data, length) == 0) return 0;
            fprintf(stderr, "EEPROM write acknowledgement differs at %u\n", address);
            return -1;
        }
        memcpy(data, reply + 4, length);
        return 0;
    }
    fprintf(stderr, "Timed out waiting for EEPROM reply at %u\n", address);
    return -1;
}

int picolcd_eeprom_read(usblcd_operations *lcd, unsigned char data[256])
{
    unsigned int offset;
    for (offset = 0; offset < EEPROM_SIZE; offset += 20) {
        unsigned int count = EEPROM_SIZE - offset;
        if (count > 20) count = 20;
        int attempt;
        for (attempt = 0; attempt < 3; ++attempt)
            if (eeprom_transfer(lcd, offset, data + offset, count, 0) == 0) break;
        if (attempt == 3) return -1;
    }
    return 0;
}

static int write_image(usblcd_operations *lcd, const unsigned char *data, unsigned int size)
{
    unsigned int offset;
    unsigned char check[256];
    for (offset = 0; offset < size; offset += 20) {
        unsigned char chunk[20];
        unsigned int count = size - offset;
        if (count > 20) count = 20;
        memcpy(chunk, data + offset, count);
        if (eeprom_transfer(lcd, offset, chunk, count, 1) < 0) return -1;
    }
    if (picolcd_eeprom_read(lcd, check) < 0 || memcmp(data, check, size)) return -1;
    return 0;
}

int picolcd_eeprom_restore(usblcd_operations *lcd, const unsigned char data[256])
{
    return write_image(lcd, data, EEPROM_SIZE);
}

/* Return 1 for a complete line, 0 for EOF, -1 for I/O/overlong input. */
static int splash_line(FILE *file, char line[256], int skip_comments)
{
    do {
        if (!fgets(line, 256, file)) return ferror(file) ? -1 : 0;
        size_t size = strlen(line);
        if (size == 255 && line[size - 1] != '\n') return -1;
        line[strcspn(line, "\r\n")] = '\0';
    } while (skip_comments && (line[0] == '#' || !line[0]));
    return 1;
}

static int splash_parse(FILE *file, unsigned char image[SPLASH_SIZE])
{
    char line[256], extra;
    unsigned int slot, row;
    memset(image, 0, SPLASH_SIZE);
    for (slot = 0; slot < SLOT_COUNT; ++slot)
        for (row = 0; row < 2; ++row) memset(image + slot * SLOT_SIZE + 5 + row * 21, ' ', 20);
    if (splash_line(file, line, 1) != 1 || strcmp(line, ":splash")) return -1;
    slot = 0;
    int result;
    while ((result = splash_line(file, line, 1)) == 1) {
        int minutes, seconds, jump, repeat;
        if (slot >= SLOT_COUNT || sscanf(line, "%d %d %c", &minutes, &seconds, &extra) != 2 ||
            minutes < -1 || minutes > 59 || seconds < -1 || seconds > 59) return -1;
        unsigned int timeout = minutes < 0 || seconds < 0 ? 65535 : minutes * 60 + seconds;
        unsigned char *record = image + slot * SLOT_SIZE;
        record[0] = timeout & 255; record[1] = timeout >> 8;
        if (splash_line(file, line, 0) != 1 || sscanf(line, "%d %d %c", &jump, &repeat, &extra) != 2 ||
            jump < 0 || jump > 10 || repeat < 0 || repeat > 255) return -1;
        record[2] = jump; record[3] = repeat;
        if (splash_line(file, line, 0) != 1 || strlen(line) != 8) return -1;
        unsigned int bit;
        for (bit = 0; bit < 8; ++bit) {
            if (line[bit] != '0' && line[bit] != '1') return -1;
            record[4] = (record[4] << 1) | (line[bit] == '1');
        }
        for (row = 0; row < 2; ++row) {
            if (splash_line(file, line, 0) != 1 || strlen(line) > 20) return -1;
            memcpy(record + 5 + row * 21, line, strlen(line));
        }
        ++slot;
    }
    return result == 0 && slot ? 0 : -1;
}

int picolcd_setsplash_checked(usblcd_operations *lcd, const char *filename)
{
    unsigned char image[SPLASH_SIZE], original[EEPROM_SIZE];
    if (!supported(lcd)) return -1;
    FILE *file = fopen(filename, "r");
    if (!file) return -1;
    int result = splash_parse(file, image);
    fclose(file);
    if (result < 0) { fprintf(stderr, "Invalid splash file; EEPROM unchanged\n"); return -1; }
    if (picolcd_eeprom_read(lcd, original) < 0) {
        fprintf(stderr, "Cannot read original EEPROM; no writes attempted\n"); return -1;
    }
    if (!memcmp(original, image, SPLASH_SIZE)) {
        puts("Splash already matches; no EEPROM writes needed"); return 0;
    }
    if (write_image(lcd, image, SPLASH_SIZE) < 0) {
        fprintf(stderr, "Splash write failed; restoring original splash\n");
        if (write_image(lcd, original, SPLASH_SIZE) < 0)
            fprintf(stderr, "Restore failed; retain your EEPROM backup for recovery\n");
        return -1;
    }
    puts("Splash EEPROM written and read-back verified");
    return 0;
}

void picolcd_setsplash(usblcd_operations *lcd, char *filename)
{
    if (picolcd_setsplash_checked(lcd, filename) < 0)
        fprintf(stderr, "Splash update failed\n");
}
