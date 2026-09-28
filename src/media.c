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

struct media_monitor {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    int stop;
    enum media_source source;
    char title[MEDIA_TITLE_SIZE];
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
        if (title[0]) return;
    }
}

static void kodi_title(unsigned short port, char *title, size_t capacity)
{
    title[0] = '\0';
    int fd = local_socket(SOCK_STREAM, port, milliseconds() + QUERY_TIMEOUT_MS);
    if (fd < 0) return;
    json_object *reply = rpc(fd,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"Player.GetActivePlayers\"}", 1);
    int player = kodi_player(reply);
    if (reply) json_object_put(reply);
    if (player >= 0) {
        char request[256];
        snprintf(request, sizeof(request),
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"Player.GetItem\","
            "\"params\":{\"playerid\":%d,\"properties\":[\"title\",\"file\"]}}", player);
        reply = rpc(fd, request, 2);
        kodi_item(reply, title, capacity);
        if (reply) json_object_put(reply);
    }
    close(fd);
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
    for (;;) {
        char title[MEDIA_TITLE_SIZE];
        enum media_source source = MEDIA_RETROARCH;
        retroarch_title(RETROARCH_PORT, title, sizeof(title));
        if (!title[0]) { source = MEDIA_KODI; kodi_title(KODI_PORT, title, sizeof(title)); }
        if (!title[0]) source = MEDIA_NONE;
        pthread_mutex_lock(&monitor->mutex);
        memcpy(monitor->title, title, strlen(title) + 1);
        monitor->source = source;
        struct timespec next;
        clock_gettime(CLOCK_MONOTONIC, &next);
        ++next.tv_sec;
        while (!monitor->stop) {
            int result = pthread_cond_timedwait(&monitor->wake, &monitor->mutex, &next);
            if (result == ETIMEDOUT) break;
        }
        int stop = monitor->stop;
        pthread_mutex_unlock(&monitor->mutex);
        if (stop) return NULL;
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
