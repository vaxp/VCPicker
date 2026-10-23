/// @file vcpicker_app.cpp
/// @brief VCPicker — ENKI Desktop Reactive Color Studio Implementation.

#include "vcpicker_app.hpp"

#include "../core/color_engine.hpp"
#include "../portal/screen_picker.hpp"

#include "enki/widgets/window_frame.hpp"
#include "enki/widgets/titlebar.hpp"
#include "enki/widgets/container.hpp"
#include "enki/widgets/flexbox.hpp"
#include "enki/widgets/text.hpp"
#include "enki/widgets/text_field.hpp"
#include "enki/widgets/button.hpp"
#include "enki/widgets/icon_button.hpp"
#include "enki/widgets/icons_material.hpp"
#include "enki/widgets/card.hpp"
#include "enki/widgets/scroll_view.hpp"
#include "enki/widgets/stack.hpp"
#include "enki/widgets/gesture_detector.hpp"
#include "enki/platform/platform.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>

using namespace enki;

namespace {

Color hexToColor(const std::string& hex) {
    if (hex.empty() || hex[0] != '#') return 0xFF3584E4;
    std::string s = hex.substr(1);
    if (s.length() == 6) {
        try {
            uint32_t val = std::stoul(s, nullptr, 16);
            return 0xFF000000 | val;
        } catch (...) {
            return 0xFF3584E4;
        }
    }
    return 0xFF3584E4;
}

std::string formatRatio(double ratio) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << ratio << ":1";
    return ss.str();
}

} // namespace

class VCPickerAppState : public State {
public:
    std::string current_hex = "#3584E4";
    std::string copied_format = "";
    std::chrono::steady_clock::time_point copied_time;
    std::shared_ptr<TextFieldController> hex_controller;

    void initState() override {
        State::initState();
        auto* app = static_cast<const VCPickerApp*>(widget());
        if (app && !app->initial_hex.empty()) {
            current_hex = app->initial_hex;
        }
        hex_controller = std::make_shared<TextFieldController>(current_hex);
    }

    void setColor(std::string hex) {
        if (hex.empty()) return;
        if (hex[0] != '#') hex = "#" + hex;
        std::transform(hex.begin(), hex.end(), hex.begin(), ::toupper);
        if (hex.length() > 7) hex = hex.substr(0, 7);

        setState([this, hex]() {
            current_hex = hex;
            if (hex_controller && hex_controller->text != hex) {
                hex_controller->text = hex;
            }
        });
    }

    void copyToClipboard(const std::string& text, const std::string& format_name) {
        if (auto* plat = Platform::instance()) {
            plat->setClipboardText(text);
        }
        setState([this, format_name]() {
            copied_format = format_name;
            copied_time = std::chrono::steady_clock::now();
        });
    }

    void triggerScreenPick() {
        vcpicker::ScreenPicker::pick([this](const std::string& hex) {
            if (!hex.empty()) {
                this->setColor(hex);
            }
        });
    }

    WidgetPtr buildFormatRow(const std::string& label, const std::string& value, Color tag_color) {
        bool is_copied = (copied_format == label);

        auto tag = container({
            .color = tag_color & 0x2EFFFFFF,
            .border_radius = BorderRadius::circular(6.0f),
            .border = Border(tag_color, 1.0f),
            .align = Alignment::Center,
            .width = 54_px,
            .height = 28_px,
            .child = text(label, {
                .color = tag_color,
                .font_size = 11.0f,
                .font_weight = FontWeight::Bold,
            })
        });

        auto value_widget = expanded(container({
            .align = Alignment::CenterLeft,
            .padding = StyleInsets::symmetric(0.0f, 10.0f),
            .child = text(value, {
                .color = 0xFFF1F5F9,
                .font_size = 13.0f,
                .font_weight = FontWeight::Medium,
            })
        }));

        auto copy_btn = gestureDetector({
            .child = container({
                .color = is_copied ? 0x4010B981 : 0x20334155,
                .border_radius = BorderRadius::circular(6.0f),
                .border = Border(is_copied ? 0xFF10B981 : 0x4064748B, 1.0f),
                .align = Alignment::Center,
                .width = 32_px,
                .height = 32_px,
                .child = text(is_copied ? "✓" : "❐", {
                    .color = is_copied ? 0xFF34D399 : 0xFF94A3B8,
                    .font_size = 13.0f,
                    .font_weight = FontWeight::Bold,
                })
            }),
            .on_tap = [this, value, label]() {
                copyToClipboard(value, label);
            }
        });

        return container({
            .color = 0x201E293B,
            .border_radius = BorderRadius::circular(8.0f),
            .border = Border(0x15FFFFFF, 1.0f),
            .padding = StyleInsets::symmetric(6.0f, 12.0f),
            .child = row({
                .align_items = Align::Center,
                .gap = StyleValue::point(8.0f),
                .children = {
                    tag,
                    value_widget,
                    copy_btn
                }
            })
        });
    }

    WidgetPtr buildWcagBadge(const std::string& label, bool pass) {
        return container({
            .color = pass ? 0x2510B981 : 0x25EF4444,
            .border_radius = BorderRadius::circular(6.0f),
            .border = Border(pass ? 0xFF10B981 : 0xFFEF4444, 1.0f),
            .padding = StyleInsets::symmetric(3.0f, 8.0f),
            .child = text(label + (pass ? " PASS" : " FAIL"), {
                .color = pass ? 0xFF34D399 : 0xFFF87171,
                .font_size = 10.0f,
                .font_weight = FontWeight::Bold,
            })
        });
    }

    WidgetPtr buildSwatch(const std::string& hex, const std::string& label, bool is_active) {
        Color c = hexToColor(hex);
        std::vector<BoxShadow> shadows;
        if (is_active) {
            shadows.push_back(BoxShadow::glow(0x6038BDF8, 8.0f));
        } else {
            shadows.push_back(BoxShadow(0x30000000, {0.0f, 2.0f}, 4.0f));
        }

        return gestureDetector({
            .child = column({
                .align_items = Align::Center,
                .gap = StyleValue::point(6.0f),
                .children = {
                    container({
                        .color = c,
                        .border_radius = BorderRadius::circular(10.0f),
                        .border = Border(is_active ? 0xFF38BDF8 : 0x40FFFFFF, is_active ? 2.5f : 1.0f),
                        .box_shadow = shadows,
                        .width = 44_px,
                        .height = 44_px,
                    }),
                    text(label, {
                        .color = 0xFF94A3B8,
                        .font_size = 10.0f,
                        .font_weight = FontWeight::SemiBold,
                    }),
                    text(hex, {
                        .color = 0xFFCBD5E1,
                        .font_size = 10.0f,
                    })
                }
            }),
            .on_tap = [this, hex]() {
                setColor(hex);
            }
        });
    }

    WidgetPtr build(BuildContext&) override {
        vcpicker::ColorRGB rgb = vcpicker::hexToRgb(current_hex);
        vcpicker::ColorHSL hsl = vcpicker::rgbToHsl(rgb);
        vcpicker::ColorHSV hsv = vcpicker::rgbToHsv(rgb);
        vcpicker::ColorCMYK cmyk = vcpicker::rgbToCmyk(rgb);

        Color enki_active = hexToColor(current_hex);

        // String representations
        char rgb_buf[64], hsl_buf[64], hsv_buf[64], cmyk_buf[64];
        snprintf(rgb_buf, sizeof(rgb_buf), "rgb(%d, %d, %d)",
                 (int)(rgb.r * 255.0 + 0.5), (int)(rgb.g * 255.0 + 0.5), (int)(rgb.b * 255.0 + 0.5));
        snprintf(hsl_buf, sizeof(hsl_buf), "hsl(%.0f, %.0f%%, %.0f%%)",
                 hsl.h, hsl.s * 100.0, hsl.l * 100.0);
        snprintf(hsv_buf, sizeof(hsv_buf), "hsv(%.0f, %.0f%%, %.0f%%)",
                 hsv.h, hsv.s * 100.0, hsv.v * 100.0);
        snprintf(cmyk_buf, sizeof(cmyk_buf), "cmyk(%.0f%%, %.0f%%, %.0f%%, %.0f%%)",
                 cmyk.c * 100.0, cmyk.m * 100.0, cmyk.y * 100.0, cmyk.k * 100.0);

        // WCAG Calculations
        vcpicker::ColorRGB white_rgb{1.0, 1.0, 1.0, 1.0};
        vcpicker::ColorRGB black_rgb{0.0, 0.0, 0.0, 1.0};
        double white_ratio = vcpicker::getContrastRatio(rgb, white_rgb);
        double black_ratio = vcpicker::getContrastRatio(rgb, black_rgb);
        bool white_aa = vcpicker::passesAA(white_ratio);
        bool white_aaa = vcpicker::passesAAA(white_ratio);
        bool black_aa = vcpicker::passesAA(black_ratio);
        bool black_aaa = vcpicker::passesAAA(black_ratio);

        // Harmonious Palettes
        vcpicker::ColorRGB comp_rgb = vcpicker::getComplementary(rgb);
        std::string comp_hex = vcpicker::rgbToHex(comp_rgb);

        auto [an1_rgb, an2_rgb] = vcpicker::getAnalogous(rgb);
        std::string an1_hex = vcpicker::rgbToHex(an1_rgb);
        std::string an2_hex = vcpicker::rgbToHex(an2_rgb);

        // Text contrast on hero card
        double luma = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
        Color hero_fg = luma > 0.55 ? 0xFF0F172A : 0xFFFFFFFF;
        Color hero_sub_fg = luma > 0.55 ? 0xCC0F172A : 0xCCFFFFFF;

        // 1. Hero Preview Card
        std::vector<BoxShadow> hero_shadows = { BoxShadow::glow(enki_active & 0x66FFFFFF, 16.0f) };
        auto hero_card = container({
            .color = enki_active,
            .border_radius = BorderRadius::circular(14.0f),
            .border = Border(0x30FFFFFF, 1.5f),
            .box_shadow = hero_shadows,
            .align = Alignment::CenterLeft,
            .height = 100_px,
            .padding = StyleInsets::symmetric(14.0f, 16.0f),
            .child = row({
                .align_items = Align::Center,
                .children = {
                    column({
                        .justify_content = Justify::Center,
                        .align_items = Align::Start,
                        .gap = StyleValue::point(2.0f),
                        .width = 220_px,
                        .children = {
                            text("ACTIVE COLOR", {
                                .color = hero_sub_fg,
                                .font_size = 11.0f,
                                .font_weight = FontWeight::Bold,
                            }),
                            text(current_hex, {
                                .color = hero_fg,
                                .font_size = 24.0f,
                                .font_weight = FontWeight::ExtraBold,
                            })
                        }
                    }),
                    spacer(),
                    button(ButtonProps{
                        .child = row({
                            .align_items = Align::Center,
                            .gap = StyleValue::point(6.0f),
                            .children = {
                                text("🎯", { .font_size = 14.0f }),
                                text("Pick Screen", {
                                    .color = 0xFFFFFFFF,
                                    .font_size = 12.0f,
                                    .font_weight = FontWeight::Bold,
                                })
                            }
                        }),
                        .on_pressed = [this]() { triggerScreenPick(); },
                        .normal_color = 0x50000000,
                        .hover_color = 0x80000000,
                        .pressed_color = 0xAA000000,
                        .border_radius = 8.0f,
                        .padding = EdgeInsets::symmetric(8.0f, 14.0f),
                    })
                }
            })
        });

        // 2. Format Input & Values
        auto formats_section = column({
            .gap = StyleValue::point(6.0f),
            .children = {
                buildFormatRow("HEX", current_hex, 0xFF38BDF8),
                buildFormatRow("RGB", rgb_buf, 0xFFA78BFA),
                buildFormatRow("HSL", hsl_buf, 0xFF34D399),
                buildFormatRow("HSV", hsv_buf, 0xFFFBBF24),
                buildFormatRow("CMYK", cmyk_buf, 0xFFF472B6),
            }
        });

        // 3. WCAG Contrast Card
        auto wcag_card = container({
            .color = 0x251E293B,
            .border_radius = BorderRadius::circular(10.0f),
            .border = Border(0x18FFFFFF, 1.0f),
            .padding = StyleInsets::all(12.0f),
            .child = column({
                .gap = StyleValue::point(8.0f),
                .children = {
                    text("WCAG 2.1 CONTRAST ACCESSIBILITY", {
                        .color = 0xFF94A3B8,
                        .font_size = 10.0f,
                        .font_weight = FontWeight::Bold,
                    }),
                    row({
                        .gap = StyleValue::point(10.0f),
                        .children = {
                            // On White
                            expanded(container({
                                .color = 0x15FFFFFF,
                                .border_radius = BorderRadius::circular(8.0f),
                                .padding = StyleInsets::all(8.0f),
                                .child = column({
                                    .align_items = Align::Center,
                                    .gap = StyleValue::point(6.0f),
                                    .children = {
                                        text("On White", { .color = 0xFFE2E8F0, .font_size = 11.0f, .font_weight = FontWeight::SemiBold }),
                                        text(formatRatio(white_ratio), { .color = 0xFFFFFFFF, .font_size = 14.0f, .font_weight = FontWeight::Bold }),
                                        row({
                                            .gap = StyleValue::point(4.0f),
                                            .children = {
                                                buildWcagBadge("AA", white_aa),
                                                buildWcagBadge("AAA", white_aaa),
                                            }
                                        })
                                    }
                                })
                            })),
                            // On Black
                            expanded(container({
                                .color = 0x20000000,
                                .border_radius = BorderRadius::circular(8.0f),
                                .padding = StyleInsets::all(8.0f),
                                .child = column({
                                    .align_items = Align::Center,
                                    .gap = StyleValue::point(6.0f),
                                    .children = {
                                        text("On Black", { .color = 0xFFCBD5E1, .font_size = 11.0f, .font_weight = FontWeight::SemiBold }),
                                        text(formatRatio(black_ratio), { .color = 0xFFFFFFFF, .font_size = 14.0f, .font_weight = FontWeight::Bold }),
                                        row({
                                            .gap = StyleValue::point(4.0f),
                                            .children = {
                                                buildWcagBadge("AA", black_aa),
                                                buildWcagBadge("AAA", black_aaa),
                                            }
                                        })
                                    }
                                })
                            }))
                        }
                    })
                }
            })
        });

        // 4. Harmonious Palette
        auto palette_section = container({
            .color = 0x251E293B,
            .border_radius = BorderRadius::circular(10.0f),
            .border = Border(0x18FFFFFF, 1.0f),
            .padding = StyleInsets::all(12.0f),
            .child = column({
                .gap = StyleValue::point(10.0f),
                .children = {
                    text("HARMONIOUS COLOR PALETTE", {
                        .color = 0xFF94A3B8,
                        .font_size = 10.0f,
                        .font_weight = FontWeight::Bold,
                    }),
                    row({
                        .justify_content = Justify::SpaceAround,
                        .children = {
                            buildSwatch(current_hex, "Base", true),
                            buildSwatch(comp_hex, "Complement", false),
                            buildSwatch(an1_hex, "Analog +30°", false),
                            buildSwatch(an2_hex, "Analog -30°", false),
                        }
                    })
                }
            })
        });

        // Main Body Column
        auto body = container({
            .padding = StyleInsets::all(16.0f),
            .child = column({
                .gap = StyleValue::point(12.0f),
                .children = {
                    hero_card,
                    formats_section,
                    wcag_card,
                    palette_section,
                }
            })
        });

        // Return inside WindowFrame with VAXPOS styling
        return windowFrame(WindowFrameProps{
            .content = body,
            .title = "VCPicker — Color Harmony Studio",
            .border_radius = 16.0f,
            .border_color = 0x3338BDF8,
            .border_width = 2.0f,
            .background_color = 0xF00B0F19,
            .titlebar_background_color = 0x00000000,
            .titlebar_inactive_background_color = 0xF00B0F19,
            .titlebar_style = TitleBarStyle::VAXPOS,
            .enable_resize = false,
        });
    }
};

std::unique_ptr<State> VCPickerApp::createState() {
    return std::make_unique<VCPickerAppState>();
}
