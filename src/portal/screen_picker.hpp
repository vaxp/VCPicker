#pragma once
/// @file screen_picker.hpp
/// @brief Pure C++20 Sovereign Screen Color Picker for VAXP OS.

#include <string>
#include <functional>

namespace vcpicker {

using ColorPickedCallback = std::function<void(const std::string& hex)>;

class ScreenPicker {
public:
    /// @brief Trigger full-screen interactive color picker with magnifying loupe.
    /// Uses native Wayland protocols (wlr-screencopy + wlr-layer-shell) on sovereign compositors,
    /// and X11/XWayland fallback when running on non-sovereign compositors.
    static void pick(ColorPickedCallback callback);
};

} // namespace vcpicker
