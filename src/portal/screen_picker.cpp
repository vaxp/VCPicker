#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "screen_picker.hpp"
#include "../core/color_engine.hpp"

#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#endif

#ifdef HAVE_WAYLAND
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"

// Avoid C++ reserved keyword conflict with wayland-scanner generated identifier
#define namespace _wl_namespace
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT 0x111
#endif
#ifndef KEY_ESC
#define KEY_ESC 1
#endif

namespace {

struct WaylandContext;

int create_shm_fd(size_t size) {
    int fd = memfd_create("vcpicker-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        char temp_path[] = "/tmp/vcpicker-shm-XXXXXX";
        fd = mkstemp(temp_path);
        if (fd >= 0) unlink(temp_path);
    }
    if (fd >= 0) {
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

struct OutputData {
    WaylandContext* ctx = nullptr;
    struct wl_output* output = nullptr;
    uint32_t global_id = 0;

    struct zwlr_screencopy_frame_v1* frame = nullptr;
    struct wl_buffer* screencopy_buffer = nullptr;
    uint32_t* screencopy_shm_data = nullptr;
    uint32_t buffer_format = 0;
    uint32_t buffer_width = 0;
    uint32_t buffer_height = 0;
    uint32_t buffer_stride = 0;
    bool frame_ready = false;
    bool frame_y_invert = false;

    struct wl_surface* layer_surface = nullptr;
    struct zwlr_layer_surface_v1* zwlr_layer_surface = nullptr;
    struct wl_buffer* layer_buffer = nullptr;
    uint32_t* layer_shm_data = nullptr;

    double pointer_x = 0;
    double pointer_y = 0;

    int prev_lens_x = 0;
    int prev_lens_y = 0;
    int prev_lens_w = 0;
    int prev_lens_h = 0;

    ~OutputData() {
        if (zwlr_layer_surface) zwlr_layer_surface_v1_destroy(zwlr_layer_surface);
        if (layer_surface) wl_surface_destroy(layer_surface);
        if (frame) zwlr_screencopy_frame_v1_destroy(frame);
        if (layer_buffer) wl_buffer_destroy(layer_buffer);
        if (layer_shm_data && buffer_width && buffer_height) {
            munmap(layer_shm_data, static_cast<size_t>(buffer_width) * 4 * buffer_height);
        }
        if (screencopy_buffer) wl_buffer_destroy(screencopy_buffer);
        if (screencopy_shm_data && buffer_stride && buffer_height) {
            munmap(screencopy_shm_data, static_cast<size_t>(buffer_stride) * buffer_height);
        }
        if (output) wl_output_destroy(output);
    }
};

struct WaylandContext {
    vcpicker::ColorPickedCallback callback;
    struct wl_display* display = nullptr;
    struct wl_registry* registry = nullptr;
    struct wl_compositor* compositor = nullptr;
    struct wl_shm* shm = nullptr;
    struct wl_seat* seat = nullptr;
    struct zwlr_screencopy_manager_v1* screencopy_mgr = nullptr;
    struct zwlr_layer_shell_v1* layer_shell = nullptr;

    std::vector<std::unique_ptr<OutputData>> outputs;
    OutputData* active_output = nullptr;

    struct wl_pointer* pointer = nullptr;
    struct wl_keyboard* keyboard = nullptr;

    struct wl_cursor_theme* cursor_theme = nullptr;
    struct wl_surface* cursor_surface = nullptr;

    std::atomic<bool> picked{false};
    std::atomic<bool> cancelled{false};
    std::string picked_hex;

    void cleanup() {
        if (pointer) { wl_pointer_destroy(pointer); pointer = nullptr; }
        if (keyboard) { wl_keyboard_destroy(keyboard); keyboard = nullptr; }
        if (cursor_surface) { wl_surface_destroy(cursor_surface); cursor_surface = nullptr; }
        if (cursor_theme) { wl_cursor_theme_destroy(cursor_theme); cursor_theme = nullptr; }
        outputs.clear();
        if (screencopy_mgr) { zwlr_screencopy_manager_v1_destroy(screencopy_mgr); screencopy_mgr = nullptr; }
        if (layer_shell) { zwlr_layer_shell_v1_destroy(layer_shell); layer_shell = nullptr; }
        if (compositor) { wl_compositor_destroy(compositor); compositor = nullptr; }
        if (shm) { wl_shm_destroy(shm); shm = nullptr; }
        if (seat) { wl_seat_destroy(seat); seat = nullptr; }
        if (registry) { wl_registry_destroy(registry); registry = nullptr; }
        if (display) {
            wl_display_flush(display);
            wl_display_disconnect(display);
            display = nullptr;
        }
    }
};

void* allocate_shm_buffer(struct wl_shm* shm, uint32_t width, uint32_t height, uint32_t stride, uint32_t format, struct wl_buffer** out_buffer) {
    size_t size = static_cast<size_t>(stride) * height;
    int fd = create_shm_fd(size);
    if (fd < 0) return nullptr;

    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return nullptr;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(size));
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(width), static_cast<int32_t>(height), static_cast<int32_t>(stride), format);
    wl_shm_pool_destroy(pool);
    close(fd);

    *out_buffer = buffer;
    return data;
}

void draw_magnifier(OutputData* out, int px, int py) {
    if (!out || !out->layer_shm_data || !out->screencopy_shm_data || !out->frame_ready) return;

    uint32_t width = out->buffer_width;
    uint32_t height = out->buffer_height;

    // Clear previous lens area
    if (out->prev_lens_w > 0 && out->prev_lens_h > 0) {
        int lx = out->prev_lens_x;
        int ly = out->prev_lens_y;
        int lw = out->prev_lens_w;
        int lh = out->prev_lens_h;
        for (int r = ly; r < ly + lh && r < static_cast<int>(height); r++) {
            if (r < 0) continue;
            for (int c = lx; c < lx + lw && c < static_cast<int>(width); c++) {
                if (c < 0) continue;
                out->layer_shm_data[r * width + c] = 0x00000000;
            }
        }
    }

    const int grid_size = 11;
    const int cell_size = 10;
    const int lens_size = grid_size * cell_size;
    const int border_thick = 3;
    const int total_lens_w = lens_size + 2 * border_thick;
    const int total_lens_h = lens_size + 2 * border_thick;

    int lens_x = px + 24;
    int lens_y = py + 24;
    if (lens_x + total_lens_w > static_cast<int>(width)) {
        lens_x = px - total_lens_w - 12;
    }
    if (lens_y + total_lens_h > static_cast<int>(height)) {
        lens_y = py - total_lens_h - 12;
    }
    if (lens_x < 0) lens_x = 0;
    if (lens_y < 0) lens_y = 0;

    out->prev_lens_x = lens_x;
    out->prev_lens_y = lens_y;
    out->prev_lens_w = total_lens_w;
    out->prev_lens_h = total_lens_h;

    double center_x = total_lens_w / 2.0;
    double center_y = total_lens_h / 2.0;
    double outer_radius = total_lens_w / 2.0;
    double inner_radius = outer_radius - border_thick;

    for (int r = 0; r < total_lens_h; r++) {
        int dst_y = lens_y + r;
        if (dst_y < 0 || dst_y >= static_cast<int>(height)) continue;
        for (int c = 0; c < total_lens_w; c++) {
            int dst_x = lens_x + c;
            if (dst_x < 0 || dst_x >= static_cast<int>(width)) continue;

            double dx = (c + 0.5) - center_x;
            double dy = (r + 0.5) - center_y;
            double dist_sq = dx * dx + dy * dy;

            if (dist_sq > outer_radius * outer_radius) {
                continue;
            }

            if (dist_sq >= inner_radius * inner_radius) {
                out->layer_shm_data[dst_y * width + dst_x] = 0xFF38BDF8; // Glowing cyan
                continue;
            }

            int inner_r = r - border_thick;
            int inner_c = c - border_thick;

            int sample_grid_x = inner_c / cell_size;
            int sample_grid_y = inner_r / cell_size;

            int src_x = px + (sample_grid_x - grid_size / 2);
            int src_y = py + (sample_grid_y - grid_size / 2);

            src_x = std::clamp(src_x, 0, static_cast<int>(width) - 1);
            src_y = std::clamp(src_y, 0, static_cast<int>(height) - 1);

            uint32_t sample_y = out->frame_y_invert ? (height - 1 - src_y) : src_y;
            const uint8_t* row = reinterpret_cast<const uint8_t*>(out->screencopy_shm_data) + static_cast<size_t>(sample_y) * out->buffer_stride;
            uint32_t src_pixel = *reinterpret_cast<const uint32_t*>(row + static_cast<size_t>(src_x) * 4);

            uint8_t r_val = 0, g_val = 0, b_val = 0;
            switch (out->buffer_format) {
                case WL_SHM_FORMAT_XBGR8888:
                case WL_SHM_FORMAT_ABGR8888:
                    r_val = (src_pixel) & 0xFF;
                    g_val = (src_pixel >> 8) & 0xFF;
                    b_val = (src_pixel >> 16) & 0xFF;
                    break;
                case WL_SHM_FORMAT_ARGB8888:
                case WL_SHM_FORMAT_XRGB8888:
                default:
                    b_val = (src_pixel) & 0xFF;
                    g_val = (src_pixel >> 8) & 0xFF;
                    r_val = (src_pixel >> 16) & 0xFF;
                    break;
            }

            bool is_center_cell = (sample_grid_x == grid_size / 2) && (sample_grid_y == grid_size / 2);
            int cell_local_x = inner_c % cell_size;
            int cell_local_y = inner_r % cell_size;
            bool is_cell_border = (cell_local_x == 0 || cell_local_x == cell_size - 1 || cell_local_y == 0 || cell_local_y == cell_size - 1);

            if (is_center_cell && is_cell_border) {
                out->layer_shm_data[dst_y * width + dst_x] = 0xFFFFFFFF;
            } else {
                out->layer_shm_data[dst_y * width + dst_x] = (0xFF << 24) | (r_val << 16) | (g_val << 8) | b_val;
            }
        }
    }

    wl_surface_attach(out->layer_surface, out->layer_buffer, 0, 0);
    wl_surface_damage(out->layer_surface, 0, 0, width, height);
    wl_surface_commit(out->layer_surface);
}

void extract_color_from_output(OutputData *out, int x, int y) {
    if (!out || !out->screencopy_shm_data || out->buffer_width == 0 || out->buffer_height == 0) return;

    x = std::clamp(x, 0, static_cast<int>(out->buffer_width) - 1);
    y = std::clamp(y, 0, static_cast<int>(out->buffer_height) - 1);

    if (out->frame_y_invert) {
        y = static_cast<int>(out->buffer_height) - 1 - y;
    }

    const uint8_t *pixel_ptr = reinterpret_cast<const uint8_t*>(out->screencopy_shm_data) +
                               static_cast<size_t>(y) * out->buffer_stride + static_cast<size_t>(x) * 4;
    uint32_t pixel = *reinterpret_cast<const uint32_t*>(pixel_ptr);

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

    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    if (out->ctx) {
        out->ctx->picked_hex = buf;
        out->ctx->picked = true;
    }
}

void set_crosshair_cursor(WaylandContext *ctx, struct wl_pointer *pointer, uint32_t serial) {
    if (!ctx || !ctx->compositor || !ctx->shm || !pointer) return;

    if (!ctx->cursor_theme) {
        ctx->cursor_theme = wl_cursor_theme_load(nullptr, 24, ctx->shm);
    }
    if (!ctx->cursor_theme) return;

    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "crosshair");
    if (!cursor) cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "color-picker");
    if (!cursor) cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "cell");
    if (!cursor) cursor = wl_cursor_theme_get_cursor(ctx->cursor_theme, "left_ptr");

    if (cursor && cursor->image_count > 0) {
        struct wl_cursor_image *image = cursor->images[0];
        struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
        if (!ctx->cursor_surface) {
            ctx->cursor_surface = wl_compositor_create_surface(ctx->compositor);
        }
        if (ctx->cursor_surface && buffer) {
            wl_surface_attach(ctx->cursor_surface, buffer, 0, 0);
            wl_surface_damage(ctx->cursor_surface, 0, 0, image->width, image->height);
            wl_surface_commit(ctx->cursor_surface);

            wl_pointer_set_cursor(pointer, serial, ctx->cursor_surface, image->hotspot_x, image->hotspot_y);
        }
    }
}

OutputData* find_output_for_surface(WaylandContext* ctx, struct wl_surface* surface) {
    if (!ctx || !surface) return nullptr;
    for (auto& out : ctx->outputs) {
        if (out->layer_surface == surface) return out.get();
    }
    return nullptr;
}

const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = [](void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
        auto* out = static_cast<OutputData*>(data);
        out->buffer_format = format;
        out->buffer_width = width;
        out->buffer_height = height;
        out->buffer_stride = stride;

        out->screencopy_shm_data = static_cast<uint32_t*>(
            allocate_shm_buffer(out->ctx->shm, width, height, stride, format, &out->screencopy_buffer));
        if (out->screencopy_buffer) {
            zwlr_screencopy_frame_v1_copy(frame, out->screencopy_buffer);
        }
    },
    .flags = [](void *data, struct zwlr_screencopy_frame_v1*, uint32_t flags) {
        auto* out = static_cast<OutputData*>(data);
        out->frame_y_invert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
    },
    .ready = [](void *data, struct zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) {
        auto* out = static_cast<OutputData*>(data);
        out->frame_ready = true;
    },
    .failed = [](void *data, struct zwlr_screencopy_frame_v1*) {
        auto* out = static_cast<OutputData*>(data);
        if (out->ctx) out->ctx->cancelled = true;
    },
};

const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = [](void *data, struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height) {
        auto* out = static_cast<OutputData*>(data);

        if (width == 0) width = out->buffer_width > 0 ? out->buffer_width : 1920;
        if (height == 0) height = out->buffer_height > 0 ? out->buffer_height : 1080;

        if (!out->layer_buffer) {
            uint32_t stride = width * 4;
            out->layer_shm_data = static_cast<uint32_t*>(
                allocate_shm_buffer(out->ctx->shm, width, height, stride, WL_SHM_FORMAT_ARGB8888, &out->layer_buffer));
            if (out->layer_shm_data) {
                std::memset(out->layer_shm_data, 0, static_cast<size_t>(stride) * height);
            }
        }

        if (out->layer_buffer) {
            wl_surface_attach(out->layer_surface, out->layer_buffer, 0, 0);
            wl_surface_damage(out->layer_surface, 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
        }

        zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
        wl_surface_commit(out->layer_surface);
    },
    .closed = [](void *data, struct zwlr_layer_surface_v1*) {
        auto* out = static_cast<OutputData*>(data);
        if (out->ctx) out->ctx->cancelled = true;
    },
};

const struct wl_pointer_listener pointer_listener = {
    .enter = [](void *data, struct wl_pointer *pointer, uint32_t serial,
                struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
        auto* ctx = static_cast<WaylandContext*>(data);
        auto* out = find_output_for_surface(ctx, surface);
        if (out) {
            ctx->active_output = out;
            out->pointer_x = wl_fixed_to_double(sx);
            out->pointer_y = wl_fixed_to_double(sy);
            draw_magnifier(out, static_cast<int>(out->pointer_x), static_cast<int>(out->pointer_y));
        }
        set_crosshair_cursor(ctx, pointer, serial);
    },
    .leave = [](void*, struct wl_pointer*, uint32_t, struct wl_surface*) {},
    .motion = [](void *data, struct wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
        auto* ctx = static_cast<WaylandContext*>(data);
        if (ctx->active_output) {
            ctx->active_output->pointer_x = wl_fixed_to_double(sx);
            ctx->active_output->pointer_y = wl_fixed_to_double(sy);
            draw_magnifier(ctx->active_output, static_cast<int>(ctx->active_output->pointer_x), static_cast<int>(ctx->active_output->pointer_y));
        }
    },
    .button = [](void *data, struct wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
        auto* ctx = static_cast<WaylandContext*>(data);
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
            if (button == BTN_LEFT || button == 1) {
                if (ctx->active_output) {
                    extract_color_from_output(ctx->active_output,
                                             static_cast<int>(ctx->active_output->pointer_x),
                                             static_cast<int>(ctx->active_output->pointer_y));
                } else if (!ctx->outputs.empty()) {
                    extract_color_from_output(ctx->outputs[0].get(),
                                             static_cast<int>(ctx->outputs[0]->pointer_x),
                                             static_cast<int>(ctx->outputs[0]->pointer_y));
                }
            } else if (button == BTN_RIGHT || button == 3) {
                ctx->cancelled = true;
            }
        }
    },
    .axis = [](void*, struct wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {},
    .frame = [](void*, struct wl_pointer*) {},
    .axis_source = [](void*, struct wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, struct wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void*, struct wl_pointer*, uint32_t, int32_t) {},
};

const struct wl_keyboard_listener keyboard_listener = {
    .keymap = [](void*, struct wl_keyboard*, uint32_t, int32_t, uint32_t) {},
    .enter = [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*, struct wl_array*) {},
    .leave = [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*) {},
    .key = [](void *data, struct wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
        auto* ctx = static_cast<WaylandContext*>(data);
        if (state == WL_KEYBOARD_KEY_STATE_PRESSED && key == KEY_ESC) {
            ctx->cancelled = true;
        }
    },
    .modifiers = [](void*, struct wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {},
    .repeat_info = [](void*, struct wl_keyboard*, int32_t, int32_t) {},
};

const struct wl_registry_listener registry_listener = {
    .global = [](void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t) {
        auto* ctx = static_cast<WaylandContext*>(data);
        if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
            ctx->compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, id, &wl_compositor_interface, 4));
        } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
            ctx->shm = static_cast<struct wl_shm*>(wl_registry_bind(registry, id, &wl_shm_interface, 1));
        } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
            ctx->seat = static_cast<struct wl_seat*>(wl_registry_bind(registry, id, &wl_seat_interface, 1));
        } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
            auto* output = static_cast<struct wl_output*>(wl_registry_bind(registry, id, &wl_output_interface, 1));
            auto out = std::make_unique<OutputData>();
            out->ctx = ctx;
            out->output = output;
            out->global_id = id;
            ctx->outputs.push_back(std::move(out));
        } else if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
            ctx->screencopy_mgr = static_cast<struct zwlr_screencopy_manager_v1*>(wl_registry_bind(registry, id, &zwlr_screencopy_manager_v1_interface, 1));
        } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
            ctx->layer_shell = static_cast<struct zwlr_layer_shell_v1*>(wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1));
        }
    },
    .global_remove = [](void*, struct wl_registry*, uint32_t) {}
};

void run_wayland_picker(vcpicker::ColorPickedCallback callback) {
    auto ctx = std::make_unique<WaylandContext>();
    ctx->callback = callback;
    ctx->display = wl_display_connect(nullptr);
    if (!ctx->display) return;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx.get());
    wl_display_roundtrip(ctx->display);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->shm || !ctx->screencopy_mgr || !ctx->layer_shell || ctx->outputs.empty()) {
        ctx->cleanup();
        return;
    }

    for (auto& out : ctx->outputs) {
        out->frame = zwlr_screencopy_manager_v1_capture_output(ctx->screencopy_mgr, 1, out->output);
        zwlr_screencopy_frame_v1_add_listener(out->frame, &frame_listener, out.get());

        out->layer_surface = wl_compositor_create_surface(ctx->compositor);
        out->zwlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            ctx->layer_shell, out->layer_surface, out->output,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "vcpicker-picker"
        );

        zwlr_layer_surface_v1_add_listener(out->zwlr_layer_surface, &layer_surface_listener, out.get());
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
            wl_pointer_add_listener(ctx->pointer, &pointer_listener, ctx.get());
        }
        ctx->keyboard = wl_seat_get_keyboard(ctx->seat);
        if (ctx->keyboard) {
            wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx.get());
        }
    }

    wl_display_roundtrip(ctx->display);

    while (!ctx->picked && !ctx->cancelled && wl_display_dispatch(ctx->display) != -1) {
    }

    std::string picked_color = ctx->picked ? ctx->picked_hex : "";
    ctx->cleanup();

    if (!picked_color.empty() && callback) {
        callback(picked_color);
    }
}

} // namespace
#endif

namespace vcpicker {

#ifdef HAVE_X11
static void pick_color_x11(ColorPickedCallback callback) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    Cursor cursor = XCreateFontCursor(dpy, XC_crosshair);

    if (XGrabPointer(dpy, root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, root, cursor, CurrentTime) != GrabSuccess) {
        XFreeCursor(dpy, cursor);
        XCloseDisplay(dpy);
        return;
    }

    XEvent ev;
    bool picked = false;
    std::string hex = "#000000";

    while (!picked) {
        XNextEvent(dpy, &ev);
        if (ev.type == ButtonPress && ev.xbutton.button == 1) {
            XImage* image = XGetImage(dpy, root, ev.xbutton.x_root, ev.xbutton.y_root, 1, 1, AllPlanes, ZPixmap);
            if (image) {
                unsigned long pixel = XGetPixel(image, 0, 0);
                XColor c;
                c.pixel = pixel;
                XQueryColor(dpy, DefaultColormap(dpy, screen), &c);
                ColorRGB rgb{c.red / 65535.0, c.green / 65535.0, c.blue / 65535.0, 1.0};
                hex = rgbToHex(rgb);
                XDestroyImage(image);
                picked = true;
            }
        } else if (ev.type == ButtonPress && ev.xbutton.button == 3) {
            // Cancel on right click
            break;
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XFreeCursor(dpy, cursor);
    XCloseDisplay(dpy);

    if (picked && callback) {
        callback(hex);
    }
}
#endif

void ScreenPicker::pick(ColorPickedCallback callback) {
#ifdef HAVE_WAYLAND
    const char* wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display != nullptr) {
        // Probe compositor protocols
        struct wl_display* probe_display = wl_display_connect(nullptr);
        if (probe_display) {
            struct wl_registry* probe_registry = wl_display_get_registry(probe_display);
            bool has_screencopy = false;
            bool has_layer_shell = false;

            struct ProbeCtx {
                bool* sc;
                bool* ls;
            } probe_ctx{&has_screencopy, &has_layer_shell};

            static const struct wl_registry_listener probe_listener = {
                .global = [](void* data, struct wl_registry*, uint32_t, const char* interface, uint32_t) {
                    auto* c = static_cast<ProbeCtx*>(data);
                    if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
                        *(c->sc) = true;
                    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
                        *(c->ls) = true;
                    }
                },
                .global_remove = [](void*, struct wl_registry*, uint32_t) {}
            };

            wl_registry_add_listener(probe_registry, &probe_listener, &probe_ctx);
            wl_display_roundtrip(probe_display);
            wl_display_roundtrip(probe_display);
            wl_registry_destroy(probe_registry);
            wl_display_disconnect(probe_display);

            if (has_screencopy && has_layer_shell) {
                std::thread([callback]() {
                    run_wayland_picker(callback);
                }).detach();
                return;
            }
        }
    }
#endif

#ifdef HAVE_X11
    if (getenv("DISPLAY") != nullptr) {
        std::thread([callback]() {
            pick_color_x11(callback);
        }).detach();
        return;
    }
#endif
}

} // namespace vcpicker
