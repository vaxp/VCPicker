#pragma once
/// @file vcpicker_app.hpp
/// @brief VCPicker — ENKI Desktop Reactive Color Studio.
/// Ported from GTK4 to ENKI C++20 for Vaxp Odyssey.

#include "enki/enki.hpp"
#include <string>
#include <memory>

class VCPickerApp : public enki::StatefulWidget {
public:
    std::string initial_hex;

    explicit VCPickerApp(std::string initial = "#3584E4")
        : initial_hex(std::move(initial)) {}

    std::unique_ptr<enki::State> createState() override;
    [[nodiscard]] std::string_view typeName() const override { return "VCPickerApp"; }
};
