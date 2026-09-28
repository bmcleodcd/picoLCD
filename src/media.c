/* Local, read-only status queries. All socket waits run outside the USB loop. */
#include "media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <json-c/json.h>

#define KODI_PORT 9090
#define RETROARCH_PORT 55355
#define QUERY_TIMEOUT_MS 500
#define MAX_JSON_BYTES 65536

struct kodi_status {
    char title[MEDIA_TITLE_SIZE];
    double elapsed, total;
    int time_valid, speed, live;
    int volume_known, volume, muted;
    long long sampled_at, volume_until;
};

struct media_monitor {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    int stop;
    enum media_source source;
    char title[MEDIA_TITLE_SIZE];
    struct kodi_status kodi;
};

static long long milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int wait_socket(int fd, short events, long long deadline)
{
    struct pollfd item = {fd, events, 0};
    for (;;) {
        long long remaining = deadline - milliseconds();
        if (remaining <= 0) return -1;
        int result = poll(&item, 1, (int)remaining);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return -1;
        return (item.revents & events) ? 0 : -1;
    }
}

static int local_socket(int type, unsigned short port, long long deadline)
{
    struct sockaddr_in address = {0};
    int fd = socket(AF_INET, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (errno != EINPROGRESS || wait_socket(fd, POLLOUT, deadline) < 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static int send_request(int fd, const char *text, long long deadline)
{
    size_t length = strlen(text), sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, text + sent, length - sent, MSG_NOSIGNAL);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            wait_socket(fd, POLLOUT, deadline) == 0) continue;
        return -1;
    }
    return 0;
}

/* JSON-RPC over TCP has no message delimiter. json-c handles fragmented and
 * concatenated documents; unsolicited notifications are skipped by request ID. */
static json_object *rpc(int fd, const char *request, int id)
{
    long long deadline = milliseconds() + QUERY_TIMEOUT_MS;
    struct json_tokener *parser = json_tokener_new();
    json_object *reply = NULL;
    size_t total = 0;
    if (!parser) return NULL;
    if (send_request(fd, request, deadline) < 0) goto done;
    while (total < MAX_JSON_BYTES && wait_socket(fd, POLLIN, deadline) == 0) {
        char buffer[4096];
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) break;
        total += (size_t)n;
        size_t offset = 0;
        while (offset < (size_t)n) {
            json_object *document = json_tokener_parse_ex(parser, buffer + offset, (int)(n - offset));
            enum json_tokener_error error = json_tokener_get_error(parser);
            size_t consumed = json_tokener_get_parse_end(parser);
            offset += consumed;
            if (error == json_tokener_continue) break;
            if (error != json_tokener_success || !consumed) {
                if (document) json_object_put(document);
                goto done;
            }
            json_object *response_id;
            if (json_object_object_get_ex(document, "id", &response_id) &&
                json_object_is_type(response_id, json_type_int) &&
                json_object_get_int(response_id) == id) {
                reply = document;
                goto done;
            }
            json_object_put(document);
            json_tokener_reset(parser);
        }
    }
done:
    json_tokener_free(parser);
    return reply;
}

/* The character LCD is not UTF-8. Preserve ASCII and substitute one '?' per
 * unsupported code point, rather than scrolling individual UTF-8 bytes. */
static void lcd_title(char *out, size_t capacity, const char *input)
{
    size_t used = 0;
    if (!capacity) return;
    while (*input && used + 1 < capacity) {
        unsigned char ch = (unsigned char)*input++;
        if (ch >= 0x80 && ch < 0xc0) continue;
        out[used++] = ch >= 0x80 ? '?' : (ch < 32 || ch == 127 ? ' ' : (char)ch);
    }
    while (used && out[used - 1] == ' ') --used;
    out[used] = '\0';
}

static const char *string_field(json_object *object, const char *name)
{
    json_object *value;
    if (json_object_object_get_ex(object, name, &value) &&
        json_object_is_type(value, json_type_string))
        return json_object_get_string(value);
    return "";
}

static int kodi_player(json_object *reply)
{
    json_object *players;
    int audio = -1;
    if (!reply || !json_object_object_get_ex(reply, "result", &players) ||
        !json_object_is_type(players, json_type_array)) return -1;
    size_t i;
    for (i = 0; i < json_object_array_length(players); ++i) {
        json_object *player = json_object_array_get_idx(players, i), *id;
        if (!json_object_object_get_ex(player, "playerid", &id) ||
            !json_object_is_type(id, json_type_int)) continue;
        int number = json_object_get_int(id);
        if (number < 0 || number > 2) continue;
        const char *type = string_field(player, "type");
        if (strcmp(type, "video") == 0) return number;
        if (strcmp(type, "audio") == 0) audio = number;
    }
    return audio;
}

static void kodi_item(json_object *reply, char *title, size_t capacity)
{
    json_object *result, *item;
    title[0] = '\0';
    if (!reply || !json_object_object_get_ex(reply, "result", &result) ||
        !json_object_object_get_ex(result, "item", &item)) return;
    const char *names[] = {"title", "label", "file"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        lcd_title(title, capacity, string_field(item, names[i]));
        if (title[0]) break;
    }
    char detail[MEDIA_TITLE_SIZE * 2];
    const char *show = string_field(item, "showtitle");
    json_object *season, *episode, *artists, *year;
    if (strcmp(string_field(item, "type"), "movie") == 0) {
        if (title[0] && json_object_object_get_ex(item, "year", &year) &&
            json_object_is_type(year, json_type_int) && json_object_get_int(year) > 0) {
            snprintf(detail, sizeof(detail), "%s (%d)", title, json_object_get_int(year));
            lcd_title(title, capacity, detail);
        }
    } else if (*show && json_object_object_get_ex(item, "season", &season) &&
        json_object_object_get_ex(item, "episode", &episode) &&
        json_object_is_type(season, json_type_int) && json_object_is_type(episode, json_type_int) &&
        json_object_get_int(season) >= 0 && json_object_get_int(episode) >= 0) {
        snprintf(detail, sizeof(detail), "%s S%02dE%02d%s%s", show,
                 json_object_get_int(season), json_object_get_int(episode),
                 title[0] ? " - " : "", title);
        lcd_title(title, capacity, detail);
    } else if (json_object_object_get_ex(item, "artist", &artists) &&
               json_object_is_type(artists, json_type_array) && json_object_array_length(artists)) {
        json_object *artist = json_object_array_get_idx(artists, 0);
        if (json_object_is_type(artist, json_type_string) && *json_object_get_string(artist)) {
            snprintf(detail, sizeof(detail), "%s%s%s", json_object_get_string(artist),
                     title[0] ? " - " : "", title);
            lcd_title(title, capacity, detail);
        }
    }
}

static double kodi_seconds(json_object *time)
{
    const char *fields[] = {"hours", "minutes", "seconds", "milliseconds"};
    const double weights[] = {3600, 60, 1, 0.001};
    double total = 0;
    size_t i;
    for (i = 0; i < 4; ++i) {
        json_object *value;
        if (!json_object_object_get_ex(time, fields[i], &value) ||
            !json_object_is_type(value, json_type_int) || json_object_get_int(value) < 0) return -1;
        total += json_object_get_int(value) * weights[i];
    }
    return total <= 359999 ? total : -1; /* bounded 99-hour display */
}

static void kodi_properties(json_object *reply, struct kodi_status *status)
{
    json_object *result, *elapsed, *total, *speed, *live;
    status->time_valid = 0;
    if (!reply || !json_object_object_get_ex(reply, "result", &result) ||
        !json_object_object_get_ex(result, "time", &elapsed) ||
        !json_object_object_get_ex(result, "totaltime", &total) ||
        !json_object_object_get_ex(result, "speed", &speed) ||
        !json_object_is_type(speed, json_type_int)) return;
    status->elapsed = kodi_seconds(elapsed);
    status->total = kodi_seconds(total);
    status->speed = json_object_get_int(speed);
    status->live = json_object_object_get_ex(result, "live", &live) && json_object_get_boolean(live);
    status->sampled_at = milliseconds();
    status->time_valid = status->elapsed >= 0 && status->total >= 0;
}

static void kodi_volume(json_object *data, struct kodi_status *status, long long now)
{
    json_object *volume, *muted;
    if (!data || !json_object_object_get_ex(data, "volume", &volume) ||
        !json_object_object_get_ex(data, "muted", &muted) ||
        !json_object_is_type(volume, json_type_int) || !json_object_is_type(muted, json_type_boolean)) return;
    int level = json_object_get_int(volume), silent = json_object_get_boolean(muted);
    if (level < 0 || level > 100) return;
    if (status->volume_known && (level != status->volume || silent != status->muted))
        status->volume_until = now + 3000;
    status->volume_known = 1;
    status->volume = level;
    status->muted = silent;
}

static void kodi_query(unsigned short port, struct kodi_status *status)
{
    status->title[0] = '\0';
    status->time_valid = 0;
    int fd = local_socket(SOCK_STREAM, port, milliseconds() + QUERY_TIMEOUT_MS);
    if (fd < 0) return;
    json_object *reply = rpc(fd,
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"Application.GetProperties\","
        "\"params\":{\"properties\":[\"volume\",\"muted\"]}}", 3);
    json_object *result;
    if (reply && json_object_object_get_ex(reply, "result", &result))
        kodi_volume(result, status, milliseconds());
    if (reply) json_object_put(reply);
    reply = rpc(fd,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"Player.GetActivePlayers\"}", 1);
    int player = kodi_player(reply);
    if (reply) json_object_put(reply);
    if (player >= 0) {
        char request[512];
        snprintf(request, sizeof(request),
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"Player.GetItem\","
            "\"params\":{\"playerid\":%d,\"properties\":[\"title\",\"file\",\"year\",\"artist\",\"showtitle\",\"season\",\"episode\"]}}", player);
        reply = rpc(fd, request, 2);
        kodi_item(reply, status->title, sizeof(status->title));
        if (reply) json_object_put(reply);
        snprintf(request, sizeof(request),
            "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"Player.GetProperties\","
            "\"params\":{\"playerid\":%d,\"properties\":[\"time\",\"totaltime\",\"speed\",\"live\"]}}", player);
        reply = rpc(fd, request, 4);
        kodi_properties(reply, status);
        if (reply) json_object_put(reply);
    }
    close(fd);
}

static void kodi_notification(json_object *document, struct kodi_status *status, int *dirty)
{
    const char *method = string_field(document, "method");
    if (strcmp(method, "Application.OnVolumeChanged") == 0) {
        json_object *params, *data;
        if (json_object_object_get_ex(document, "params", &params) &&
            json_object_object_get_ex(params, "data", &data))
            kodi_volume(data, status, milliseconds());
    } else if (strncmp(method, "Player.On", 9) == 0) {
        *dirty = 1;
        if (strcmp(method, "Player.OnStop") == 0) {
            status->title[0] = '\0';
            status->time_valid = 0;
        }
    }
}

/* Dedicated persistent notification stream. Query connections are separate so
 * their response parsing cannot discard notifications arriving after a reply. */
struct event_stream {
    int fd;
    struct json_tokener *parser;
    size_t bytes;
};
static void events_close(struct event_stream *stream)
{
    if (stream->fd >= 0) close(stream->fd);
    stream->fd = -1;
    if (stream->parser) json_tokener_free(stream->parser);
    stream->parser = NULL;
    stream->bytes = 0;
}
static int events_read(struct event_stream *stream, struct kodi_status *status, int *dirty)
{
    size_t budget = 0;
    while (budget < MAX_JSON_BYTES) {
        char buffer[4096];
        ssize_t n = recv(stream->fd, buffer, sizeof(buffer), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (n <= 0) return -1;
        budget += (size_t)n;
        size_t offset = 0;
        while (offset < (size_t)n) {
            json_object *document = json_tokener_parse_ex(stream->parser, buffer + offset, (int)(n - offset));
            size_t used = json_tokener_get_parse_end(stream->parser);
            enum json_tokener_error error = json_tokener_get_error(stream->parser);
            offset += used;
            stream->bytes += used;
            if (stream->bytes > MAX_JSON_BYTES) { if (document) json_object_put(document); return -1; }
            if (error == json_tokener_continue) break;
            if (error != json_tokener_success || !used) { if (document) json_object_put(document); return -1; }
            kodi_notification(document, status, dirty);
            json_object_put(document);
            json_tokener_reset(stream->parser);
            stream->bytes = 0;
        }
    }
    return 0;
}
static void retroarch_status(const char *reply, char *title, size_t capacity)
{
    title[0] = '\0';
    if (strncmp(reply, "GET_STATUS PLAYING ", 19) != 0 &&
        strncmp(reply, "GET_STATUS PAUSED ", 18) != 0) return;
    const char *start = strchr(reply, ',');
    if (!start) return;
    ++start;
    const char *end = NULL, *search = start;
    while ((search = strstr(search, ",crc32=")) != NULL) { end = search; ++search; }
    if (!end || end == start) return;
    char name[4096];
    size_t length = (size_t)(end - start);
    if (length >= sizeof(name)) return;
    memcpy(name, start, length);
    name[length] = '\0';
    lcd_title(title, capacity, name);
}

static void retroarch_title(unsigned short port, char *title, size_t capacity)
{
    title[0] = '\0';
    long long deadline = milliseconds() + QUERY_TIMEOUT_MS;
    int fd = local_socket(SOCK_DGRAM, port, deadline);
    if (fd < 0) return;
    if (send_request(fd, "GET_STATUS\n", deadline) == 0 &&
        wait_socket(fd, POLLIN, deadline) == 0) {
        char reply[4096];
        ssize_t n = recv(fd, reply, sizeof(reply) - 1, 0);
        if (n > 0) { reply[n] = '\0'; retroarch_status(reply, title, capacity); }
    }
    close(fd);
}

static void *media_worker(void *argument)
{
    struct media_monitor *monitor = argument;
    struct kodi_status kodi = {0};
    struct event_stream events = {.fd = -1};
    char game[MEDIA_TITLE_SIZE] = "";
    long long next_refresh = 0, next_retroarch = 0, next_connect = 0;
    for (;;) {
        long long now = milliseconds();
        int dirty = 0;
        if (events.fd < 0 && now >= next_connect) {
            events.fd = local_socket(SOCK_STREAM, KODI_PORT, now + QUERY_TIMEOUT_MS);
            if (events.fd >= 0) {
                events.parser = json_tokener_new();
                if (!events.parser) events_close(&events);
                else next_refresh = 0;
            }
            next_connect = now + 2000;
        }
        if (events.fd >= 0 && events_read(&events, &kodi, &dirty) < 0) {
            events_close(&events);
            memset(&kodi, 0, sizeof(kodi));
            next_connect = now + 2000;
        }
        if (dirty || now >= next_refresh) {
            kodi_query(KODI_PORT, &kodi);
            next_refresh = milliseconds() + (events.fd >= 0 ? 10000 : 2000);
        }
        if (now >= next_retroarch) {
            retroarch_title(RETROARCH_PORT, game, sizeof(game));
            next_retroarch = milliseconds() + 1000;
        }
        enum media_source source = game[0] ? MEDIA_RETROARCH : kodi.title[0] ? MEDIA_KODI : MEDIA_NONE;
        pthread_mutex_lock(&monitor->mutex);
        snprintf(monitor->title, sizeof(monitor->title), "%s", game[0] ? game : kodi.title);
        monitor->source = source;
        monitor->kodi = kodi;
        struct timespec next;
        clock_gettime(CLOCK_MONOTONIC, &next);
        next.tv_nsec += 100000000;
        if (next.tv_nsec >= 1000000000) { ++next.tv_sec; next.tv_nsec -= 1000000000; }
        while (!monitor->stop) {
            int result = pthread_cond_timedwait(&monitor->wake, &monitor->mutex, &next);
            if (result == ETIMEDOUT) break;
        }
        int stop = monitor->stop;
        pthread_mutex_unlock(&monitor->mutex);
        if (stop) { events_close(&events); return NULL; }
    }
}
struct media_monitor *media_start(void)
{
    struct media_monitor *monitor = calloc(1, sizeof(*monitor));
    pthread_condattr_t attributes;
    if (!monitor) return NULL;
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0) goto failed;
    if (pthread_condattr_init(&attributes) != 0) goto mutex_failed;
    if (pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) != 0) goto attr_failed;
    if (pthread_cond_init(&monitor->wake, &attributes) != 0) goto attr_failed;
    pthread_condattr_destroy(&attributes);
    if (pthread_create(&monitor->thread, NULL, media_worker, monitor) == 0) return monitor;
    pthread_cond_destroy(&monitor->wake);
    pthread_mutex_destroy(&monitor->mutex);
    free(monitor);
    return NULL;
attr_failed:
    pthread_condattr_destroy(&attributes);
mutex_failed:
    pthread_mutex_destroy(&monitor->mutex);
failed:
    free(monitor);
    return NULL;
}

enum media_source media_snapshot(struct media_monitor *monitor, char *title, size_t size)
{
    enum media_source source = MEDIA_NONE;
    if (!size) return source;
    title[0] = '\0';
    if (monitor) {
        pthread_mutex_lock(&monitor->mutex);
        snprintf(title, size, "%s", monitor->title);
        source = monitor->source;
        pthread_mutex_unlock(&monitor->mutex);
    }
    return source;
}

void media_get_playback(struct media_monitor *monitor, struct media_playback *playback)
{
    memset(playback, 0, sizeof(*playback));
    if (!monitor) return;
    pthread_mutex_lock(&monitor->mutex);
    struct kodi_status status = monitor->kodi;
    enum media_source source = monitor->source;
    pthread_mutex_unlock(&monitor->mutex);
    long long now = milliseconds();
    playback->active = source == MEDIA_KODI;
    playback->time_valid = status.time_valid;
    playback->paused = status.speed == 0;
    playback->live = status.live;
    double elapsed = status.elapsed;
    if (status.time_valid && now > status.sampled_at)
        elapsed += (now - status.sampled_at) / 1000.0 * status.speed;
    if (elapsed < 0) elapsed = 0;
    if (status.total > 0 && elapsed > status.total) elapsed = status.total;
    if (elapsed > 359999) elapsed = 359999;
    playback->elapsed = (int)elapsed;
    playback->total = status.total > 0 ? (int)status.total : 0;
    playback->volume_visible = source != MEDIA_RETROARCH && now < status.volume_until;
    playback->volume = status.volume;
    playback->muted = status.muted;
}

static void format_time(int seconds, char text[9])
{
    if (seconds < 0) seconds = 0;
    if (seconds > 359999) seconds = 359999;
    if (seconds >= 3600)
        snprintf(text, 9, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        snprintf(text, 9, "%02d:%02d", seconds / 60, seconds % 60);
}

/* Empty means the caller should show its ordinary temperature row. */
void media_status_row(const struct media_playback *playback, char row[21])
{
    row[0] = '\0';
    if (playback->volume_visible) {
        if (playback->muted) snprintf(row, 21, "Muted");
        else snprintf(row, 21, "Volume %d%%", playback->volume);
    } else if (playback->active && playback->time_valid) {
        char elapsed[9], total[9];
        format_time(playback->elapsed, elapsed);
        format_time(playback->total, total);
        if (playback->paused) snprintf(row, 21, "Paused %s", elapsed);
        else if (playback->live) snprintf(row, 21, "Live %s", elapsed);
        else if (playback->total > 0) snprintf(row, 21, "%s / %s", elapsed, total);
        else snprintf(row, 21, "Playing %s", elapsed);
    }
}

void media_stop(struct media_monitor *monitor)
{
    if (!monitor) return;
    pthread_mutex_lock(&monitor->mutex);
    monitor->stop = 1;
    pthread_cond_signal(&monitor->wake);
    pthread_mutex_unlock(&monitor->mutex);
    pthread_join(monitor->thread, NULL);
    pthread_cond_destroy(&monitor->wake);
    pthread_mutex_destroy(&monitor->mutex);
    free(monitor);
}
