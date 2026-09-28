/* Exercise real D-Bus serialization on an isolated session bus, never systemd. */
#include <assert.h>
#include "src/panel.c"
int main(void)
{
    DBusError error = DBUS_ERROR_INIT;
    DBusConnection *server = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    DBusConnection *client = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    DBusPendingCall *pending;
    DBusMessage *request = NULL, *reply;
    const char *unit, *mode, *job = "/org/freedesktop/systemd1/job/123";
    int i;
    assert(server && client);
    assert(dbus_bus_request_name(server, "org.freedesktop.systemd1", 0, &error) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
    pending = start_retroarch(client);
    assert(pending);
    for (i = 0; i < 100 && !request; ++i) {
        dbus_connection_read_write_dispatch(client, 0);
        dbus_connection_read_write(server, 10);
        request = dbus_connection_pop_message(server);
        if (request && !dbus_message_is_method_call(request, "org.freedesktop.systemd1.Manager", "StartUnit")) {
            dbus_message_unref(request); request = NULL;
        }
    }
    assert(request);
    assert(dbus_message_get_args(request, &error, DBUS_TYPE_STRING, &unit,
                                 DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID));
    assert(strcmp(unit, "retroarch.service") == 0 && strcmp(mode, "replace") == 0);
    reply = dbus_message_new_method_return(request);
    assert(dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &job, DBUS_TYPE_INVALID));
    assert(dbus_connection_send(server, reply, NULL));
    dbus_connection_flush(server);
    for (i = 0; i < 100 && pending; ++i) {
        dbus_connection_read_write_dispatch(client, 10);
        check_launch(&pending);
    }
    assert(!pending);
    dbus_message_unref(reply); dbus_message_unref(request);
    dbus_connection_close(client); dbus_connection_unref(client);
    dbus_connection_close(server); dbus_connection_unref(server);
    dbus_shutdown();
    puts("Asynchronous systemd request test passed on isolated D-Bus");
    return 0;
}
