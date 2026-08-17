#include "wayland_picker.h"

#ifdef HAVE_WAYLAND

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT 0x111
#endif
#ifndef KEY_ESC
#define KEY_ESC 1
#endif

typedef struct WaylandPickerData WaylandPickerData;

typedef struct {
    WaylandPickerData *picker_ctx;
    struct wl_output *output;
    uint32_t global_id;

    // Screencopy buffer
    struct zwlr_screencopy_frame_v1 *frame;
    struct wl_buffer *screencopy_buffer;
    void *screencopy_shm_data;
    uint32_t buffer_format;
    uint32_t buffer_width;
    uint32_t buffer_height;
    uint32_t buffer_stride;
    gboolean frame_ready;
    gboolean frame_y_invert;

    // Layer surface overlay
    struct wl_surface *layer_surface;
    struct zwlr_layer_surface_v1 *zwlr_layer_surface;
    struct wl_buffer *layer_buffer;
    void *layer_shm_data;

    double pointer_x;
    double pointer_y;
} WaylandOutputData;

struct WaylandPickerData {
    VaxpColorPickedCallback callback;
    gpointer user_data;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct zwlr_screencopy_manager_v1 *screencopy_mgr;
    struct zwlr_layer_shell_v1 *layer_shell;

    GPtrArray *outputs;

    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;

    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;

    WaylandOutputData *active_output;
    gboolean picked;
    gboolean cancelled;
    gchar *picked_hex;
};

static int create_anonymous_file(off_t size) {
    int fd = -1;
#if defined(MFD_CLOEXEC)
    fd = memfd_create("vcpicker-shm", MFD_CLOEXEC);
#endif
    if (fd < 0) {
        char template[] = "/tmp/vcpicker-shm-XXXXXX";
        fd = mkstemp(template);
        if (fd >= 0) {
            unlink(template);
        }
    }
    if (fd >= 0) {
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static void *allocate_shm_buffer(struct wl_shm *shm, uint32_t width, uint32_t height, uint32_t stride, uint32_t format, struct wl_buffer **out_buffer) {
    size_t size = (size_t)stride * height;
    int fd = create_anonymous_file((off_t)size);
    if (fd < 0) return NULL;

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)width, (int32_t)height, (int32_t)stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);

    *out_buffer = buffer;
    return data;
}

static void frame_handle_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame,
                                uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    (void)frame;
    WaylandOutputData *out = data;
    out->buffer_format = format;
    out->buffer_width = width;
    out->buffer_height = height;
    out->buffer_stride = stride;

    out->screencopy_shm_data = allocate_shm_buffer(out->picker_ctx->shm, width, height, stride, format, &out->screencopy_buffer);
    if (out->screencopy_buffer) {
        zwlr_screencopy_frame_v1_copy(frame, out->screencopy_buffer);
    }
}

static void frame_handle_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {
    (void)frame;
    WaylandOutputData *out = data;
    out->frame_y_invert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void frame_handle_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
                               uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    (void)frame; (void)tv_sec_hi; (void)tv_sec_lo; (void)tv_nsec;
    WaylandOutputData *out = data;
    out->frame_ready = TRUE;
}

static void frame_handle_failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    (void)frame;
    WaylandOutputData *out = data;
    out->picker_ctx->cancelled = TRUE;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
};

static void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                            uint32_t serial, uint32_t width, uint32_t height) {
    WaylandOutputData *out = data;

    if (width == 0) width = out->buffer_width > 0 ? out->buffer_width : 1920;
    if (height == 0) height = out->buffer_height > 0 ? out->buffer_height : 1080;

    if (!out->layer_buffer) {
        uint32_t stride = width * 4;
        out->layer_shm_data = allocate_shm_buffer(out->picker_ctx->shm, width, height, stride, WL_SHM_FORMAT_ARGB8888, &out->layer_buffer);
        if (out->layer_shm_data) {
            memset(out->layer_shm_data, 0, (size_t)stride * height);
        }
    }

    if (out->layer_buffer) {
        wl_surface_attach(out->layer_surface, out->layer_buffer, 0, 0);
        wl_surface_damage(out->layer_surface, 0, 0, (int32_t)width, (int32_t)height);
    }

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    wl_surface_commit(out->layer_surface);
}

static void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    WaylandOutputData *out = data;
    out->picker_ctx->cancelled = TRUE;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static void extract_color_from_output(WaylandOutputData *out, int x, int y) {
    if (!out || !out->screencopy_shm_data || out->buffer_width == 0 || out->buffer_height == 0) return;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= out->buffer_width) x = (int)out->buffer_width - 1;
    if ((uint32_t)y >= out->buffer_height) y = (int)out->buffer_height - 1;

    if (out->frame_y_invert) {
        y = (int)out->buffer_height - 1 - y;
    }

    uint8_t *pixel_ptr = (uint8_t *)out->screencopy_shm_data + (size_t)y * out->buffer_stride + (size_t)x * 4;
    uint32_t pixel = *(uint32_t *)pixel_ptr;

    uint8_t r = 0, g = 0, b = 0;

    switch (out->buffer_format) {
        case WL_SHM_FORMAT_XBGR8888:
        case WL_SHM_FORMAT_ABGR8888:
            r = (pixel) & 0xFF;
            g = (pixel >> 8) & 0xFF;
            b = (pixel >> 16) & 0xFF;
            break;
        case WL_SHM_FORMAT_ARGB8888:
        case WL_SHM_FORMAT_XRGB8888:
        default:
            b = (pixel) & 0xFF;
            g = (pixel >> 8) & 0xFF;
            r = (pixel >> 16) & 0xFF;
            break;
    }

    out->picker_ctx->picked_hex = g_strdup_printf("#%02X%02X%02X", r, g, b);
}

static WaylandOutputData *find_output_for_surface(WaylandPickerData *ctx, struct wl_surface *surface) {
    if (!ctx->outputs || !surface) return NULL;
    for (guint i = 0; i < ctx->outputs->len; i++) {
        WaylandOutputData *out = g_ptr_array_index(ctx->outputs, i);
        if (out->layer_surface == surface) {
            return out;
        }
    }
    return NULL;
}

static void set_crosshair_cursor(WaylandPickerData *ctx, struct wl_pointer *pointer, uint32_t serial) {
    if (!ctx || !ctx->compositor || !ctx->shm || !pointer) return;

    if (!ctx->cursor_theme) {
        ctx->cursor_theme = wl_cursor_theme_load(NULL, 24, ctx->shm);
    }
    if (!ctx->cursor_theme) return;

    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "crosshair");
    if (!cursor) {
        cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "color-picker");
    }
    if (!cursor) {
        cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "cell");
    }
    if (!cursor) {
        cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "left_ptr");
    }

    if (cursor && cursor->image_count > 0) {
        struct wl_cursor_image *image = cursor->images[0];
        struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
        if (!ctx->cursor_surface) {
            ctx->cursor_surface = wl_compositor_create_surface(ctx->compositor);
        }
        if (ctx->cursor_surface && buffer) {
            wl_surface_attach(ctx->cursor_surface, buffer, 0, 0);
            wl_surface_damage(ctx->cursor_surface, 0, 0, (int32_t)image->width, (int32_t)image->height);
            wl_surface_commit(ctx->cursor_surface);

            wl_pointer_set_cursor(pointer, serial, ctx->cursor_surface, (int32_t)image->hotspot_x, (int32_t)image->hotspot_y);
        }
    }
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    WaylandPickerData *ctx = data;
    WaylandOutputData *out = find_output_for_surface(ctx, surface);
    if (out) {
        ctx->active_output = out;
        out->pointer_x = wl_fixed_to_double(sx);
        out->pointer_y = wl_fixed_to_double(sy);
    }
    set_crosshair_cursor(ctx, pointer, serial);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    WaylandPickerData *ctx = data;
    if (ctx->active_output) {
        ctx->active_output->pointer_x = wl_fixed_to_double(sx);
        ctx->active_output->pointer_y = wl_fixed_to_double(sy);
    }
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    WaylandPickerData *ctx = data;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == BTN_LEFT || button == 1) {
            if (ctx->active_output) {
                extract_color_from_output(ctx->active_output,
                                         (int)ctx->active_output->pointer_x,
                                         (int)ctx->active_output->pointer_y);
            } else if (ctx->outputs->len > 0) {
                WaylandOutputData *first_out = g_ptr_array_index(ctx->outputs, 0);
                extract_color_from_output(first_out, (int)first_out->pointer_x, (int)first_out->pointer_y);
            }
            ctx->picked = TRUE;
        } else {
            ctx->cancelled = TRUE;
        }
    }
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}
static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
    (void)data; (void)pointer;
}
static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
    (void)data; (void)pointer; (void)axis_source;
}
static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size) {
    (void)data; (void)keyboard; (void)format; (void)size;
    if (fd >= 0) close(fd);
}
static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                  struct wl_surface *surface, struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}
static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
}
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                uint32_t time, uint32_t key, uint32_t state) {
    (void)keyboard; (void)serial; (void)time;
    WaylandPickerData *ctx = data;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && key == KEY_ESC) {
        ctx->cancelled = TRUE;
    }
}
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                       uint32_t mods_depressed, uint32_t mods_latched,
                                       uint32_t mods_locked, uint32_t group) {
    (void)data; (void)keyboard; (void)serial; (void)mods_depressed; (void)mods_latched; (void)mods_locked; (void)group;
}
static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

static void free_output_data(gpointer p) {
    WaylandOutputData *out = p;
    if (!out) return;
    if (out->zwlr_layer_surface) zwlr_layer_surface_v1_destroy(out->zwlr_layer_surface);
    if (out->layer_surface) wl_surface_destroy(out->layer_surface);
    if (out->frame) zwlr_screencopy_frame_v1_destroy(out->frame);
    if (out->layer_buffer) wl_buffer_destroy(out->layer_buffer);
    if (out->layer_shm_data && out->buffer_width && out->buffer_height) {
        munmap(out->layer_shm_data, (size_t)out->buffer_width * 4 * out->buffer_height);
    }
    if (out->screencopy_buffer) wl_buffer_destroy(out->screencopy_buffer);
    if (out->screencopy_shm_data && out->buffer_stride && out->buffer_height) {
        munmap(out->screencopy_shm_data, (size_t)out->buffer_stride * out->buffer_height);
    }
    if (out->output) wl_output_destroy(out->output);
    g_free(out);
}

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
                                   const char *interface, uint32_t version) {
    (void)version;
    WaylandPickerData *ctx = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output = wl_registry_bind(registry, id, &wl_output_interface, 1);
        WaylandOutputData *out = g_new0(WaylandOutputData, 1);
        out->picker_ctx = ctx;
        out->output = output;
        out->global_id = id;
        g_ptr_array_add(ctx->outputs, out);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        ctx->screencopy_mgr = wl_registry_bind(registry, id, &zwlr_screencopy_manager_v1_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

typedef struct {
    VaxpColorPickedCallback callback;
    gpointer user_data;
    gchar *hex_color;
} IdleCbData;

static gboolean idle_deliver_color(gpointer user_data) {
    IdleCbData *cb_data = user_data;
    if (cb_data->callback) {
        cb_data->callback(cb_data->hex_color, cb_data->user_data);
    }
    g_free(cb_data->hex_color);
    g_free(cb_data);
    return G_SOURCE_REMOVE;
}

static gpointer wayland_picker_thread_func(gpointer user_data) {
    WaylandPickerData *ctx = user_data;

    for (guint i = 0; i < ctx->outputs->len; i++) {
        WaylandOutputData *out = g_ptr_array_index(ctx->outputs, i);

        out->frame = zwlr_screencopy_manager_v1_capture_output(ctx->screencopy_mgr, 1, out->output);
        zwlr_screencopy_frame_v1_add_listener(out->frame, &frame_listener, out);

        out->layer_surface = wl_compositor_create_surface(ctx->compositor);
        out->zwlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            ctx->layer_shell, out->layer_surface, out->output,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "vcpicker-picker"
        );

        zwlr_layer_surface_v1_add_listener(out->zwlr_layer_surface, &layer_surface_listener, out);
        zwlr_layer_surface_v1_set_anchor(out->zwlr_layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(out->zwlr_layer_surface, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(out->zwlr_layer_surface, 1);
        wl_surface_commit(out->layer_surface);
    }

    if (ctx->seat) {
        ctx->pointer = wl_seat_get_pointer(ctx->seat);
        if (ctx->pointer) {
            wl_pointer_add_listener(ctx->pointer, &pointer_listener, ctx);
        }
        ctx->keyboard = wl_seat_get_keyboard(ctx->seat);
        if (ctx->keyboard) {
            wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
        }
    }

    wl_display_roundtrip(ctx->display);

    while (!ctx->picked && !ctx->cancelled && wl_display_dispatch(ctx->display) != -1) {
    }

    if (ctx->pointer) wl_pointer_destroy(ctx->pointer);
    if (ctx->keyboard) wl_keyboard_destroy(ctx->keyboard);
    if (ctx->cursor_surface) wl_surface_destroy(ctx->cursor_surface);
    if (ctx->cursor_theme) wl_cursor_theme_destroy(ctx->cursor_theme);

    g_ptr_array_unref(ctx->outputs);

    if (ctx->screencopy_mgr) zwlr_screencopy_manager_v1_destroy(ctx->screencopy_mgr);
    if (ctx->layer_shell) zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    if (ctx->compositor) wl_compositor_destroy(ctx->compositor);
    if (ctx->shm) wl_shm_destroy(ctx->shm);
    if (ctx->seat) wl_seat_destroy(ctx->seat);
    if (ctx->registry) wl_registry_destroy(ctx->registry);

    wl_display_flush(ctx->display);
    wl_display_disconnect(ctx->display);

    IdleCbData *cb_data = g_new0(IdleCbData, 1);
    cb_data->callback = ctx->callback;
    cb_data->user_data = ctx->user_data;
    cb_data->hex_color = ctx->picked_hex;

    g_idle_add(idle_deliver_color, cb_data);
    g_free(ctx);

    return NULL;
}

gboolean vaxp_wayland_pick_color(VaxpColorPickedCallback callback, gpointer user_data) {
    WaylandPickerData *ctx = g_new0(WaylandPickerData, 1);
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->outputs = g_ptr_array_new_with_free_func(free_output_data);

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) {
        g_ptr_array_unref(ctx->outputs);
        g_free(ctx);
        return FALSE;
    }

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->shm || !ctx->screencopy_mgr || !ctx->layer_shell || ctx->outputs->len == 0) {
        if (ctx->screencopy_mgr) zwlr_screencopy_manager_v1_destroy(ctx->screencopy_mgr);
        if (ctx->layer_shell) zwlr_layer_shell_v1_destroy(ctx->layer_shell);
        if (ctx->compositor) wl_compositor_destroy(ctx->compositor);
        if (ctx->shm) wl_shm_destroy(ctx->shm);
        if (ctx->seat) wl_seat_destroy(ctx->seat);
        if (ctx->registry) wl_registry_destroy(ctx->registry);
        g_ptr_array_unref(ctx->outputs);
        wl_display_disconnect(ctx->display);
        g_free(ctx);
        return FALSE;
    }

    g_message("Wayland picker: detected %u display output(s). Activating multi-monitor overlays...", ctx->outputs->len);

    GThread *thread = g_thread_new("wayland-picker", wayland_picker_thread_func, ctx);
    g_thread_unref(thread);
    return TRUE;
}

#else

gboolean vaxp_wayland_pick_color(VaxpColorPickedCallback callback, gpointer user_data) {
    (void)callback;
    (void)user_data;
    return FALSE;
}

#endif
