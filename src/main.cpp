/// @file main.cpp
/// @brief VCPicker — Desktop Reactive Color Studio Main Entrypoint.

#include "enki/app/app.hpp"
#include "ui/vcpicker_app.hpp"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================\n";
    std::cout << "  VCPicker — ENKI Desktop Color Studio (VAXP OS)\n";
    std::cout << "================================================\n";

    enki::AppConfig config;
    config.title       = "VCPicker";
    config.width       = 440;
    config.height      = 730;
    config.resizable   = false;
    config.enable_csd  = true;
    config.app_id      = "org.vaxp.ColorPicker";
    config.clear_color = 0x00000000; // Translucent native surface for rounded CSD

    std::string initial_hex = "#3584E4";
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        initial_hex = argv[1];
    }

    return enki::runApp(std::make_shared<VCPickerApp>(initial_hex), config);
}
