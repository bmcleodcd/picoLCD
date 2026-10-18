/*
 * Changes by bmcleodcd: 2026-09-28.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#include <assert.h>
#include "lib/rc5.c"
int main(void)
{
    rc5decoder *decoder = rc5_init();
    unsigned char invalid[2] = {0, 0};
    struct timespec start = {10, 900000000}, end = {11, 100000000};
    struct timespec elapsed = rc5_get_delay(start, end);
    int i;
    assert(elapsed.tv_sec == 0 && elapsed.tv_nsec == 200000000);
    assert(decoder);
    for (i = 0; i < 20; ++i) {
        rc5_decode(decoder, invalid, 1);
        rc5_decode(decoder, invalid, 2);
        assert(decoder->nbits == 0);
    }
    rc5_close(decoder);
    puts("RC5 decoder lifetime tests passed");
    return 0;
}
