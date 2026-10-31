// SPDX-License-Identifier: MIT
//
// GTK 3 module that moves menu bars into KDE Plasma's Global Menu.
//
// Every GtkMenuBar that sits in a normal toplevel window is exported as com.canonical.dbusmenu (with
// libdbusmenu-gtk, which keeps it in sync with the widgets) and the window is linked to it the way Qt does it:
// the org_kde_kwin_appmenu protocol on native Wayland, _KDE_NET_WM_APPMENU_* window properties on X11. The
// menu bar then takes no space in the window. Alt+<mnemonic> and F10 ask Plasma to open the menu in the panel.
//
// Only active when there is a global menu (com.canonical.AppMenu.Registrar on the session bus, the same check
// Qt does); everywhere else the module does nothing and menu bars stay in the window.

#define G_LOG_DOMAIN "kde-appmenu-gtk-module"

#include <dlfcn.h>
#include <string.h>

#include <gtk/gtk.h>
#include <libdbusmenu-glib/server.h>
#include <libdbusmenu-gtk/menuitem.h>
#include <libdbusmenu-gtk/parser.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#include "appmenu-client-protocol.h"
#endif

#ifdef GDK_WINDOWING_X11
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#endif

#define REGISTRAR "com.canonical.AppMenu.Registrar"

typedef struct {
    GtkWidget *bar;
    GtkWidget *window; // weak
    DbusmenuServer *server;
    DbusmenuMenuitem *root;
    gchar *path;
    guint fix_source;
#ifdef GDK_WINDOWING_WAYLAND
    struct org_kde_kwin_appmenu *appmenu;
#endif
} Export;

// GtkMenuBar -> its Export; GtkWindow -> the Export of its global menu (not owned)
static GQuark bar_quark;
static GQuark window_quark;

static enum { STATE_UNKNOWN, STATE_ENABLED, STATE_DISABLED } state = STATE_UNKNOWN;
static GDBusConnection *bus;
static guint last_path_id;

#ifdef GDK_WINDOWING_WAYLAND
static struct wl_display *wl_display;
static struct org_kde_kwin_appmenu_manager *appmenu_manager;
#endif

static void (*widget_size_allocate)(GtkWidget *, GtkAllocation *);
static void (*orig_get_preferred_width)(GtkWidget *, gint *, gint *);
static void (*orig_get_preferred_height)(GtkWidget *, gint *, gint *);
static void (*orig_get_preferred_width_for_height)(GtkWidget *, gint, gint *, gint *);
static void (*orig_get_preferred_height_for_width)(GtkWidget *, gint, gint *, gint *);
static void (*orig_size_allocate)(GtkWidget *, GtkAllocation *);
static gboolean (*orig_draw)(GtkWidget *, cairo_t *);
static void (*orig_hierarchy_changed)(GtkWidget *, GtkWidget *);

static Export *bar_export(GtkWidget *bar)
{
    return g_object_get_qdata(G_OBJECT(bar), bar_quark);
}

static Export *window_export(GtkWidget *window)
{
    return g_object_get_qdata(G_OBJECT(window), window_quark);
}

// ---------------------------------------------------------------------------------------------------------------
// Is there a global menu to put menu bars in?

#ifdef GDK_WINDOWING_WAYLAND
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    if (!appmenu_manager && strcmp(interface, org_kde_kwin_appmenu_manager_interface.name) == 0) {
        appmenu_manager = wl_registry_bind(registry, name, &org_kde_kwin_appmenu_manager_interface, MIN(version, 2u));
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static gboolean wayland_init(GdkDisplay *display)
{
    wl_display = gdk_wayland_display_get_wl_display(display);

    // Our own queue, so the roundtrip doesn't dispatch GDK's events. The appmenu objects never get events, so
    // nothing has to dispatch this queue afterwards. It lives as long as the process.
    struct wl_event_queue *queue = wl_display_create_queue(wl_display);
    struct wl_display *wrapper = wl_proxy_create_wrapper(wl_display);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
    struct wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);

    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip_queue(wl_display, queue);
    wl_registry_destroy(registry);

    return appmenu_manager != NULL;
}
#endif

static gboolean other_module_loaded(void)
{
    void *handle = dlopen("libappmenu-gtk-module.so", RTLD_LAZY | RTLD_NOLOAD);
    if (handle) {
        dlclose(handle);
        return TRUE;
    }
    return FALSE;
}

static gboolean enabled(GdkDisplay *display)
{
    if (state != STATE_UNKNOWN) {
        return state == STATE_ENABLED;
    }
    state = STATE_DISABLED;

    if (other_module_loaded()) {
        g_message("appmenu-gtk-module is loaded as well, leaving the menu bars to it");
        return FALSE;
    }

    g_autoptr(GError) error = NULL;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!bus) {
        g_debug("no session bus: %s", error->message);
        return FALSE;
    }

    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(bus,
                                                            "org.freedesktop.DBus",
                                                            "/org/freedesktop/DBus",
                                                            "org.freedesktop.DBus",
                                                            "NameHasOwner",
                                                            g_variant_new("(s)", REGISTRAR),
                                                            G_VARIANT_TYPE("(b)"),
                                                            G_DBUS_CALL_FLAGS_NONE,
                                                            1000,
                                                            NULL,
                                                            &error);
    gboolean registrar = FALSE;
    if (reply) {
        g_variant_get(reply, "(b)", &registrar);
    }
    if (!registrar) {
        g_debug("no global menu, " REGISTRAR " isn't on the session bus");
        return FALSE;
    }

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(display)) {
        if (!wayland_init(display)) {
            g_debug("the compositor doesn't support org_kde_kwin_appmenu_manager");
            return FALSE;
        }
        state = STATE_ENABLED;
    }
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(display)) {
        state = STATE_ENABLED;
    }
#endif

    g_debug("global menu %s", state == STATE_ENABLED ? "enabled" : "not supported on this display");
    return state == STATE_ENABLED;
}

// ---------------------------------------------------------------------------------------------------------------
// Telling the window manager where a window's menu is

#ifdef GDK_WINDOWING_WAYLAND
static void withdraw_wayland(Export *e)
{
    if (e->appmenu) {
        org_kde_kwin_appmenu_release(e->appmenu);
        e->appmenu = NULL;
        wl_display_flush(wl_display);
    }
}
#endif

#ifdef GDK_WINDOWING_X11
static void x11_set_property(GdkWindow *window, const char *name, const char *value)
{
    GdkDisplay *display = gdk_window_get_display(window);
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(display);
    Atom atom = gdk_x11_get_xatom_by_name_for_display(display, name);

    gdk_x11_display_error_trap_push(display);
    if (value) {
        XChangeProperty(xdisplay, GDK_WINDOW_XID(window), atom, XA_STRING, 8, PropModeReplace, (const guchar *)value, strlen(value));
    } else {
        XDeleteProperty(xdisplay, GDK_WINDOW_XID(window), atom);
    }
    gdk_x11_display_error_trap_pop_ignored(display);
}
#endif

static void announce(Export *e)
{
    GdkWindow *window = e->window ? gtk_widget_get_window(e->window) : NULL;
    if (!window) {
        return;
    }
    const gchar *service = g_dbus_connection_get_unique_name(bus);

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_WINDOW(window)) {
        // GDK destroys the wl_surface when the window is hidden and makes a new one when it's shown again
        withdraw_wayland(e);
        struct wl_surface *surface = gtk_widget_get_mapped(e->window) ? gdk_wayland_window_get_wl_surface(window) : NULL;
        if (surface) {
            e->appmenu = org_kde_kwin_appmenu_manager_create(appmenu_manager, surface);
            org_kde_kwin_appmenu_set_address(e->appmenu, service, e->path);
            wl_display_flush(wl_display);
        }
        return;
    }
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_WINDOW(window)) {
        x11_set_property(window, "_KDE_NET_WM_APPMENU_SERVICE_NAME", service);
        x11_set_property(window, "_KDE_NET_WM_APPMENU_OBJECT_PATH", e->path);
        // GtkApplication also announces its menus, for Plasma's GTK menu proxy, which would then take the two
        // properties above over. Those menus are already in the menu bar we export (see on_display_opened()).
        x11_set_property(window, "_GTK_MENUBAR_OBJECT_PATH", NULL);
        x11_set_property(window, "_GTK_APP_MENU_OBJECT_PATH", NULL);
    }
#endif
}

static void withdraw(Export *e)
{
#ifdef GDK_WINDOWING_WAYLAND
    withdraw_wayland(e);
#endif
#ifdef GDK_WINDOWING_X11
    GdkWindow *window = e->window && gtk_widget_get_realized(e->window) ? gtk_widget_get_window(e->window) : NULL;
    if (window && GDK_IS_X11_WINDOW(window)) {
        x11_set_property(window, "_KDE_NET_WM_APPMENU_SERVICE_NAME", NULL);
        x11_set_property(window, "_KDE_NET_WM_APPMENU_OBJECT_PATH", NULL);
    }
#endif
}

// X11 wants the properties before the window is mapped, Wayland needs the surface that only exists while it is
static void on_window_realize(GtkWidget *window, gpointer data)
{
    (void)data;
    Export *e = window_export(window);
#ifdef GDK_WINDOWING_X11
    if (e && GDK_IS_X11_WINDOW(gtk_widget_get_window(window))) {
        announce(e);
    }
#else
    (void)e;
#endif
}

static void on_window_map(GtkWidget *window, gpointer data)
{
    (void)data;
    Export *e = window_export(window);
#ifdef GDK_WINDOWING_WAYLAND
    if (e && GDK_IS_WAYLAND_WINDOW(gtk_widget_get_window(window))) {
        announce(e);
    }
#else
    (void)e;
#endif
}

static void on_window_unmap(GtkWidget *window, gpointer data)
{
    (void)data;
    Export *e = window_export(window);
#ifdef GDK_WINDOWING_WAYLAND
    if (e) {
        withdraw_wayland(e);
    }
#else
    (void)e;
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Opening the menu from the keyboard

// Ask Plasma to open the menu of a top-level item, like Qt does for Alt+<mnemonic> on a native menu bar
static gboolean request_menu(GtkWidget *item)
{
    GtkWidget *bar = gtk_widget_get_parent(item);
    if (!bar || !bar_export(bar) || !gtk_menu_item_get_submenu(GTK_MENU_ITEM(item))) {
        return FALSE;
    }
    DbusmenuMenuitem *mi = dbusmenu_gtk_parse_get_cached_item(item);
    if (!mi) {
        return FALSE;
    }
    dbusmenu_menuitem_show_to_user(mi, gtk_get_current_event_time());
    return TRUE;
}

// Alt+<mnemonic> of a top-level item. Returning TRUE keeps GTK from opening the menu in the (hidden) menu bar.
static gboolean on_item_mnemonic_activate(GtkWidget *item, gboolean group_cycling, gpointer data)
{
    (void)group_cycling;
    (void)data;
    return request_menu(item);
}

static void watch_item(GtkWidget *item, gpointer data)
{
    (void)data;
    // Stays connected for good, it does nothing unless the menu bar is exported
    if (GTK_IS_MENU_ITEM(item) && !g_signal_handler_find(item, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_item_mnemonic_activate, NULL)) {
        g_signal_connect(item, "mnemonic-activate", G_CALLBACK(on_item_mnemonic_activate), NULL);
    }
}

static void on_bar_insert(GtkMenuShell *bar, GtkWidget *item, gint position, gpointer data)
{
    (void)bar;
    (void)position;
    watch_item(item, data);
}

static gboolean window_has_accel(GtkWindow *window, guint keyval, GdkModifierType mods)
{
    for (GSList *l = gtk_accel_groups_from_object(G_OBJECT(window)); l; l = l->next) {
        guint n = 0;
        if (gtk_accel_group_query(l->data, keyval, mods, &n) && n > 0) {
            return TRUE;
        }
    }

    GtkApplication *app = gtk_window_get_application(window);
    if (app) {
        g_autofree gchar *accel = gtk_accelerator_name(keyval, mods);
        g_auto(GStrv) actions = gtk_application_get_actions_for_accel(app, accel);
        if (actions && actions[0]) {
            return TRUE;
        }
    }
    return FALSE;
}

// F10 (gtk-menu-bar-accel). GTK opens the first menu of the menu bar right away, so this has to come first. Like
// GTK, it leaves the key to the app if the app uses it as a shortcut.
static gint key_snooper(GtkWidget *grab_widget, GdkEventKey *event, gpointer data)
{
    (void)data;
    if (event->type != GDK_KEY_PRESS) {
        return FALSE;
    }
    GtkWidget *window = gtk_widget_get_toplevel(grab_widget);
    Export *e = GTK_IS_WINDOW(window) ? window_export(window) : NULL;
    if (!e) {
        return FALSE;
    }

    g_autofree gchar *accel = NULL;
    g_object_get(gtk_widget_get_settings(window), "gtk-menu-bar-accel", &accel, NULL);
    guint keyval = 0;
    GdkModifierType mods = 0;
    if (accel && *accel) {
        gtk_accelerator_parse(accel, &keyval, &mods);
    }
    const GdkModifierType mask = gtk_accelerator_get_default_mod_mask();
    if (keyval == 0 || event->keyval != keyval || (event->state & mask) != (mods & mask)) {
        return FALSE;
    }
    if (window_has_accel(GTK_WINDOW(window), keyval, mods & mask)) {
        return FALSE;
    }

    gboolean handled = FALSE;
    GList *items = gtk_container_get_children(GTK_CONTAINER(e->bar));
    for (GList *l = items; l && !handled; l = l->next) {
        if (gtk_widget_get_visible(l->data) && gtk_widget_is_sensitive(l->data)) {
            handled = request_menu(l->data);
        }
    }
    g_list_free(items);
    return handled;
}

// ---------------------------------------------------------------------------------------------------------------
// libdbusmenu-gtk predates parts of GTK 3, fill in what its parser gets wrong

// The shortcut of a GAction a widget in the menu item stands for, e.g. GIMP's shortcut labels, which have the action
// they show the shortcut of. GIMP registers the shortcuts with GtkApplication, as "app.<name>" or, for radio
// actions, "app.<name>(<value>)".
static gboolean action_accel(GtkWidget *widget, guint *keyval, GdkModifierType *mods)
{
    GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(widget), "action");
    GApplication *app = g_application_get_default();
    if (!pspec || !G_IS_PARAM_SPEC_OBJECT(pspec) || !GTK_IS_APPLICATION(app)) {
        return FALSE;
    }
    g_autoptr(GObject) action = NULL;
    g_object_get(widget, "action", &action, NULL);
    if (!G_IS_ACTION(action)) {
        return FALSE;
    }

    g_autofree gchar *name = NULL;
    GParamSpec *value_pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(action), "value");
    if (value_pspec && G_IS_PARAM_SPEC_INT(value_pspec)) {
        gint value = 0;
        g_object_get(action, "value", &value, NULL);
        name = g_strdup_printf("app.%s(%i)", g_action_get_name(G_ACTION(action)), value);
    } else {
        name = g_strdup_printf("app.%s", g_action_get_name(G_ACTION(action)));
    }
    g_auto(GStrv) accels = gtk_application_get_accels_for_action(GTK_APPLICATION(app), name);
    if (accels && accels[0]) {
        gtk_accelerator_parse(accels[0], keyval, mods);
    }
    return *keyval != 0;
}

// The shortcut a menu item shows
static gboolean find_accel(GtkWidget *widget, guint *keyval, GdkModifierType *mods)
{
    if (GTK_IS_ACCEL_LABEL(widget)) {
        gtk_accel_label_get_accel(GTK_ACCEL_LABEL(widget), keyval, mods);
        if (*keyval) {
            return TRUE;
        }
    } else if (GTK_IS_LABEL(widget) && action_accel(widget, keyval, mods)) {
        return TRUE;
    }

    gboolean found = FALSE;
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = children; l && !found; l = l->next) {
            found = find_accel(l->data, keyval, mods);
        }
        g_list_free(children);
    }
    return found;
}

static void fix_menu(GtkWidget *menu);

// Fix up a submenu right before it's shown, for changes that don't make it relayout (e.g. a changed shortcut)
static gboolean on_about_to_show(DbusmenuMenuitem *mi, GtkWidget *item)
{
    (void)mi;
    GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
    if (submenu) {
        fix_menu(submenu);
    }
    return FALSE;
}

static void fix_item(GtkWidget *item, gpointer data)
{
    (void)data;
    if (!GTK_IS_MENU_ITEM(item)) {
        return;
    }
    DbusmenuMenuitem *mi = dbusmenu_gtk_parse_get_cached_item(item);
    if (!mi) {
        return;
    }

    guint keyval = 0;
    GdkModifierType mods = 0;

    // The items of menus made from a GMenuModel (GtkApplication menus) are all GtkCheckMenuItems, the parser turns
    // every one of them into a checkbox. And it only knows shortcuts that are accel closures, not the ones these
    // items show, the ones set with gtk_accel_label_set_accel(), or GIMP's.
    static GType model_item_type = 0;
    if (!model_item_type) {
        model_item_type = g_type_from_name("GtkModelMenuItem");
    }
    if (model_item_type && G_TYPE_CHECK_INSTANCE_TYPE(item, model_item_type)) {
        gint role = 0; // GTK_MENU_TRACKER_ITEM_ROLE_NORMAL
        g_autofree gchar *accel = NULL;
        g_object_get(item, "action-role", &role, "accel", &accel, NULL);
        if (role == 0) {
            dbusmenu_menuitem_property_remove(mi, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE);
            dbusmenu_menuitem_property_remove(mi, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE);
        }
        if (accel) {
            gtk_accelerator_parse(accel, &keyval, &mods);
        }
    } else if (gtk_bin_get_child(GTK_BIN(item))) {
        find_accel(gtk_bin_get_child(GTK_BIN(item)), &keyval, &mods);
    }
    if (keyval) {
        dbusmenu_menuitem_property_set_shortcut(mi, keyval, mods);
    }

    GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
    if (submenu) {
        if (!g_signal_handler_find(mi, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL, on_about_to_show, item)) {
            g_signal_connect_object(mi, DBUSMENU_MENUITEM_SIGNAL_ABOUT_TO_SHOW, G_CALLBACK(on_about_to_show), item, 0);
        }
        fix_menu(submenu);
    }
}

static void fix_menu(GtkWidget *menu)
{
    gtk_container_foreach(GTK_CONTAINER(menu), fix_item, NULL);
}

static gboolean fix_export(gpointer data)
{
    Export *e = data;
    e->fix_source = 0;
    fix_menu(e->bar);
    return G_SOURCE_REMOVE;
}

// The parser added or recreated items
static void on_layout_updated(DbusmenuServer *server, guint revision, gint parent, gpointer data)
{
    (void)server;
    (void)revision;
    (void)parent;
    Export *e = data;
    if (!e->fix_source) {
        e->fix_source = g_idle_add(fix_export, e);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Exporting menu bars

static void attach(GtkWidget *bar, GtkWidget *window)
{
    Export *e = g_new0(Export, 1);
    e->bar = bar;
    e->window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&e->window);
    e->path = g_strdup_printf("/MenuBar/%u", ++last_path_id);
    e->root = dbusmenu_gtk_parse_menu_structure(bar);
    e->server = dbusmenu_server_new(e->path);
    dbusmenu_server_set_root(e->server, e->root);
    fix_menu(bar);
    g_signal_connect(e->server, DBUSMENU_SERVER_SIGNAL_LAYOUT_UPDATED, G_CALLBACK(on_layout_updated), e);

    g_object_set_qdata(G_OBJECT(bar), bar_quark, e);
    g_object_set_qdata(G_OBJECT(window), window_quark, e);
    g_debug("exporting menu bar %p of window %p as %s", (void *)bar, (void *)window, e->path);

    // These stay connected for good too
    if (!g_signal_handler_find(window, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_window_map, NULL)) {
        g_signal_connect_after(window, "realize", G_CALLBACK(on_window_realize), NULL);
        g_signal_connect_after(window, "map", G_CALLBACK(on_window_map), NULL);
        g_signal_connect(window, "unmap", G_CALLBACK(on_window_unmap), NULL);
    }
    if (!g_signal_handler_find(bar, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_bar_insert, NULL)) {
        g_signal_connect_after(bar, "insert", G_CALLBACK(on_bar_insert), NULL);
    }
    gtk_container_foreach(GTK_CONTAINER(bar), watch_item, NULL);

    if (gtk_widget_get_realized(window)) {
        announce(e);
    }
    gtk_widget_queue_resize(bar);
}

static void detach(GtkWidget *bar)
{
    Export *e = bar_export(bar);
    if (!e) {
        return;
    }
    g_debug("no longer exporting menu bar %p as %s", (void *)bar, e->path);

    withdraw(e);
    if (e->window) {
        if (window_export(e->window) == e) {
            g_object_set_qdata(G_OBJECT(e->window), window_quark, NULL);
        }
        g_object_remove_weak_pointer(G_OBJECT(e->window), (gpointer *)&e->window);
    }
    g_object_set_qdata(G_OBJECT(bar), bar_quark, NULL);

    if (e->fix_source) {
        g_source_remove(e->fix_source);
    }
    g_signal_handlers_disconnect_by_data(e->server, e);
    g_object_unref(e->server);
    g_object_unref(e->root);
    g_free(e->path);
    g_free(e);

    gtk_widget_queue_resize(bar);
}

static gboolean suitable_window(GtkWidget *window)
{
    if (!GTK_IS_WINDOW(window) || !gtk_widget_is_toplevel(window) || GTK_IS_OFFSCREEN_WINDOW(window)
        || gtk_window_get_window_type(GTK_WINDOW(window)) != GTK_WINDOW_TOPLEVEL) {
        return FALSE;
    }
#ifdef GDK_WINDOWING_X11
    if (GTK_IS_PLUG(window)) {
        return FALSE;
    }
#endif
    return TRUE;
}

static void update_bar(GtkWidget *bar)
{
    GtkWidget *window = gtk_widget_get_toplevel(bar);
    Export *e = bar_export(bar);
    if (e && e->window == window) {
        return;
    }
    detach(bar);

    // One global menu per window: if there are several menu bars, the others stay in the window
    if (suitable_window(window) && !window_export(window) && enabled(gtk_widget_get_display(bar))) {
        attach(bar, window);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// GtkMenuBar overrides: track which window a menu bar is in, and take no space while it's exported

static void kam_get_preferred_width(GtkWidget *widget, gint *minimum, gint *natural)
{
    orig_get_preferred_width(widget, minimum, natural);
    if (bar_export(widget)) {
        *minimum = *natural = 0;
    }
}

static void kam_get_preferred_height(GtkWidget *widget, gint *minimum, gint *natural)
{
    orig_get_preferred_height(widget, minimum, natural);
    if (bar_export(widget)) {
        *minimum = *natural = 0;
    }
}

static void kam_get_preferred_width_for_height(GtkWidget *widget, gint height, gint *minimum, gint *natural)
{
    orig_get_preferred_width_for_height(widget, height, minimum, natural);
    if (bar_export(widget)) {
        *minimum = *natural = 0;
    }
}

static void kam_get_preferred_height_for_width(GtkWidget *widget, gint width, gint *minimum, gint *natural)
{
    orig_get_preferred_height_for_width(widget, width, minimum, natural);
    if (bar_export(widget)) {
        *minimum = *natural = 0;
    }
}

static void kam_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    if (!bar_export(widget)) {
        orig_size_allocate(widget, allocation);
        return;
    }
    // Just store an empty allocation (GtkMenuBar would lay out its items in it), and move the menu bar's own
    // GdkWindow out of sight. It stays mapped, which GTK requires for the mnemonics of its items to work.
    GtkAllocation empty = {allocation->x, allocation->y, 0, 0};
    widget_size_allocate(widget, &empty);
    if (gtk_widget_get_realized(widget) && gtk_widget_get_has_window(widget)) {
        gdk_window_move_resize(gtk_widget_get_window(widget), -1, -1, 1, 1);
    }
}

static gboolean kam_draw(GtkWidget *widget, cairo_t *cr)
{
    return bar_export(widget) ? FALSE : orig_draw(widget, cr);
}

static void kam_hierarchy_changed(GtkWidget *widget, GtkWidget *old_toplevel)
{
    if (orig_hierarchy_changed) {
        orig_hierarchy_changed(widget, old_toplevel);
    }
    update_bar(widget);
}

// Also the already registered subclasses: they copied GtkMenuBar's vfuncs when their class was initialized
static void override_menu_bar_class(GType type)
{
    GtkWidgetClass *klass = g_type_class_ref(type);

    if (klass->get_preferred_width == orig_get_preferred_width) {
        klass->get_preferred_width = kam_get_preferred_width;
    }
    if (klass->get_preferred_height == orig_get_preferred_height) {
        klass->get_preferred_height = kam_get_preferred_height;
    }
    if (klass->get_preferred_width_for_height == orig_get_preferred_width_for_height) {
        klass->get_preferred_width_for_height = kam_get_preferred_width_for_height;
    }
    if (klass->get_preferred_height_for_width == orig_get_preferred_height_for_width) {
        klass->get_preferred_height_for_width = kam_get_preferred_height_for_width;
    }
    if (klass->size_allocate == orig_size_allocate) {
        klass->size_allocate = kam_size_allocate;
    }
    if (klass->draw == orig_draw) {
        klass->draw = kam_draw;
    }
    if (klass->hierarchy_changed == orig_hierarchy_changed) {
        klass->hierarchy_changed = kam_hierarchy_changed;
    }

    guint n = 0;
    GType *children = g_type_children(type, &n);
    for (guint i = 0; i < n; i++) {
        override_menu_bar_class(children[i]);
    }
    g_free(children);
    g_type_class_unref(klass);
}

// ---------------------------------------------------------------------------------------------------------------

// With a global menu, Plasma's GTK menu proxy sets gtk-shell-shows-menubar in GTK's settings.ini (GDK on Wayland
// ignores it, KWin doesn't have the GNOME protocol it goes by there). GtkApplicationWindow then leaves out the menu
// bar it would make from the app's GMenuModel (gtk_application_set_menubar()) and only the proxy shows that menu.
// Let GtkApplicationWindow make its menu bar and export it like any other, so it gets Alt+<mnemonic> and F10 too.
static void on_display_opened(GdkDisplayManager *manager, GdkDisplay *display, gpointer data)
{
    (void)manager;
    (void)data;
    GtkSettings *settings = gtk_settings_get_for_screen(gdk_display_get_default_screen(display));
    gboolean shell_shows_menubar = FALSE;
    g_object_get(settings, "gtk-shell-shows-menubar", &shell_shows_menubar, NULL);
    if (shell_shows_menubar && enabled(display)) {
        g_object_set(settings, "gtk-shell-shows-menubar", FALSE, NULL);
    }
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module)
{
    // GTK classes point into this module from now on
    g_module_make_resident(module);
    return NULL;
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
    (void)argc;
    (void)argv;

    static gboolean initialized = FALSE;
    if (initialized) {
        return;
    }
    initialized = TRUE;

    bar_quark = g_quark_from_static_string("kde-appmenu-gtk-module-bar");
    window_quark = g_quark_from_static_string("kde-appmenu-gtk-module-window");

    GtkWidgetClass *widget_class = g_type_class_ref(GTK_TYPE_WIDGET);
    widget_size_allocate = widget_class->size_allocate;
    g_type_class_unref(widget_class);

    GtkWidgetClass *menu_bar_class = g_type_class_ref(GTK_TYPE_MENU_BAR);
    orig_get_preferred_width = menu_bar_class->get_preferred_width;
    orig_get_preferred_height = menu_bar_class->get_preferred_height;
    orig_get_preferred_width_for_height = menu_bar_class->get_preferred_width_for_height;
    orig_get_preferred_height_for_width = menu_bar_class->get_preferred_height_for_width;
    orig_size_allocate = menu_bar_class->size_allocate;
    orig_draw = menu_bar_class->draw;
    orig_hierarchy_changed = menu_bar_class->hierarchy_changed;
    override_menu_bar_class(GTK_TYPE_MENU_BAR);
    g_type_class_unref(menu_bar_class);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_key_snooper_install(key_snooper, NULL);
    G_GNUC_END_IGNORE_DEPRECATIONS

    GdkDisplayManager *manager = gdk_display_manager_get();
    g_signal_connect(manager, "display-opened", G_CALLBACK(on_display_opened), NULL);
    GdkDisplay *display = gdk_display_manager_get_default_display(manager);
    if (display) {
        on_display_opened(manager, display, NULL);
    }
}
