#include <assert.h>
#include "src/media.c"

struct rpc_server { int fd; const char *reply; };
static void *serve_rpc(void *argument)
{
    struct rpc_server *server = argument;
    char request[256];
    assert(recv(server->fd, request, sizeof(request), 0) > 0);
    size_t i, length = strlen(server->reply);
    for (i = 0; i < length; i += 3) {
        size_t chunk = length - i < 3 ? length - i : 3;
        assert(send(server->fd, server->reply + i, chunk, MSG_NOSIGNAL) == (ssize_t)chunk);
        usleep(1000);
    }
    close(server->fd);
    return NULL;
}

static json_object *fragmented_rpc(const char *response)
{
    int sockets[2];
    pthread_t thread;
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) == 0);
    struct rpc_server server = {sockets[1], response};
    /* Queue request before server starts, avoiding a blocking server socket. */
    assert(send(sockets[0], "request", 7, MSG_NOSIGNAL) == 7);
    assert(pthread_create(&thread, NULL, serve_rpc, &server) == 0);
    json_object *reply = rpc(sockets[0], "", 7);
    pthread_join(thread, NULL);
    close(sockets[0]);
    return reply;
}

static void expect_item(const char *input, const char *expected)
{
    char title[MEDIA_TITLE_SIZE];
    json_object *reply = json_tokener_parse(input);
    kodi_item(reply, title, sizeof(title));
    assert(strcmp(title, expected) == 0);
    json_object_put(reply);
}

int main(int argc, char **argv)
{
    char title[MEDIA_TITLE_SIZE];
    if (argc == 2 && strcmp(argv[1], "--live") == 0) {
        struct kodi_status kodi = {0};
        kodi_query(KODI_PORT, &kodi);
        printf("Kodi: %s; time=%.1f/%.1f speed=%d live=%d volume=%d muted=%d\n",
               kodi.title[0] ? kodi.title : "idle/unavailable", kodi.elapsed, kodi.total,
               kodi.speed, kodi.live, kodi.volume, kodi.muted);
        retroarch_title(RETROARCH_PORT, title, sizeof(title));
        printf("RetroArch: %s\n", title[0] ? title : "no loaded content/unavailable");
        return 0;
    }
    expect_item("{\"result\":{\"item\":{\"type\":\"movie\",\"title\":\"Speed\",\"year\":1994}}}", "Speed (1994)");
    expect_item("{\"result\":{\"item\":{\"type\":\"movie\",\"title\":\"Film title\",\"year\":0}}}", "Film title");
    expect_item("{\"result\":{\"item\":{\"type\":\"movie\",\"title\":\"Film title\",\"label\":\"label\",\"file\":\"/film.mkv\"}}}", "Film title");
    expect_item("{\"result\":{\"item\":{\"title\":\"\",\"label\":\"Song\",\"file\":\"/song.flac\"}}}", "Song");
    expect_item("{\"result\":{\"item\":{\"file\":\"/media/untitled.mkv\"}}}", "/media/untitled.mkv");
    expect_item("{\"error\":{\"code\":-1}}", "");
    expect_item("{\"result\":{\"item\":{\"title\":\"Pilot\",\"showtitle\":\"Example Show\",\"season\":2,\"episode\":4}}}", "Example Show S02E04 - Pilot");
    expect_item("{\"result\":{\"item\":{\"title\":\"Track\",\"artist\":[\"Artist\"]}}}", "Artist - Track");
    expect_item("{\"result\":{\"item\":{\"title\":\"Caf\u00e9\\nconcert\"}}}", "Caf? concert");
    json_object *reply = json_tokener_parse("{\"result\":[{\"playerid\":0,\"type\":\"audio\"},{\"playerid\":1,\"type\":\"video\"}]}");
    assert(kodi_player(reply) == 1); json_object_put(reply);
    reply = json_tokener_parse("{\"result\":[]}");
    assert(kodi_player(reply) == -1); json_object_put(reply);
    retroarch_status("GET_STATUS PLAYING snes,Super Mario World,crc32=1234\n", title, sizeof(title));
    assert(strcmp(title, "Super Mario World") == 0);
    retroarch_status("GET_STATUS PAUSED psx,Game, Disc 2,crc32=abcdef\n", title, sizeof(title));
    assert(strcmp(title, "Game, Disc 2") == 0);
    retroarch_status("GET_STATUS CONTENTLESS", title, sizeof(title));
    assert(!title[0]);
    retroarch_status("GET_STATUS PLAYING malformed", title, sizeof(title));
    assert(!title[0]);
    char tiny[4];
    retroarch_status("GET_STATUS PLAYING snes,Long title,crc32=1234", tiny, sizeof(tiny));
    assert(strcmp(tiny, "Lon") == 0);
    reply = fragmented_rpc("{\"jsonrpc\":\"2.0\",\"method\":\"Player.OnPlay\"}{\"id\":7,\"result\":{\"item\":{\"title\":\"A \\\"quoted\\\" title\"}}}");
    assert(reply);
    kodi_item(reply, title, sizeof(title));
    assert(strcmp(title, "A \"quoted\" title") == 0);
    json_object_put(reply);
    struct kodi_status status = {0};
    reply = json_tokener_parse("{\"result\":{\"time\":{\"hours\":1,\"minutes\":2,\"seconds\":3,\"milliseconds\":0},\"totaltime\":{\"hours\":2,\"minutes\":0,\"seconds\":0,\"milliseconds\":0},\"speed\":0,\"live\":false}}");
    kodi_properties(reply, &status);
    assert(status.time_valid && status.elapsed == 3723 && status.total == 7200 && status.speed == 0);
    json_object_put(reply);
    reply = json_tokener_parse("{\"volume\":50,\"muted\":false}");
    kodi_volume(reply, &status, 1000);
    assert(status.volume_known && !status.volume_until); /* Initial state isn't a change. */
    json_object_put(reply);
    reply = json_tokener_parse("{\"volume\":65,\"muted\":false}");
    kodi_volume(reply, &status, 2000);
    assert(status.volume_until == 5000 && status.volume == 65);
    kodi_volume(reply, &status, 2500);
    assert(status.volume_until == 5000); /* Polling doesn't extend the overlay. */
    json_object_put(reply);
    struct media_playback playback = {.active=1, .time_valid=1, .elapsed=3723, .total=7200};
    char row[21];
    media_status_row(&playback, row); assert(strcmp(row, "01:02:03 / 02:00:00") == 0);
    playback.paused = 1;
    media_status_row(&playback, row); assert(strcmp(row, "Paused 01:02:03") == 0);
    playback.volume_visible = 1; playback.volume = 65;
    media_status_row(&playback, row); assert(strcmp(row, "Volume 65%") == 0);
    playback.muted = 1;
    media_status_row(&playback, row); assert(strcmp(row, "Muted") == 0);
    playback.volume_visible = 0; playback.paused = 0; playback.live = 1;
    media_status_row(&playback, row); assert(strcmp(row, "Live 01:02:03") == 0);
    playback.active = 0;
    media_status_row(&playback, row); assert(!row[0]);
    struct media_monitor sample = {0};
    assert(pthread_mutex_init(&sample.mutex, NULL) == 0);
    sample.source = MEDIA_KODI; sample.kodi = status;
    sample.kodi.speed = 1; sample.kodi.sampled_at = milliseconds() - 5000;
    media_get_playback(&sample, &playback); assert(playback.elapsed >= 3728 && playback.elapsed <= 3729);
    sample.kodi.speed = 0;
    media_get_playback(&sample, &playback); assert(playback.elapsed == 3723 && playback.paused);
    sample.kodi.volume_until = milliseconds() - 1;
    media_get_playback(&sample, &playback); assert(!playback.volume_visible);
    pthread_mutex_destroy(&sample.mutex);
    int stream_fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, stream_fds) == 0);
    struct event_stream stream = {.fd=stream_fds[0], .parser=json_tokener_new()};
    assert(stream.parser);
    int dirty = 0;
    const char *part1 = "{\"method\":\"Application.OnVolumeChanged\",\"params\":{";
    const char *part2 = "\"data\":{\"volume\":65,\"muted\":true}}}{\"method\":\"Player.OnPause\"}";
    assert(send(stream_fds[1], part1, strlen(part1), 0) == (ssize_t)strlen(part1));
    assert(events_read(&stream, &status, &dirty) == 0 && !dirty);
    assert(send(stream_fds[1], part2, strlen(part2), 0) == (ssize_t)strlen(part2));
    assert(events_read(&stream, &status, &dirty) == 0 && dirty && status.muted);
    assert(status.volume_until > milliseconds());
    strcpy(status.title, "old title");
    reply = json_tokener_parse("{\"method\":\"Player.OnStop\"}");
    kodi_notification(reply, &status, &dirty);
    assert(!status.title[0] && !status.time_valid); json_object_put(reply);
    close(stream_fds[1]);
    assert(events_read(&stream, &status, &dirty) == -1);
    events_close(&stream);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) == 0);
    long long before = milliseconds();
    assert(rpc(sockets[0], "request", 1) == NULL);
    assert(milliseconds() - before < 1500);
    close(sockets[0]); close(sockets[1]);
    strcpy(title, "old title");
    assert(media_snapshot(NULL, title, sizeof(title)) == MEDIA_NONE && !title[0]);
    struct media_monitor *monitor = media_start();
    assert(monitor);
    usleep(1200000);
    enum media_source source = media_snapshot(monitor, title, sizeof(title));
    assert(source >= MEDIA_NONE && source <= MEDIA_RETROARCH);
    media_stop(monitor);
    puts("Media title, fallback, fragmented JSON, and timeout tests passed");
    return 0;
}
