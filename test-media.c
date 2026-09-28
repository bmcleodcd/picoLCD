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
        kodi_title(KODI_PORT, title, sizeof(title));
        printf("Kodi: %s\n", title[0] ? title : "idle/unavailable");
        retroarch_title(RETROARCH_PORT, title, sizeof(title));
        printf("RetroArch: %s\n", title[0] ? title : "no loaded content/unavailable");
        return 0;
    }
    expect_item("{\"result\":{\"item\":{\"title\":\"Film title\",\"label\":\"label\",\"file\":\"/film.mkv\"}}}", "Film title");
    expect_item("{\"result\":{\"item\":{\"title\":\"\",\"label\":\"Song\",\"file\":\"/song.flac\"}}}", "Song");
    expect_item("{\"result\":{\"item\":{\"file\":\"/media/untitled.mkv\"}}}", "/media/untitled.mkv");
    expect_item("{\"error\":{\"code\":-1}}", "");
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
