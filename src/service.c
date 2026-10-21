#include <NetworkManager.h>
#include <gio/gio.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define SERVICE "org.freedesktop.NetworkManager.sshuttle"

typedef struct {
    NMVpnServicePlugin parent;
    GSubprocess *child;
    GDataInputStream *error_stream;
    guint32 gateway;
    gboolean stopping;
    gboolean ready;
} SshuttleService;
typedef struct { NMVpnServicePluginClass parent; } SshuttleServiceClass;

G_DEFINE_TYPE(SshuttleService, sshuttle_service, NM_TYPE_VPN_SERVICE_PLUGIN)

static gboolean valid_name(const char *s) {
    if (!s || !*s) return FALSE;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (!(g_ascii_isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return FALSE;
    return TRUE;
}

static gboolean valid_path(const char *s) {
    if (!s || !g_path_is_absolute(s) || !g_utf8_validate(s, -1, NULL)) return FALSE;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p < 32 || *p == 127) return FALSE;
    return TRUE;
}

static void report_ready(SshuttleService *self) {
    if (self->ready || self->stopping) return;
    self->ready = TRUE;
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY, g_variant_new_uint32(self->gateway));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP4, g_variant_new_boolean(FALSE));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP6, g_variant_new_boolean(FALSE));
    nm_vpn_service_plugin_set_config(NM_VPN_SERVICE_PLUGIN(self), g_variant_builder_end(&b));
}

static void read_error_line(GObject *source, GAsyncResult *result, gpointer data) {
    SshuttleService *self = data;
    g_autoptr(GError) error = NULL;
    gsize size = 0;
    GDataInputStream *stream = G_DATA_INPUT_STREAM(source);
    g_autofree char *line = g_data_input_stream_read_line_finish(stream, result, &size, &error);
    if (stream != self->error_stream) return;
    if (line) {
        if (strstr(line, "Connected to server.")) report_ready(self);
        else if (!self->ready && *line) g_message("sshuttle: %s", line);
        g_data_input_stream_read_line_async(stream, G_PRIORITY_DEFAULT, NULL, read_error_line, self);
    } else if (error) {
        g_warning("sshuttle stderr: %s", error->message);
    }
}

static void child_finished(GObject *source, GAsyncResult *result, gpointer data) {
    SshuttleService *self = data;
    g_autoptr(GError) error = NULL;
    gboolean waited = g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
    if (!self->stopping) {
        g_warning("sshuttle exited: %s", waited ? "process stopped" : error->message);
        nm_vpn_service_plugin_failure(NM_VPN_SERVICE_PLUGIN(self), NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
    }
    g_clear_object(&self->error_stream);
    g_clear_object(&self->child);
    self->ready = FALSE;
}

static gboolean service_connect(NMVpnServicePlugin *plugin, NMConnection *connection, GError **error) {
    SshuttleService *self = (SshuttleService *)plugin;
    NMSettingVpn *vpn = nm_connection_get_setting_vpn(connection);
    const char *host = vpn ? nm_setting_vpn_get_data_item(vpn, "host") : NULL;
    const char *user = vpn ? nm_setting_vpn_get_data_item(vpn, "user") : NULL;
    const char *port_text = vpn ? nm_setting_vpn_get_data_item(vpn, "port") : NULL;
    const char *key = vpn ? nm_setting_vpn_get_data_item(vpn, "key") : NULL;
    const char *known_hosts = vpn ? nm_setting_vpn_get_data_item(vpn, "known-hosts") : NULL;
    struct in_addr addr;
    char *end = NULL;
    long port = port_text ? strtol(port_text, &end, 10) : 22;
    if (self->child) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "sshuttle is already running");
        return FALSE;
    }
    if (!host || inet_pton(AF_INET, host, &addr) != 1 || !valid_name(user) || !valid_path(key) ||
        !valid_path(known_hosts) || port < 1 || port > 65535 || (port_text && (!end || *end))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid SSH server, port, user or file path");
        return FALSE;
    }
    if (!g_file_test(key, G_FILE_TEST_IS_REGULAR) || !g_file_test(known_hosts, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "SSH key or known_hosts file is missing");
        return FALSE;
    }
    g_autofree char *remote = g_strdup_printf("%s@%s:%ld", user, host, port);
    g_autofree char *exclude_host = g_strdup_printf("%s/32", host);
    g_autofree char *quoted_known_hosts = g_shell_quote(known_hosts);
    g_autofree char *quoted_key = g_shell_quote(key);
    g_autofree char *ssh_cmd = g_strdup_printf("/usr/bin/ssh -F /dev/null -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=20 -o ServerAliveCountMax=3 -o IdentityAgent=none -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=%s -i %s", quoted_known_hosts, quoted_key);
    const char *argv[] = {"/usr/bin/sshuttle", "--method=nft", "--disable-ipv6", "--dns",
                          "--remote", remote, "--exclude", exclude_host,
                          "--exclude", "10.0.0.0/8", "--exclude", "172.16.0.0/12",
                          "--exclude", "192.168.0.0/16", "--ssh-cmd", ssh_cmd,
                          "0.0.0.0/0", NULL};
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_PIPE);
    self->child = g_subprocess_launcher_spawnv(launcher, argv, error);
    if (!self->child) return FALSE;
    self->stopping = FALSE;
    self->ready = FALSE;
    self->gateway = addr.s_addr;
    self->error_stream = g_data_input_stream_new(g_subprocess_get_stderr_pipe(self->child));
    g_data_input_stream_read_line_async(self->error_stream, G_PRIORITY_DEFAULT, NULL, read_error_line, self);
    g_subprocess_wait_async(self->child, NULL, child_finished, self);
    return TRUE;
}

static gboolean service_disconnect(NMVpnServicePlugin *plugin, GError **error) {
    (void)error;
    SshuttleService *self = (SshuttleService *)plugin;
    self->stopping = TRUE;
    if (self->child) g_subprocess_send_signal(self->child, SIGINT);
    return TRUE;
}

static gboolean service_need_secrets(NMVpnServicePlugin *plugin, NMConnection *connection,
                                     const char **setting_name, GError **error) {
    (void)plugin;
    (void)connection;
    (void)setting_name;
    (void)error;
    return FALSE;
}

static void sshuttle_service_class_init(SshuttleServiceClass *klass) {
    NMVpnServicePluginClass *vpn_class = NM_VPN_SERVICE_PLUGIN_CLASS(klass);
    vpn_class->connect = service_connect;
    vpn_class->disconnect = service_disconnect;
    vpn_class->need_secrets = service_need_secrets;
}
static void sshuttle_service_init(SshuttleService *self) { (void)self; }

int main(int argc, char **argv) {
    gboolean persist = FALSE;
    gboolean debug = FALSE;
    char *bus_name = g_strdup(SERVICE);
    GOptionEntry options[] = {
        { "persist", 0, 0, G_OPTION_ARG_NONE, &persist, NULL, NULL },
        { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, NULL, NULL },
        { "bus-name", 0, 0, G_OPTION_ARG_STRING, &bus_name, NULL, NULL },
        { NULL }
    };
    g_autoptr(GOptionContext) context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, options, NULL);
    g_autoptr(GError) error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("nm-sshuttle: %s\n", error->message);
        return 1;
    }
    (void)persist;
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    SshuttleService *service = g_initable_new(sshuttle_service_get_type(), NULL, &error,
        NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
        NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, !debug, NULL);
    g_free(bus_name);
    if (!service) {
        g_printerr("nm-sshuttle: %s\n", error->message);
        return 1;
    }
    g_signal_connect_swapped(service, "quit", G_CALLBACK(g_main_loop_quit), loop);
    g_main_loop_run(loop);
    if (service->child) g_subprocess_send_signal(service->child, SIGINT);
    g_object_unref(service);
    return 0;
}
