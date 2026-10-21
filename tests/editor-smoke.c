#include <NetworkManager.h>
#include <gtk/gtk.h>
#include <gmodule.h>

typedef NMVpnEditorPlugin *(*Factory)(GError **error);
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    gtk_init();
    GModule *module = g_module_open(argv[1], G_MODULE_BIND_LAZY);
    g_assert_nonnull(module);
    Factory factory = NULL;
    g_assert_true(g_module_symbol(module, "nm_vpn_editor_plugin_factory", (gpointer *)&factory));
    g_autoptr(GError) error = NULL;
    NMVpnEditorPlugin *plugin = factory(&error);
    g_assert_no_error(error);
    g_assert_nonnull(plugin);
    NMConnection *connection = nm_simple_connection_new();
    NMSetting *setting = nm_setting_connection_new();
    g_object_set(setting, NM_SETTING_CONNECTION_ID, "SSH test",
                 NM_SETTING_CONNECTION_UUID, "a9c47209-210a-4e66-97bf-413f06e22e4d",
                 NM_SETTING_CONNECTION_TYPE, "vpn", NULL);
    nm_connection_add_setting(connection, setting);
    NMVpnEditor *editor = nm_vpn_editor_plugin_get_editor(plugin, connection, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);
    GtkWidget *box = GTK_WIDGET(nm_vpn_editor_get_widget(editor));
    g_assert_true(GTK_IS_WIDGET(box));
    GtkWidget *host_row = gtk_widget_get_first_child(box);
    GtkWidget *host_label = gtk_widget_get_first_child(host_row);
    GtkWidget *host_entry = gtk_widget_get_next_sibling(host_label);
    g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(host_entry)), ==, "");
    g_assert_false(nm_vpn_editor_update_connection(editor, connection, &error));
    g_clear_error(&error);
    gtk_editable_set_text(GTK_EDITABLE(host_entry), "198.51.100.10");
    GtkWidget *user_row = gtk_widget_get_next_sibling(host_row);
    GtkWidget *user_entry = gtk_widget_get_next_sibling(gtk_widget_get_first_child(user_row));
    gtk_editable_set_text(GTK_EDITABLE(user_entry), "example");
    GtkWidget *port_row = gtk_widget_get_next_sibling(user_row);
    GtkWidget *key_row = gtk_widget_get_next_sibling(port_row);
    GtkWidget *key_entry = gtk_widget_get_next_sibling(gtk_widget_get_first_child(key_row));
    gtk_editable_set_text(GTK_EDITABLE(key_entry), "/tmp/example-key");
    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);
    NMSettingVpn *vpn = nm_connection_get_setting_vpn(connection);
    g_assert_cmpstr(nm_setting_vpn_get_service_type(vpn), ==, "org.freedesktop.NetworkManager.sshuttle");
    g_assert_cmpstr(nm_setting_vpn_get_data_item(vpn, "host"), ==, "198.51.100.10");
    g_assert_null(nm_setting_vpn_get_data_item(vpn, "known-hosts"));
    g_assert_true(nm_connection_verify(connection, &error));
    g_assert_no_error(error);
    g_object_unref(editor);
    nm_setting_vpn_add_data_item(vpn, "known-hosts", "/tmp/legacy-known-hosts");
    editor = nm_vpn_editor_plugin_get_editor(plugin, connection, &error);
    g_assert_no_error(error);
    box = GTK_WIDGET(nm_vpn_editor_get_widget(editor));
    host_row = gtk_widget_get_first_child(box);
    host_label = gtk_widget_get_first_child(host_row);
    host_entry = gtk_widget_get_next_sibling(host_label);
    g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(host_entry)), ==, "198.51.100.10");
    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(nm_setting_vpn_get_data_item(vpn, "known-hosts"), ==, "/tmp/legacy-known-hosts");
    g_object_unref(editor);
    g_object_unref(connection);
    g_object_unref(plugin);
    g_module_close(module);
    return 0;
}
