#pragma once
/// @file color_engine.hpp
/// @brief Pure C++20 Sovereign Color Math & Palette Engine for VAXP OS.

#include <string>
#include <string_view>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdint>

namespace vcpicker {

struct ColorRGB {
    double r = 0.0; // 0.0 - 1.0
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;
};

struct ColorHSL {
    double h = 0.0; // 0.0 - 360.0
    double s = 0.0; // 0.0 - 1.0
    double l = 0.0; // 0.0 - 1.0
    double a = 1.0;
};

struct ColorHSV {
    double h = 0.0; // 0.0 - 360.0
    double s = 0.0; // 0.0 - 1.0
    double v = 0.0; // 0.0 - 1.0
    double a = 1.0;
};

struct ColorCMYK {
    double c = 0.0; // 0.0 - 1.0
    double m = 0.0;
    double y = 0.0;
    double k = 0.0;
    double a = 1.0;
};

inline ColorHSL rgbToHsl(const ColorRGB& rgb) {
    ColorHSL hsl{0.0, 0.0, 0.0, rgb.a};
    double r = rgb.r, g = rgb.g, b = rgb.b;
    double min_v = std::min({r, g, b});
    double max_v = std::max({r, g, b});
    double delta = max_v - min_v;

    hsl.l = (max_v + min_v) / 2.0;

    if (delta == 0.0) {
        hsl.h = 0.0;
        hsl.s = 0.0;
    } else {
        hsl.s = hsl.l < 0.5 ? delta / (max_v + min_v) : delta / (2.0 - max_v - min_v);
        if (max_v == r) {
            hsl.h = (g - b) / delta + (g < b ? 6.0 : 0.0);
        } else if (max_v == g) {
            hsl.h = (b - r) / delta + 2.0;
        } else {
            hsl.h = (r - g) / delta + 4.0;
        }
        hsl.h *= 60.0;
    }
    return hsl;
}

inline ColorHSV rgbToHsv(const ColorRGB& rgb) {
    ColorHSV hsv{0.0, 0.0, 0.0, rgb.a};
    double r = rgb.r, g = rgb.g, b = rgb.b;
    double min_v = std::min({r, g, b});
    double max_v = std::max({r, g, b});
    double delta = max_v - min_v;

    hsv.v = max_v;

    if (delta == 0.0) {
        hsv.h = 0.0;
        hsv.s = 0.0;
    } else {
        hsv.s = delta / max_v;
        if (max_v == r) {
            hsv.h = (g - b) / delta + (g < b ? 6.0 : 0.0);
        } else if (max_v == g) {
            hsv.h = (b - r) / delta + 2.0;
        } else {
            hsv.h = (r - g) / delta + 4.0;
        }
        hsv.h *= 60.0;
    }
    return hsv;
}

inline ColorCMYK rgbToCmyk(const ColorRGB& rgb) {
    ColorCMYK cmyk{0.0, 0.0, 0.0, 0.0, rgb.a};
    double max_v = std::max({rgb.r, rgb.g, rgb.b});
    cmyk.k = 1.0 - max_v;
    if (cmyk.k < 1.0) {
        cmyk.c = (1.0 - rgb.r - cmyk.k) / (1.0 - cmyk.k);
        cmyk.m = (1.0 - rgb.g - cmyk.k) / (1.0 - cmyk.k);
        cmyk.y = (1.0 - rgb.b - cmyk.k) / (1.0 - cmyk.k);
    }
    return cmyk;
}

inline std::string rgbToHex(const ColorRGB& rgb) {
    int r = std::clamp(static_cast<int>(rgb.r * 255.0 + 0.5), 0, 255);
    int g = std::clamp(static_cast<int>(rgb.g * 255.0 + 0.5), 0, 255);
    int b = std::clamp(static_cast<int>(rgb.b * 255.0 + 0.5), 0, 255);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return std::string(buf);
}

inline ColorRGB hexToRgb(std::string_view hex) {
    ColorRGB rgb{0.0, 0.0, 0.0, 1.0};
    if (hex.empty()) return rgb;
    if (hex.front() == '#') hex.remove_prefix(1);

    if (hex.length() >= 6) {
        unsigned int r = 0, g = 0, b = 0;
        if (std::sscanf(hex.data(), "%02x%02x%02x", &r, &g, &b) == 3) {
            rgb.r = r / 255.0;
            rgb.g = g / 255.0;
            rgb.b = b / 255.0;
        }
    }
    return rgb;
}

namespace detail {
inline double hueToRgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}
} // namespace detail

inline ColorRGB hslToRgb(const ColorHSL& hsl) {
    ColorRGB rgb{0.0, 0.0, 0.0, hsl.a};
    if (hsl.s == 0.0) {
        rgb.r = rgb.g = rgb.b = hsl.l;
    } else {
        double q = hsl.l < 0.5 ? hsl.l * (1.0 + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
        double p = 2.0 * hsl.l - q;
        double hk = hsl.h / 360.0;
        rgb.r = detail::hueToRgb(p, q, hk + 1.0 / 3.0);
        rgb.g = detail::hueToRgb(p, q, hk);
        rgb.b = detail::hueToRgb(p, q, hk - 1.0 / 3.0);
    }
    return rgb;
}

inline ColorRGB getComplementary(const ColorRGB& base) {
    ColorHSL hsl = rgbToHsl(base);
    hsl.h += 180.0;
    if (hsl.h >= 360.0) hsl.h -= 360.0;
    return hslToRgb(hsl);
}

inline std::pair<ColorRGB, ColorRGB> getAnalogous(const ColorRGB& base) {
    ColorHSL hsl = rgbToHsl(base);

    ColorHSL h1 = hsl;
    h1.h += 30.0;
    if (h1.h >= 360.0) h1.h -= 360.0;

    ColorHSL h2 = hsl;
    h2.h -= 30.0;
    if (h2.h < 0.0) h2.h += 360.0;

    return {hslToRgb(h1), hslToRgb(h2)};
}

namespace detail {
inline double relativeLuminance(const ColorRGB& c) {
    auto toLinear = [](double val) {
        return val <= 0.03928 ? val / 12.92 : std::pow((val + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * toLinear(c.r) + 0.7152 * toLinear(c.g) + 0.0722 * toLinear(c.b);
}
} // namespace detail

inline double getContrastRatio(const ColorRGB& c1, const ColorRGB& c2) {
    double l1 = detail::relativeLuminance(c1);
    double l2 = detail::relativeLuminance(c2);
    if (l1 > l2) {
        return (l1 + 0.05) / (l2 + 0.05);
    } else {
        return (l2 + 0.05) / (l1 + 0.05);
    }
}

inline bool passesAA(double ratio) {
    return ratio >= 4.5;
}

inline bool passesAAA(double ratio) {
    return ratio >= 7.0;
}

} // namespace vcpicker
