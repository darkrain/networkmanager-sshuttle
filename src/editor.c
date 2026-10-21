#include <NetworkManager.h>
#include <gtk/gtk.h>

#define SERVICE "org.freedesktop.NetworkManager.sshuttle"

typedef struct {
    GObject parent;
    GtkWidget *box;
    GtkWidget *host;
    GtkWidget *user;
    GtkWidget *port;
    GtkWidget *key;
} SshuttleEditor;
typedef struct { GObjectClass parent; } SshuttleEditorClass;
static void editor_interface_init(NMVpnEditorInterface *iface);
G_DEFINE_TYPE_WITH_CODE(SshuttleEditor, sshuttle_editor, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR, editor_interface_init))

static GObject *editor_widget(NMVpnEditor *iface) {
    return G_OBJECT(((SshuttleEditor *)iface)->box);
}
static gboolean editor_update(NMVpnEditor *iface, NMConnection *connection, GError **error) {
    SshuttleEditor *self = (SshuttleEditor *)iface;
    const char *host = gtk_editable_get_text(GTK_EDITABLE(self->host));
    const char *user = gtk_editable_get_text(GTK_EDITABLE(self->user));
    const char *key = gtk_editable_get_text(GTK_EDITABLE(self->key));
    if (!*host || !*user || !*key) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Enter the server IPv4 address, SSH user and private key path");
        return FALSE;
    }
    NMSettingVpn *vpn = nm_connection_get_setting_vpn(connection);
    if (!vpn) {
        vpn = NM_SETTING_VPN(nm_setting_vpn_new());
        nm_connection_add_setting(connection, NM_SETTING(vpn));
    }
    g_object_set(vpn, NM_SETTING_VPN_SERVICE_TYPE, SERVICE, NULL);
    nm_setting_vpn_add_data_item(vpn, "host", host);
    nm_setting_vpn_add_data_item(vpn, "user", user);
    g_autofree char *port = g_strdup_printf("%d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->port)));
    nm_setting_vpn_add_data_item(vpn, "port", port);
    nm_setting_vpn_add_data_item(vpn, "key", key);
    return TRUE;
}
static void editor_interface_init(NMVpnEditorInterface *iface) {
    iface->get_widget = editor_widget;
    iface->update_connection = editor_update;
}
static void field_changed(GtkEditable *editable, gpointer data) {
    (void)editable;
    g_signal_emit_by_name(data, "changed");
}
static GtkWidget *add_field(SshuttleEditor *self, const char *label, const char *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), value ? value : "");
    gtk_box_append(GTK_BOX(row), caption);
    gtk_box_append(GTK_BOX(row), entry);
    gtk_box_append(GTK_BOX(self->box), row);
    g_signal_connect(entry, "changed", G_CALLBACK(field_changed), self);
    return entry;
}
static void sshuttle_editor_init(SshuttleEditor *self) {
    self->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    g_object_ref_sink(self->box);
}
static void sshuttle_editor_dispose(GObject *object) {
    SshuttleEditor *self = (SshuttleEditor *)object;
    g_clear_object(&self->box);
    G_OBJECT_CLASS(sshuttle_editor_parent_class)->dispose(object);
}
static void sshuttle_editor_class_init(SshuttleEditorClass *klass) {
    G_OBJECT_CLASS(klass)->dispose = sshuttle_editor_dispose;
}

typedef struct { GObject parent; } SshuttlePlugin;
typedef struct { GObjectClass parent; } SshuttlePluginClass;
static void plugin_interface_init(NMVpnEditorPluginInterface *iface);
G_DEFINE_TYPE_WITH_CODE(SshuttlePlugin, sshuttle_plugin, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR_PLUGIN, plugin_interface_init))
enum { PROP_0, PROP_NAME, PROP_DESCRIPTION, PROP_SERVICE };
static void plugin_get_property(GObject *object, guint id, GValue *value, GParamSpec *pspec) {
    switch (id) {
    case PROP_NAME: g_value_set_string(value, "sshuttle over SSH"); break;
    case PROP_DESCRIPTION: g_value_set_string(value, "Transparent TCP tunnel over SSH"); break;
    case PROP_SERVICE: g_value_set_string(value, SERVICE); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
    }
}
static NMVpnEditor *plugin_get_editor(NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error) {
    (void)plugin;
    (void)error;
    NMSettingVpn *vpn = connection ? nm_connection_get_setting_vpn(connection) : NULL;
    const char *host = vpn ? nm_setting_vpn_get_data_item(vpn, "host") : NULL;
    const char *user = vpn ? nm_setting_vpn_get_data_item(vpn, "user") : NULL;
    const char *port = vpn ? nm_setting_vpn_get_data_item(vpn, "port") : NULL;
    const char *key = vpn ? nm_setting_vpn_get_data_item(vpn, "key") : NULL;
    g_autofree char *default_key = g_build_filename(g_get_home_dir(), ".ssh", "id_ed25519", NULL);
    if (!g_file_test(default_key, G_FILE_TEST_IS_REGULAR)) {
        g_clear_pointer(&default_key, g_free);
        default_key = g_build_filename(g_get_home_dir(), ".ssh", "id_rsa", NULL);
    }
    SshuttleEditor *self = g_object_new(sshuttle_editor_get_type(), NULL);
    self->host = add_field(self, "Server IPv4 address", host);
    self->user = add_field(self, "SSH user", user ? user : g_get_user_name());
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *caption = gtk_label_new("SSH port");
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    self->port = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->port), port ? g_ascii_strtoll(port, NULL, 10) : 22);
    gtk_box_append(GTK_BOX(row), caption);
    gtk_box_append(GTK_BOX(row), self->port);
    gtk_box_append(GTK_BOX(self->box), row);
    g_signal_connect(self->port, "value-changed", G_CALLBACK(field_changed), self);
    self->key = add_field(self, "SSH private key path", key ? key :
                          (g_file_test(default_key, G_FILE_TEST_IS_REGULAR) ? default_key : ""));
    return NM_VPN_EDITOR(self);
}
static NMVpnEditorPluginCapability plugin_capabilities(NMVpnEditorPlugin *plugin) {
    (void)plugin;
    return NM_VPN_EDITOR_PLUGIN_CAPABILITY_NONE;
}
static void plugin_interface_init(NMVpnEditorPluginInterface *iface) {
    iface->get_editor = plugin_get_editor;
    iface->get_capabilities = plugin_capabilities;
}
static void sshuttle_plugin_init(SshuttlePlugin *self) { (void)self; }
static void sshuttle_plugin_class_init(SshuttlePluginClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->get_property = plugin_get_property;
    g_object_class_override_property(object_class, PROP_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
    g_object_class_override_property(object_class, PROP_DESCRIPTION, NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
    g_object_class_override_property(object_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}
G_MODULE_EXPORT NMVpnEditorPlugin *nm_vpn_editor_plugin_factory(GError **error) {
    (void)error;
    return NM_VPN_EDITOR_PLUGIN(g_object_new(sshuttle_plugin_get_type(), NULL));
}
