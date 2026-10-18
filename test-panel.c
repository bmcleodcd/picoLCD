/*
 * Changes by bmcleodcd: 2026-09-28, 2026-10-09, 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#include <assert.h>
#include "src/panel.c"

static char actions[128];
static unsigned int actions_used;
int panel_input_hold(struct panel_input *input,const char *key,int down)
{
 (void)input;
 actions_used+=snprintf(actions+actions_used,sizeof(actions)-actions_used,"%c%s;",down?'D':'U',key);
 return 0;
}
static int writes;
static int sample_usage(struct cpu_usage_reader *reader, const char *text,
                         unsigned int *percent)
{
    FILE *file = tmpfile();
    assert(file);
    assert(fputs(text, file) >= 0);
    rewind(file);
    int result = cpu_usage_sample(reader, file, percent);
    fclose(file);
    return result;
}
static void capture_row(usblcd_operations *lcd, unsigned int row, unsigned int col, char *text)
{
    (void)lcd; (void)row; (void)col;
    assert(strlen(text) == 20);
    ++writes;
}
int main(int argc, char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--display")) {
        struct display_monitor monitor={.fd=-1};struct display_mode mode;
        for(int i=0;i<30;i++) {
            if(display_monitor_poll(&monitor,monotonic_seconds(),&mode)) {
                printf("Live primary output: %ux%u%s @ %u.%02uHz\n",mode.width,mode.height,
                       mode.interlaced?"i":"",mode.centihz/100,mode.centihz%100);
                display_monitor_close(&monitor);return 0;
            }
            usleep(100000);
        }
        display_monitor_close(&monitor);return 1;
    }
    char row[21], previous[21] = {0};
    unsigned char data[2];
    unsigned int state = 0;
    unsigned int a, b;
    usblcd_operations lcd = {0};
    struct panel_input fake_input = {0};
    lcd.settext = capture_row;
    {
        struct cpu_usage_reader reader = {0};
        unsigned int usage = 999;
        char sensors[64];
        assert(sample_usage(&reader, "cpu 100 20 30 400 50 6 7 8 99 99\n", &usage) == -1);
        assert(usage == 999);
        /* 20 busy ticks, 80 idle/I/O-wait ticks; ignore guest columns. */
        assert(sample_usage(&reader, "cpu 110 22 33 470 60 7 9 10 199 199\n", &usage) == 0);
        assert(usage == 20);
        assert(sample_usage(&reader, "cpu 110 22 33 570 60 7 9 10\n", &usage) == 0 && usage == 0);
        assert(sample_usage(&reader, "cpu 210 22 33 570 60 7 9 10\n", &usage) == 0 && usage == 100);
        assert(sample_usage(&reader, "cpu 210 22 33 570 60 7 9 10\n", &usage) == -1);
        assert(sample_usage(&reader, "cpu 1 0 0 1\n", &usage) == -1);
        assert(sample_usage(&reader, "cpu 2 0 0 2\n", &usage) == 0 && usage == 50);
        assert(sample_usage(&reader, "cpu broken\n", &usage) == -1 && !reader.initialized);
        assert(sample_usage(&reader, "cpu0 1 2 3 4\n", &usage) == -1);
        assert(sample_usage(&reader, "cpu 100 0 0 100\n", &usage) == -1);
        assert(sample_usage(&reader, "cpu 101 0 0 109\n", &usage) == 0 && usage == 10);
        sensor_row(sensors, sizeof(sensors), "50.0", 1, 60, 1, 20);
        assert(strcmp(sensors, "CPU 50.0 GPU 60.0 C") == 0);
        sensor_row(sensors, sizeof(sensors), "50.0", 0, 0, 1, 20);
        assert(strcmp(sensors, "CPU 50.0C Load 20%") == 0);
        sensor_row(sensors, sizeof(sensors), "100.0", 0, 0, 1, 100);
        assert(strcmp(sensors, "CPU 100.0C Load 100%") == 0 && strlen(sensors) <= 20);
        sensor_row(sensors, sizeof(sensors), "--", 0, 0, 0, 0);
        assert(strcmp(sensors, "CPU --C Load --%") == 0);
        sensor_row(sensors, sizeof(sensors), "50.0", 1, 60, 1, 20);
        assert(strcmp(sensors, "CPU 50.0 GPU 60.0 C") == 0);
    }
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
    apply_keys(&fake_input, &state, 1u << 8);
    apply_keys(&fake_input, &state, 1u << 8);
    apply_keys(&fake_input, &state, 1u << 9);
    apply_keys(&fake_input, &state, 0);
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
    if (argc > 1 && (strcmp(argv[1], "--sensors") == 0 ||
                     strcmp(argv[1], "--sensors-no-gpu") == 0)) {
        struct gpu_reader gpu = {0};
        char path[256] = "";
        float cpu;
        struct cpu_usage_reader usage_reader = {0};
        unsigned int usage;
        unsigned int temperature = 201;
        assert(read_cpu_temperature(path, sizeof(path), &cpu) == 0);
        assert(read_cpu_usage(&usage_reader, &usage) == -1);
        usleep(200000);
        assert(read_cpu_usage(&usage_reader, &usage) == 0 && usage <= 100);
        printf("Live CPU usage: %u%%\n", usage);
        if (strcmp(argv[1], "--sensors-no-gpu") == 0) {
            /* Intel-only hosts must keep the unavailable reading unavailable,
             * including subsequent retries, rather than inventing a value. */
            assert(gpu_temperature(&gpu, &temperature) == -1);
            assert(temperature == 201);
            assert(gpu_temperature(&gpu, &temperature) == -1);
            assert(temperature == 201);
            printf("Live sensors: CPU %.1f C (%s), GPU unavailable as expected\n", cpu, path);
        } else {
            assert(gpu_temperature(&gpu, &temperature) == 0);
            printf("Live sensors: CPU %.1f C (%s), GPU %u C\n", cpu, path, temperature);
        }
        gpu_close(&gpu);
        assert(gpu.library == NULL && gpu.initialized == 0);
    }
    puts("Panel tests passed");
    return 0;
}
