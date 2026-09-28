#include <assert.h>
#include "src/panel.c"

static char actions[128];
static unsigned int actions_used;
int xdo_send_keysequence_window_up(const xdo_t *x, Window w, const char *key, useconds_t delay)
{
    (void)x; (void)w; (void)delay;
    actions_used += snprintf(actions + actions_used, sizeof(actions) - actions_used, "U%s;", key);
    return 0;
}
int xdo_send_keysequence_window_down(const xdo_t *x, Window w, const char *key, useconds_t delay)
{
    (void)x; (void)w; (void)delay;
    actions_used += snprintf(actions + actions_used, sizeof(actions) - actions_used, "D%s;", key);
    return 0;
}
static int writes;
static void capture_row(usblcd_operations *lcd, unsigned int row, unsigned int col, char *text)
{
    (void)lcd; (void)row; (void)col;
    assert(strlen(text) == 20);
    ++writes;
}
int main(int argc, char **argv)
{
    char row[21], previous[21] = {0};
    unsigned char data[2];
    unsigned int state = 0;
    unsigned int a, b;
    usblcd_operations lcd = {0};
    xdo_t fake_x = {0};
    lcd.settext = capture_row;
    make_row(row, "1234567890123456789012345");
    assert(strcmp(row, "12345678901234567890") == 0);
    make_row(row, "CPU -- GPU -- C");
    assert(strlen(row) == 20 && row[19] == ' ');
    update_row(&lcd, 0, previous, "hello");
    update_row(&lcd, 0, previous, "hello");
    assert(writes == 1);
    update_row(&lcd, 0, previous, "hi");
    assert(writes == 2 && previous[2] == ' ');
    /* Every possible USB key pair, including malformed IDs. */
    for (a = 0; a < 256; ++a) for (b = 0; b < 256; ++b) {
        data[0] = a; data[1] = b;
        assert((key_state(data) & ~0x1fbEu) == 0);
    }
    data[0] = 8; data[1] = 9;
    assert(key_state(data) == ((1u << 8) | (1u << 9)));
    apply_keys(&fake_x, &state, 1u << 8);
    apply_keys(&fake_x, &state, 1u << 8);
    apply_keys(&fake_x, &state, 1u << 9);
    apply_keys(&fake_x, &state, 0);
    assert(strcmp(actions, "DLeft;ULeft;DRight;URight;") == 0);
    assert(!apply_keys(NULL, &state, 1u << F1_KEY));
    assert(apply_keys(NULL, &state, 0));
    assert(!apply_keys(NULL, &state, 0));
    {
        char missing[256] = "/dev/null";
        float cpu = 99;
        assert(read_cpu_temperature(missing, sizeof(missing), &cpu) == -1);
        assert(cpu == 99 && missing[0] == '\0');
    }
    if (argc > 1 && strcmp(argv[1], "--sensors") == 0) {
        struct gpu_reader gpu = {0};
        char path[256] = "";
        float cpu;
        unsigned int temperature;
        assert(read_cpu_temperature(path, sizeof(path), &cpu) == 0);
        assert(gpu_temperature(&gpu, &temperature) == 0);
        printf("Live sensors: CPU %.1f C (%s), GPU %u C\n", cpu, path, temperature);
        gpu_close(&gpu);
    }
    puts("Panel tests passed");
    return 0;
}
