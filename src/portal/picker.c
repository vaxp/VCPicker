#include "picker.h"
#include "wayland_picker.h"
#include <gio/gio.h>
#include <gtk/gtk.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#endif

static guint signal_id = 0;
static VaxpColorPickedCallback current_callback = NULL;
static gpointer current_user_data = NULL;
static gchar *current_request_path = NULL;

#ifdef HAVE_X11
static gboolean fallback_callback_idle(gpointer data) {
    gchar *hex = (gchar *)data;
    if (current_callback) {
        current_callback(hex, current_user_data);
    }
    g_free(hex);
    current_callback = NULL;
    current_user_data = NULL;
    return G_SOURCE_REMOVE;
}

static gpointer pick_color_fallback_thread(gpointer data) {
    (void)data;
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        g_warning("Could not open X display for fallback color picker");
        current_callback = NULL;
        current_user_data = NULL;
        return NULL;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    Cursor cursor = XCreateFontCursor(dpy, XC_crosshair);

    if (XGrabPointer(dpy, root, False,
                     ButtonPressMask, GrabModeAsync,
                     GrabModeAsync, root, cursor, CurrentTime) != GrabSuccess) {
        g_warning("Could not grab pointer for X11 fallback");
        XFreeCursor(dpy, cursor);
        XCloseDisplay(dpy);
        current_callback = NULL;
        current_user_data = NULL;
        return NULL;
    }

    XEvent ev;
    gboolean picked = FALSE;
    gchar *hex = NULL;

    while (!picked) {
        XNextEvent(dpy, &ev);
        if (ev.type == ButtonPress && ev.xbutton.button == 1) {
            int x = ev.xbutton.x_root;
            int y = ev.xbutton.y_root;
            
            XImage *image = XGetImage(dpy, root, x, y, 1, 1, AllPlanes, ZPixmap);
            if (image) {
                unsigned long pixel = XGetPixel(image, 0, 0);
                XColor color;
                color.pixel = pixel;
                XQueryColor(dpy, DefaultColormap(dpy, screen), &color);
                
                hex = g_strdup_printf("#%02X%02X%02X",
                                      color.red >> 8,
                                      color.green >> 8,
                                      color.blue >> 8);
                XDestroyImage(image);
            }
            picked = TRUE;
        } else if (ev.type == ButtonPress) {
            // Cancel on right/middle click
            picked = TRUE;
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XFreeCursor(dpy, cursor);
    XCloseDisplay(dpy);

    if (hex) {
        g_idle_add(fallback_callback_idle, hex);
    } else {
        current_callback = NULL;
        current_user_data = NULL;
    }

    return NULL;
}
#endif

static void
on_response_signal (GDBusConnection *connection,
                    const gchar     *sender_name,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
    (void)sender_name; (void)object_path; (void)interface_name; (void)signal_name; (void)user_data;
    guint32 response = 1;
    GVariant *results = NULL;

    if (g_variant_n_children (parameters) >= 2) {
        g_variant_get (parameters, "(u@a{sv})", &response, &results);
    }

    if (response == 0 && results != NULL) {
        GVariant *color_var = g_variant_lookup_value(results, "color", G_VARIANT_TYPE("(ddd)"));
        if (color_var) {
            double r, g, b;
            g_variant_get(color_var, "(ddd)", &r, &g, &b);
            gchar *hex = g_strdup_printf("#%02X%02X%02X", 
                (int)(r * 255.0 + 0.5), 
                (int)(g * 255.0 + 0.5), 
                (int)(b * 255.0 + 0.5));
            
            if (current_callback) {
                current_callback(hex, current_user_data);
            }
            g_free(hex);
            g_variant_unref(color_var);
        }
    }

    if (results) g_variant_unref (results);

    if (signal_id != 0) {
        g_dbus_connection_signal_unsubscribe (connection, signal_id);
        signal_id = 0;
    }
    current_callback = NULL;
    current_user_data = NULL;
    g_clear_pointer(&current_request_path, g_free);
}

static void
pick_color_ready_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
    (void)user_data;
    GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
    GError *error = NULL;
    GVariant *result;
    gchar *request_path = NULL;

    result = g_dbus_connection_call_finish (connection, res, &error);
    if (!result) {
        g_message ("PickColor via Desktop Portal failed: %s. Trying X11 fallback if available.", error->message);
        g_error_free (error);
        
#ifdef HAVE_X11
        if (getenv("DISPLAY") != NULL) {
            g_thread_unref(g_thread_new("x11-pick-fallback", pick_color_fallback_thread, NULL));
            return;
        }
#endif
        g_warning ("No usable fallback available for this session.");
        current_callback = NULL;
        current_user_data = NULL;
        return;
    }

    g_variant_get (result, "(o)", &request_path);
    current_request_path = g_strdup(request_path);

    signal_id = g_dbus_connection_signal_subscribe (
        connection,
        "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request",
        "Response",
        request_path,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
        on_response_signal,
        NULL,
        NULL
    );

    g_free (request_path);
    g_variant_unref (result);
}

void vaxp_portal_pick_color(VaxpColorPickedCallback callback, gpointer user_data) {
    if (current_callback != NULL || signal_id != 0) {
        g_warning("A color picking request is already in progress");
        return;
    }

    const char *session_type = getenv("XDG_SESSION_TYPE");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    const char *x11_display = getenv("DISPLAY");

    g_message("VCPicker Environment: XDG_SESSION_TYPE='%s', WAYLAND_DISPLAY='%s', DISPLAY='%s'",
              session_type ? session_type : "unset",
              wayland_display ? wayland_display : "unset",
              x11_display ? x11_display : "unset");

#ifdef HAVE_WAYLAND
    gboolean is_wayland_env = (wayland_display != NULL) || (session_type && g_strcmp0(session_type, "wayland") == 0);
    if (is_wayland_env) {
        g_message("Wayland environment detected. Attempting native Wayland protocols (wlr-screencopy / wlr-layer-shell)...");
        if (vaxp_wayland_pick_color(callback, user_data)) {
            g_message("Native Wayland protocol color picker started.");
            return;
        }
        g_message("Native Wayland protocols not supported by compositor. Falling back to XDG Desktop Portal.");
    }
#endif

    g_message("Initiating XDG Desktop Portal PickColor...");

    g_autoptr(GError) error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!connection) {
        g_warning("Failed to connect to session bus: %s", error->message);
#ifdef HAVE_X11
        if (x11_display != NULL) {
            g_message("Attempting X11 fallback...");
            current_callback = callback;
            current_user_data = user_data;
            g_thread_unref(g_thread_new("x11-pick-fallback", pick_color_fallback_thread, NULL));
        }
#endif
        return;
    }

    current_callback = callback;
    current_user_data = user_data;

    GVariantBuilder opt_builder;
    g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

    g_dbus_connection_call (
        connection,
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot",
        "PickColor",
        g_variant_new ("(sa{sv})", "", &opt_builder),
        G_VARIANT_TYPE ("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        pick_color_ready_cb,
        NULL
    );
    
    g_object_unref(connection);
}
