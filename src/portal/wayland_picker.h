#ifndef VAXP_WAYLAND_PICKER_H
#define VAXP_WAYLAND_PICKER_H

#include <glib.h>
#include "picker.h"

G_BEGIN_DECLS

/**
 * vaxp_wayland_pick_color:
 * @callback: Callback to receive hex color when picked or NULL if cancelled
 * @user_data: User data for callback
 *
 * Attempts to perform native Wayland color picking via zwlr_screencopy_v1
 * and zwlr_layer_shell_v1 protocols.
 *
 * Returns: TRUE if Wayland picking started successfully, FALSE if Wayland is unavailable.
 */
gboolean vaxp_wayland_pick_color(VaxpColorPickedCallback callback, gpointer user_data);

G_END_DECLS

#endif /* VAXP_WAYLAND_PICKER_H */
