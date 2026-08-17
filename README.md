# VCPicker

**VCPicker** is a highly professional, modern, and lightweight color picker application designed specifically for the VAXP ecosystem. Built from the ground up utilizing **C** and **GTK4 / libadwaita**, it offers a sleek, hardware-accelerated user interface complete with custom native transparent styling and advanced precision tools for artists, designers, and developers.

## ✨ Features

- **🔍 Precision Screen Picking**: Pick any color from your screen reliably across both **Wayland** and **X11** display servers through native XDG Desktop Portal integration.
- **🎨 Instant Color Formatting**: View and copy your matched colors immediately in multiple formats including **HEX, RGB, HSL, HSV**, and **CMYK**.
- **🧠 Intelligent Palette Generation**: Automatically generates a mathematically perfect Complementary color for every color you pick.
- **📊 WCAG Contrast Verification**: Instantly checks your chosen color against both pure white and pure black backgrounds, supplying real-time WCAG AA and AAA accessibility contrast scores.
- **🖼️ Custom UI Aesthetics**: Native ARGB transparent window background powered by GTK CSS overrides for an elegant ecosystem appearance.
- **�� System Integrations**: Packaged with a custom `.desktop` entry and `GSettings` DBus activatable environment.

## 🛠️ Build Requirements

- `meson` (>= 0.62.0)
- `ninja`
- `gtk4` (>= 4.10.0)

## 🚀 Building and Running

You can easily build the project natively using the Meson build system:

```bash
meson setup build --prefix=/usr --buildtype=release
meson compile -C build
meson install -C build
```

---
*Built for VAXP-OS*
