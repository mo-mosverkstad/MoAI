#include <iostream>
#include <string>
#include <filesystem>
#include "commands.h"
#include "../common/config.h"
#include "../common/config_validator.h"

int main(int argc, char** argv) {
    try {
        auto& cfg = Config::instance();
        for (auto& p : {"../config/default.conf", "config/default.conf",
                        "../../config/default.conf"}) {
            if (std::filesystem::exists(p) && cfg.load(p)) {
                std::cerr << "Config loaded: " << p << "\n";
                break;
            }
        }
        if (!ConfigValidator::validate()) {
            std::cerr << "[FATAL] Config validation failed. Fix the errors above.\n";
            return 1;
        }
        return run_cli(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "[FATAL] Uncaught exception: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[FATAL] Unknown exception\n";
        return 1;
    }
}