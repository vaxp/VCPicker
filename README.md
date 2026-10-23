# VCPicker — Desktop Color Studio

**VCPicker** is a high-performance, modern, and lightweight color studio designed natively for **VAXP OS**, powered by the **ENKI Reactive UI Framework (C++20)**.

---

## ✨ Features

- ** Precision Screen Picking**: Native Wayland protocol integration (`zwlr_screencopy_v1`, `zwlr_layer_shell_v1`) with magnified loupe lens, alongside automatic fallback to X11 pointer grab.
- **🎨 Instant Multi-Format Conversion**: Real-time reactive updates for **HEX**, **RGB**, **HSL**, **HSV**, and **CMYK**.
- ** One-Click Native Clipboard**: Instant clipboard copy for each format via ENKI's native platform abstraction.
- ** Harmonious Color Palette**: Real-time generation of mathematical **Complementary** and dual **Analogous** (+30° / -30°) swatches with instant one-click color switching.
- ** WCAG 2.1 Accessibility Verification**: Live contrast ratio verification against pure white (`#FFFFFF`) and deep black (`#000000`) with dynamic **AA** and **AAA** Pass/Fail status badges.
- ** Sovereign VAXPOS Aesthetics**: Deep obsidian translucent glass surface, interactive VAXPOS client-side decorations (CSD), rounded geometry (16px), and hardware-accelerated rendering.

---

## 🛠️ Build Requirements

- `meson` (>= 0.62.0)
- `ninja`
- `enki` (>= 0.1.0)
- `wayland-client`, `wayland-cursor` (optional, for Wayland direct screencopy)
- `x11` (optional, for X11 fallback)

---

## 🚀 Building and Running

```bash
meson setup build --prefix=/usr --buildtype=release
ninja -C build
./build/src/vcpicker
```

To install system-wide:
```bash
sudo meson install -C build
```

To build Debian package:
```bash
./package.sh
```

---
*Developed for VAXP OS*
